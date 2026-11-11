#include "app/app.hpp"

#include "app/damage_numbers.hpp"
#include "app/game.hpp"
#include "app/view.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/math.hpp"
#include "core/profile.hpp"
#include "core/tick_clock.hpp"
#include "data/game_data.hpp"
#include "platform/clock.hpp"

namespace app {

namespace {

// Frames during a calendar step's transition: few, leaving the cores to the step (design doc 0002 §7.3).
constexpr u64 TRANSITION_FRAME_NS = core::NS_PER_SECOND / 20;
constexpr f32 PAN_SPEED = 900.0F; // world units a second at full movement, at zoom 1
constexpr u64 DATA_CHECK_NS = core::NS_PER_SECOND / 2;
constexpr u64 LOAD_BUDGET_NS = 2 * core::NS_PER_MS; // a scene's uploads each frame: the window never freezes while one loads
#ifdef NDEBUG
constexpr bool HOT_RELOAD = false;
#else
constexpr bool HOT_RELOAD = true; // debug builds reload the game data when its files change (design doc 0002.3 §7)
#endif

// The controls that are simply an action for the logic, and what the log says when one is asked for.
struct ActionControl {
    Control control = Control::Quit;
    core::ActionKind kind = core::ActionKind::Pause;
    std::string_view asked;
};
constexpr std::array ACTION_CONTROLS{
    ActionControl{ .control = Control::AdvanceCalendar,
                   .kind = core::ActionKind::AdvanceCalendar,
                   .asked = "a month asked to pass" },
    ActionControl{ .control = Control::Save, .kind = core::ActionKind::Save, .asked = "a save asked for" },
    ActionControl{ .control = Control::Load, .kind = core::ActionKind::Load, .asked = "a load asked for" },
    ActionControl{ .control = Control::ToggleRecording,
                   .kind = core::ActionKind::ToggleRecording,
                   .asked = "a replay recording started or finished" },
    ActionControl{ .control = Control::PlayReplay,
                   .kind = core::ActionKind::PlayReplay,
                   .asked = "the replay asked to play or stop" },
    ActionControl{ .control = Control::CycleScene, .kind = core::ActionKind::CycleScene, .asked = "the next scene asked for" },
    ActionControl{ .control = Control::DebugBurst,
                   .kind = core::ActionKind::DebugBurst,
                   .asked = "a debug burst of hits asked for" },
};

std::string_view DeviceName(platform::Device device, platform::GamepadFamily family) {
    if (device == platform::Device::Keyboard || device == platform::Device::Mouse) {
        return "keyboard and mouse";
    }
    if (family == platform::GamepadFamily::Xbox) {
        return "an Xbox gamepad";
    }
    if (family == platform::GamepadFamily::PlayStation) {
        return "a PlayStation gamepad";
    }
    if (family == platform::GamepadFamily::Nintendo) {
        return "a Nintendo gamepad";
    }
    return "a gamepad";
}

} // namespace

DataWatch::DataWatch(std::filesystem::path folder)
  : m_folder{ std::move(folder) }
  , m_seen{ [](const std::filesystem::path& watched) {
      std::error_code unreadable; // logged by the first Changed(), which finds it so again
      return List(watched, unreadable);
  }(m_folder) } {
}

bool DataWatch::Changed() {
    PN_PROFILE_ZONE();
    std::error_code error;
    Listing listing = List(m_folder, error);
    if (error && !std::exchange(m_unreadable, true)) {
        PN_LOG_WARNING("the data folder {} can't be read: {}", m_folder.string(), error.message());
    }
    else if (!error && std::exchange(m_unreadable, false)) {
        PN_LOG_INFO("the data folder {} can be read again", m_folder.string());
    }
    if (listing == m_seen) {
        return false;
    }
    m_seen = std::move(listing);
    return true;
}

DataWatch::Listing DataWatch::List(const std::filesystem::path& folder, std::error_code& error) {
    Listing listing;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{ folder, error }) {
        std::error_code unreadable;
        listing.emplace_back(entry.path().filename(), entry.last_write_time(unreadable));
        if (unreadable && !error) {
            error = unreadable; // the first file that couldn't be read says why
        }
    }
    std::ranges::sort(listing);
    return listing;
}

