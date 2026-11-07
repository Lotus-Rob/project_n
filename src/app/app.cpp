#include "app/app.hpp"

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
};

// When a file in `folder` last changed; the oldest time when none can be read.
std::filesystem::file_time_type NewestChange(const std::filesystem::path& folder) {
    std::filesystem::file_time_type newest{};
    std::error_code error;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{ folder, error }) {
        newest = std::max(newest, entry.last_write_time(error));
    }
    return newest;
}

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

App::App(platform::Window window, gfx::Renderer renderer, Options options)
  : m_window{ std::move(window) }
  , m_renderer{ std::move(renderer) }
  , m_options{ std::move(options) }
  , m_startNs{ platform::NowNs() }
  , m_frameNs{ m_startNs } {
    if (HOT_RELOAD && !m_options.dataFolder.empty()) {
        m_dataChanged = NewestChange(m_options.dataFolder);
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
        {
            Game game(
                m_handover,
                { .scene = m_options.scene,
                  .data = m_options.data,
                  .saveSlot = m_options.saveSlot,
                  .replaySlot = m_options.replaySlot,
                  .startNs = m_startNs,
                  .now = platform::NowNs }
            );
            game.Run(stop);
        } // the workers join here: a hang at shutdown shows as no "stopped" line
        PN_LOG_INFO("logic thread and job system stopped");
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
        Present(state.visible);
    }
    PN_LOG_INFO("shutting down; {} snapshots were published after they were due", m_display.LateAdoptions());
    return 0;
}

void App::WatchData(u64 nowNs) {
    PN_PROFILE_ZONE();
    if (!HOT_RELOAD || m_options.dataFolder.empty() || nowNs < m_dataCheckNs) {
        return;
    }
    m_dataCheckNs = nowNs + DATA_CHECK_NS;
    const std::filesystem::file_time_type changed = NewestChange(m_options.dataFolder);
    if (changed == m_dataChanged) {
        return;
    }
    m_dataChanged = changed;
    // On this thread: the files are small. A worker parses them once they aren't.
    const std::expected<core::GameData, core::Error> reloaded = data::LoadGameData(m_options.dataFolder);
    if (!reloaded) {
        PN_LOG_ERROR("the game data changed but wasn't reloaded; the old data stays: {}", reloaded.error().message);
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
    const bool keyboardOrMouse = input.device != platform::Device::Gamepad;
    if (keyboardOrMouse == (m_device != platform::Device::Gamepad) && (keyboardOrMouse || input.family == m_family)) {
        return;
    }
    m_device = input.device;
    m_family = input.family;
    PN_LOG_INFO("prompts for {}", DeviceName(m_device, m_family));
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

void App::Pan(u64 nowNs) {
    const u64 elapsedNs = nowNs - std::exchange(m_frameNs, nowNs);
    const core::Snapshot* drawn = m_display.Drawn();
    if (drawn == nullptr || drawn->follow) {
        return; // the camera follows the snapshot's target as it draws
    }
    const f32 seconds = static_cast<f32>(elapsedNs) / static_cast<f32>(core::NS_PER_SECOND);
    m_camera.centre = m_camera.centre + (m_held.move * (PAN_SPEED * seconds / m_camera.zoom));
}

void App::Present(bool visible) {
    PN_PROFILE_ZONE();
    // Events are drained every iteration, drawing or not, so the queue never grows (§5).
    m_handover.TakeEvents(m_incoming);
    m_pending.insert(m_pending.end(), m_incoming.begin(), m_incoming.end());
    static_cast<void>(m_display.Update(m_handover, platform::NowNs(), core::DEFAULT_DISPLAY_DELAY_NS));
    const core::Snapshot* drawn = m_display.Drawn();
    if (drawn == nullptr) {
        return;
    }
    core::PresentFrame(
        *drawn,
        m_pending,
        visible,
        [this] { m_renderer.Acquire(); },
        platform::NowNs,
        // shortcut: nothing plays events yet; audio and particles (phase 7) replace the empty player.
        [](const core::Event& /*event*/) {},
        [this, drawn](f64 alpha) {
            if (drawn->follow) {
                m_camera.centre = FollowedCentre(*drawn->follow, alpha);
            }
            m_renderer.Draw(*drawn, alpha, m_camera);
            PN_PROFILE_FRAME();
        }
    );
    if (drawn->transition) {
        // shortcut: nothing draws the transition itself yet; its view model and screen come with the UI (phase 6).
        platform::WaitUntilNs(platform::NowNs() + TRANSITION_FRAME_NS);
    }
}

} // namespace app