bool HasFlag(std::span<const char* const> args, std::string_view flag) {
    return std::ranges::any_of(args, [flag](const char* arg) { return std::string_view{ arg } == flag; });
}

App::App(platform::Window window, gfx::Renderer renderer, Options options)
  : m_window{ std::move(window) }
  , m_renderer{ std::move(renderer) }
  , m_options{ std::move(options) }
  , m_startNs{ platform::NowNs() }
  , m_nextBurstNs{ m_startNs + core::NS_PER_SECOND } // the scene is resident by the first burst
  , m_numbers{ m_options.settings }
  , m_frameNs{ m_startNs } {
    m_numberTexts.reserve(NUMBER_RESERVE);
    // The main thread is the job system's thread 0: waiting on its low-priority work, it may run the
    // logic's chunks, which need the simulation's environment.
    core::math::SetFloatingPointEnvironment();
    PN_LOG_INFO("job system running {} workers", m_jobs.Workers());
    if (HOT_RELOAD && !m_options.dataFolder.empty()) {
        m_dataWatch = DataWatch{ m_options.dataFolder };
        PN_LOG_INFO("watching {} to reload the game data", m_options.dataFolder.string());
    }
    // Started here, not in the initializer list: every member it uses is built by now.
    m_logic = std::jthread{ [this](const std::stop_token& stop) { RunLogic(stop); } };
}

App::~App() {
    m_logic.request_stop();
    m_logic.join();
}

void App::RunLogic(const std::stop_token& stop) {
    // An exception that escapes is logged and stops the game (§12).
    try {
        core::math::SetFloatingPointEnvironment();
        const core::Jobs::ThreadRegistration registration{ m_jobs };
        {
            Game game(
                m_handover,
                m_jobs,
                { .manifest = m_options.manifest,
                  .scene = m_options.scene,
                  .data = m_options.data,
                  .saveSlot = m_options.saveSlot,
                  .replaySlot = m_options.replaySlot,
                  .startNs = m_startNs,
                  .now = platform::NowNs }
            );
            game.Run(stop);
        } // a save in progress finishes here: a hang at shutdown shows as no "stopped" line
        PN_LOG_INFO("logic thread stopped");
    }
    catch (const std::exception& e) {
        PN_LOG_CRITICAL("logic thread failed: {}", e.what());
        m_logicFailed = true;
    }
    catch (...) {
        PN_LOG_CRITICAL("logic thread failed: an exception of unknown type");
        m_logicFailed = true;
    }
}

i32 App::Run() {
    for (;;) {
        PN_PROFILE_ZONE();
        const platform::WindowState& state = m_window.Poll(core::TICK_NS, m_inputs);
        HandleInput(state);
        if (state.quitRequested || m_quit) {
            PN_LOG_DEBUG("quit requested");
            break;
        }
        if (m_options.quitAfterNs != 0 && platform::NowNs() - m_startNs >= m_options.quitAfterNs) {
            PN_LOG_INFO("quitting after {} seconds, as --quit-after asked", m_options.quitAfterNs / core::NS_PER_SECOND);
            break;
        }
        if (m_logicFailed) {
            return 1;
        }
        Pan(platform::NowNs());
        WatchData(platform::NowNs());
        RepeatBurst(platform::NowNs());
        if (!Present(state)) {
            return 1;
        }
    }
    PN_LOG_INFO("shutting down; {} snapshots were published after they were due", m_display.LateAdoptions());
    return 0;
}

void App::RepeatBurst(u64 nowNs) {
    PN_PROFILE_ZONE();
    if (!m_options.burst || nowNs < m_nextBurstNs) {
        return;
    }
    m_nextBurstNs = nowNs + core::NS_PER_SECOND;
    m_handover.SendInput({ .kind = core::ActionKind::DebugBurst });
}

void App::WatchData(u64 nowNs) {
    PN_PROFILE_ZONE();
    if (!HOT_RELOAD || m_options.dataFolder.empty() || nowNs < m_dataCheckNs) {
        return;
    }
    m_dataCheckNs = nowNs + DATA_CHECK_NS;
    if (!m_dataWatch.Changed()) {
        return;
    }
    // On this thread: the files are small. A worker parses them once they aren't.
    const std::expected<core::GameData, core::Error> reloaded = data::LoadGameData(m_options.dataFolder);
    if (const std::expected<void, core::Error> runs =
            reloaded.and_then([this](const core::GameData& data) { return game::CheckScenes(*m_options.manifest, data); });
        !runs) {
        PN_LOG_ERROR("the game data changed but wasn't reloaded; the old data stays: {}", runs.error().message);
        return;
    }
    PN_LOG_INFO("the game data changed: reloading it");
    m_handover.SendInput({ .kind = core::ActionKind::ReloadData, .data = std::make_shared<const core::GameData>(*reloaded) });
}

void App::HandleInput(const platform::WindowState& state) {
    PN_PROFILE_ZONE();
    if (platform::PausesGame(state) != m_paused) {
        m_paused = !m_paused;
        PN_LOG_INFO("{}: the window is {}", m_paused ? "paused" : "resumed", m_paused ? "unfocused or minimised" : "back");
        m_handover.SendInput({ .kind = m_paused ? core::ActionKind::Pause : core::ActionKind::Resume });
    }
    if (m_device.TakeCursor(m_window.Cursor())) {
        PN_LOG_INFO("prompts for {}", DeviceName(m_device.Used(), m_device.Family()));
    }
    const std::span actionControls{ ACTION_CONTROLS }; // a span's iterator is a class everywhere, unlike std::array's
    for (const platform::InputEvent& input : m_inputs) {
        NoteDevice(input);
        const std::optional<ControlEvent> control = m_bindings.Handle(input);
        if (!control || !control->pressed) {
            continue;
        }
        if (control->control == Control::Quit) {
            m_quit = true;
        }
        else if (control->control == Control::Zoom) {
            m_camera.zoom = gfx::ZoomedBy(m_camera.zoom, control->value);
        }
        else if (control->control == Control::Select) {
            Select(state, control->position);
        }
        else if (
            const auto action = std::ranges::find(actionControls, control->control, &ActionControl::control);
            action != actionControls.end()
        ) {
            PN_LOG_INFO("{}", action->asked);
            m_handover.SendInput({ .kind = action->kind });
        }
    }
    const core::HeldInput held{ .move = m_bindings.Move() };
    if (held != m_held) {
        m_held = held;
        m_handover.SendHeld(held);
    }
}

void App::NoteDevice(const platform::InputEvent& input) {
    if (m_device.Take(input)) {
        PN_LOG_INFO("prompts for {}", DeviceName(m_device.Used(), m_device.Family()));
    }
}

void App::Select(const platform::WindowState& state, core::Vec2 position) {
    const core::Snapshot* drawn = m_display.Drawn();
    if (drawn == nullptr) {
        return;
    }
    // The picture the player clicked on: the drawn snapshot at the render clock, as the camera shows it.
    const f64 alpha = core::Alpha(drawn->scheduledNs, platform::NowNs(), core::DEFAULT_DISPLAY_DELAY_NS, drawn->advanced);
    const core::StableId target = Pick(*drawn, alpha, WindowToWorld(m_camera, position, state.width, state.height));
    PN_LOG_DEBUG("clicked on entity {}", std::to_underlying(target));
    m_handover.SendInput({ .kind = core::ActionKind::Select, .target = target });
}

std::optional<std::size_t> App::Hovered(const platform::WindowState& state, const core::Snapshot& drawn, f64 alpha) const {
    PN_PROFILE_ZONE();
    const std::optional<core::Vec2> cursor = m_window.Cursor();
    if (!cursor || m_device.Used() == platform::Device::Gamepad) {
        return std::nullopt; // a gamepad has no cursor to hover with
    }
    return PickInstance(drawn, alpha, WindowToWorld(m_camera, *cursor, state.width, state.height));
}

void App::Pan(u64 nowNs) {
    PN_PROFILE_ZONE();
    const u64 elapsedNs = nowNs - std::exchange(m_frameNs, nowNs);
    const core::Snapshot* drawn = m_display.Drawn();
    if (drawn == nullptr || drawn->follow) {
        return; // the camera follows the snapshot's target as it draws
    }
    const f32 seconds = static_cast<f32>(elapsedNs) / static_cast<f32>(core::NS_PER_SECOND);
    m_camera.centre = m_camera.centre + (m_held.move * (PAN_SPEED * seconds / m_camera.zoom));
}

bool App::LoadScene(core::SceneId wanted) {
    PN_PROFILE_ZONE();
    if (wanted == m_reported) {
        return true;
    }
    // A load begun finishes first, wanted or not: the renderer loads one scene at a time.
    const core::SceneId scene = m_loading.value_or(wanted);
    if (!m_loading) {
        m_loading = scene;
        m_loadStartNs = platform::NowNs();
        m_renderer.BeginScene(scene, m_jobs);
    }
    const std::string& name = m_options.manifest->Scene(scene).name;
    const std::expected<bool, core::Error> held = m_renderer.ContinueScene(LOAD_BUDGET_NS);
    if (!held) {
        PN_LOG_CRITICAL("the scene {} couldn't be loaded: {}", name, held.error().message);
        return false;
    }
    if (*held) {
        m_loading.reset();
        m_reported = scene;
        m_handover.SendInput({ .kind = core::ActionKind::SceneResident, .scene = scene });
        PN_LOG_INFO("scene {} resident in {} ms", name, (platform::NowNs() - m_loadStartNs) / core::NS_PER_MS);
    }
    return true;
}

bool App::Present(const platform::WindowState& state) {
    PN_PROFILE_ZONE();
    // Events are drained every iteration, drawing or not, so the queue never grows (§5).
    m_handover.TakeEvents(m_incoming);
    m_pending.insert(m_pending.end(), m_incoming.begin(), m_incoming.end());
    static_cast<void>(m_display.Update(m_handover, platform::NowNs(), core::DEFAULT_DISPLAY_DELAY_NS));
    const core::Snapshot* drawn = m_display.Drawn();
    if (drawn == nullptr) {
        return true;
    }
    if (!LoadScene(drawn->scene)) {
        return false;
    }
    ShowScene(m_camera, m_shown, drawn->scene);
    if (m_numbersEpoch != drawn->epoch) {
        m_numbers.Clear(); // a new timeline: none of the old one's hits happened in it
        m_numbersEpoch = drawn->epoch;
    }
    core::PresentFrame(
        *drawn,
        m_pending,
        state.visible,
        [this] { m_renderer.Acquire(); },
        platform::NowNs,
        // Audio and particles (phase 7) play the events too.
        [this](const core::Event& event) { m_numbers.Play(event); },
        [this, drawn, &state](f64 alpha) {
            if (drawn->follow) {
                m_camera.centre = FollowedCentre(*drawn->follow, alpha);
            }
            const f64 renderTick = core::RenderTick(drawn->simulationTick, alpha);
            m_numberTexts.clear();
            m_numbers.Texts(renderTick, m_numberTexts);
            m_renderer.Draw(
                *drawn,
                { .alpha = alpha,
                  .camera = m_camera,
                  .outlined = Hovered(state, *drawn, alpha),
                  .clock = renderTick / static_cast<f64>(core::TICKS_PER_SECOND) },
                m_jobs,
                m_numberTexts
            );
            PN_PROFILE_FRAME();
        }
    );
    if (!state.visible) {
        // Hidden, numbers still age out, so the pool doesn't grow while nothing is drawn: by the earliest the render
        // clock can be at for this snapshot, which is near enough for numbers no one sees.
        m_numbers.Forget(core::RenderTick(drawn->simulationTick, 0.0));
    }
    if (drawn->transition) {
        // Nothing draws the transition itself yet; its view model and screen come with the UI (phase 6).
        platform::WaitUntilNs(platform::NowNs() + TRANSITION_FRAME_NS);
    }
    return true;
}

} // namespace app
