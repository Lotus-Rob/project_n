#include "app/app.hpp"

#include "app/camera_path.hpp"
#include "app/cast.hpp"
#include "app/click.hpp"
#include "app/damage_numbers.hpp"
#include "app/game.hpp"
#include "app/loop_end.hpp"
#include "app/view.hpp"
#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/math.hpp"
#include "core/path_text.hpp"
#include "core/profile.hpp"
#include "core/tick_clock.hpp"
#include "data/animations.hpp"
#include "data/effects.hpp"
#include "data/game_data.hpp"
#include "data/seals.hpp"
#include "data/world_look.hpp"
#include "platform/clock.hpp"

namespace app {

namespace {

// Frames during a calendar step's transition: few, leaving the cores to the step.
constexpr u64 TRANSITION_FRAME_NS = core::NS_PER_SECOND / 20;
constexpr u64 DATA_CHECK_NS = core::NS_PER_SECOND / 2;
// `measure` reads it, from the log and from Tracy, as the start of the path's month step segment.
constexpr std::string_view MONTH_STEP_MESSAGE = "camera path: month step";
constexpr u64 LOAD_BUDGET_NS = 2 * core::NS_PER_MS; // a scene's uploads each frame: the window never freezes while one loads
#ifdef NDEBUG
constexpr bool HOT_RELOAD = false;
#else
constexpr bool HOT_RELOAD = true; // debug builds reload the game data when its files change
#endif

/// The controls that are simply an action for the logic, and what the log says when one is asked for.
struct ActionControl {
    Control control = Control::Quit;
    core::ActionKind kind = core::ActionKind::Pause;
    std::string_view asked;
};
constexpr std::array ACTION_CONTROLS{
    ActionControl{ .control = Control::AdvanceCalendar,
                   .kind = core::ActionKind::AdvanceCalendar,
                   .asked = "a month asked to pass" },
    ActionControl{ .control = Control::PauseWorld,
                   .kind = core::ActionKind::ToggleWorldPause,
                   .asked = "the world's pause asked for by a key or button" },
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

/// `durationNs` nanoseconds in milliseconds, for the log.
f64 Milliseconds(u64 durationNs) {
    return static_cast<f64>(durationNs) / static_cast<f64>(core::NS_PER_MS);
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
        PN_LOG_WARNING("the data folder {} can't be read: {}", core::PathText(m_folder), error.message());
    }
    else if (!error && std::exchange(m_unreadable, false)) {
        PN_LOG_INFO("the data folder {} can be read again", core::PathText(m_folder));
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

std::expected<f32, core::Error> ZoomOf(std::span<const char* const> args) {
    const auto flag = std::ranges::find(args, std::string_view{ "--zoom" });
    if (flag == args.end()) {
        return 1.0F;
    }
    const std::string_view text = std::next(flag) == args.end() ? std::string_view{} : std::string_view{ *std::next(flag) };
    f32 zoom = 0.0F;
    const char* last = std::to_address(text.end());
    const auto [end, error] = std::from_chars(std::to_address(text.begin()), last, zoom);
    if (error != std::errc{} || end != last || !std::isfinite(zoom) || zoom < gfx::MIN_ZOOM || zoom > gfx::MAX_ZOOM) {
        return std::unexpected{ core::Error{
            std::format("--zoom: '{}' isn't a zoom from {} to {}", text, gfx::MIN_ZOOM, gfx::MAX_ZOOM) } };
    }
    return zoom;
}

std::expected<u32, core::Error> WorkersOf(std::span<const char* const> args) {
    const auto flag = std::ranges::find(args, std::string_view{ "--workers" });
    if (flag == args.end()) {
        return 0;
    }
    const std::string_view text = std::next(flag) == args.end() ? std::string_view{} : std::string_view{ *std::next(flag) };
    u32 workers = 0;
    const char* last = std::to_address(text.end());
    const auto [end, error] = std::from_chars(std::to_address(text.begin()), last, workers);
    if (error != std::errc{} || end != last || workers < 1 || workers > MAX_WORKERS) {
        return std::unexpected{ core::Error{ std::format("--workers: '{}' isn't a count from 1 to {}", text, MAX_WORKERS) } };
    }
    return workers;
}

std::expected<std::optional<u16>, core::Error> TravelShareOf(std::span<const char* const> args) {
    const auto flag = std::ranges::find(args, std::string_view{ "--travel-share" });
    if (flag == args.end()) {
        return std::nullopt;
    }
    const std::string_view text = std::next(flag) == args.end() ? std::string_view{} : std::string_view{ *std::next(flag) };
    u16 share = 0;
    const char* last = std::to_address(text.end());
    const auto [end, error] = std::from_chars(std::to_address(text.begin()), last, share);
    if (error != std::errc{} || end != last || share > core::MOST_SHARE_PER_MILLE) {
        return std::unexpected{ core::Error{
            std::format("--travel-share: '{}' isn't a share from 0 to {} per thousand", text, core::MOST_SHARE_PER_MILLE) } };
    }
    return share;
}

std::expected<std::optional<f32>, core::Error> MonthStepAtOf(std::span<const char* const> args) {
    const auto flag = std::ranges::find(args, std::string_view{ "--month-step-at" });
    if (flag == args.end()) {
        return std::nullopt;
    }
    if (!HasFlag(args, "--camera-path")) {
        return std::unexpected{ core::Error{ "--month-step-at: needs --camera-path, whose path it steps a month in" } };
    }
    const std::string_view text = std::next(flag) == args.end() ? std::string_view{} : std::string_view{ *std::next(flag) };
    f32 seconds = 0.0F;
    const char* last = std::to_address(text.end());
    const auto [end, error] = std::from_chars(std::to_address(text.begin()), last, seconds);
    if (error != std::errc{} || end != last || !std::isfinite(seconds) || seconds < 0.0F || CameraPathOver(seconds)) {
        return std::unexpected{ core::Error{
            std::format("--month-step-at: '{}' isn't a time in seconds within the camera path, 0 or more", text) } };
    }
    return seconds;
}

DataReload ReloadOf(
    const core::Effects& effects,
    const std::expected<core::Effects, core::Error>& readEffects,
    const core::GameData& data,
    const std::expected<core::GameData, core::Error>& readData
) {
    return {
        .effects = readEffects && readEffects->Hash() != effects.Hash(),
        .gameData = readData && readData->Hash() != data.Hash(),
    };
}

void KeepWorld(const core::GameData& running, std::expected<core::GameData, core::Error>& read) {
    if (read) {
        read->world = running.world;
        read->names = running.names;
    }
}

bool KeepSeals(core::Seals& current, std::expected<core::Seals, core::Error> read) {
    if (!read) {
        return false;
    }
    current = std::move(*read);
    return true;
}

bool KeepAnimations(std::shared_ptr<const core::Animations>& current, std::expected<core::Animations, core::Error> read) {
    if (!read) {
        return false;
    }
    current = std::make_shared<const core::Animations>(std::move(*read));
    return true;
}

bool KeepLook(core::WorldLook& current, std::expected<core::WorldLook, core::Error> read) {
    if (!read || read->Hash() == current.Hash()) {
        return false;
    }
    current = std::move(*read);
    return true;
}

bool HasFlag(std::span<const char* const> args, std::string_view flag) {
    return std::ranges::any_of(args, [flag](const char* arg) { return std::string_view{ arg } == flag; });
}

App::App(platform::Window window, gfx::Renderer renderer, Options options)
  : m_jobs{ options.workers != 0 ? options.workers : core::Jobs::DefaultWorkers(), /*externalThreads=*/1 }
  , m_window{ std::move(window) }
  , m_renderer{ std::move(renderer) }
  , m_options{ std::move(options) }
  , m_startNs{ platform::NowNs() }
  , m_nextBurstNs{ m_startNs + core::NS_PER_SECOND } // the scene is resident by the first burst
  , m_nextCastNs{ m_startNs + core::NS_PER_SECOND }  // likewise by the first cast
  , m_numbers{ m_options.settings }
  , m_hud( // parentheses: the strings' pointer is copied into a parameter (MSVC C4868)
        m_options.strings,
        { .language = m_options.settings.language,
          .outline = m_options.settings.textOutline,
          .shadow = m_options.settings.textShadow },
        m_options.settings.uiColours
    )
  , m_camera{ m_options.zoom }
  , m_cameraNs{ m_startNs }
  , m_monthStep{ m_options.monthStepAt }
  , m_debugSight{ m_options.debugSight } {
    m_numberTexts.reserve(NUMBER_RESERVE);
    ApplyEffects(m_options.effects);
    m_renderer.SetSeals(m_options.seals);
    m_renderer.SetWorld(m_options.data.world, m_options.worldLook);
    m_renderer.SetNames(m_options.data.names);
    // The main thread is the job system's thread 0: waiting on its low-priority work, it may run the
    // logic's chunks, which need the simulation's environment.
    core::math::SetFloatingPointEnvironment();
    PN_LOG_INFO("job system running {} workers", m_jobs.Workers());
    if (HOT_RELOAD && !m_options.dataFolder.empty()) {
        m_dataWatch = DataWatch{ m_options.dataFolder };
        PN_LOG_INFO("watching {} to reload the game data", core::PathText(m_options.dataFolder));
    }
    // Started here, not in the initializer list: every member it uses is built by now. The thread gets its own copy of
    // what it is built from, so it never reads m_options, which the main thread changes on a data reload.
    m_logic = std::jthread{ [this,
                             setup = GameSetup{ .manifest = m_options.manifest,
                                                .scene = m_options.scene,
                                                .data = m_options.data,
                                                .saveSlot = m_options.saveSlot,
                                                .replaySlot = m_options.replaySlot,
                                                .startNs = m_startNs,
                                                .now = platform::NowNs }](const std::stop_token& stop) mutable {
        RunLogic(stop, std::move(setup));
    } };
}

JobSystemStopLine::~JobSystemStopLine() {
    PN_LOG_INFO("job system stopped");
}

App::~App() {
    StopLogic();
}

void App::RunLogic(const std::stop_token& stop, GameSetup setup) {
    // An exception that escapes stops the game; the main thread reports it.
    const auto fail = [this](std::string text) {
        const std::scoped_lock lock{ m_logicErrorMutex };
        m_logicError = core::Error{ std::move(text) };
        m_logicFailed = true;
    };
    try {
        core::math::SetFloatingPointEnvironment();
        const core::Jobs::ThreadRegistration registration{ m_jobs };
        {
            Game game(m_handover, m_jobs, std::move(setup));
            game.Run(stop);
        } // a save in progress finishes here: a hang at shutdown shows as no "stopped" line
        PN_LOG_INFO("logic thread stopped");
    }
    catch (const std::exception& e) {
        fail(e.what());
    }
    catch (...) {
        fail("an exception of unknown type");
    }
}

std::expected<void, Failure> App::Run() {
    std::expected<void, Failure> ended = Loop();
    // The logic thread keeps stepping until it is stopped, so it is stopped on every way out before its last word is read:
    // a failure after the loop's last look would otherwise vanish.
    StopLogic();
    if (m_logicFailed) {
        // The thread has joined: nothing writes the error any more.
        if (ended) {
            ended = std::unexpected{ Failure{ .what = "the game's logic failed", .error = m_logicError } };
        }
        else {
            // The loop ended on a scene failure, which is the one reported.
            PN_LOG_ERROR("the game's logic failed too: {}", m_logicError.message);
        }
    }
    return ended;
}

void App::StopLogic() {
    m_logic.request_stop();
    if (m_logic.joinable()) {
        m_logic.join();
    }
}

std::expected<void, Failure> App::Loop() {
    for (;;) {
        PN_PROFILE_ZONE();
        const platform::WindowState& state = m_window.Poll(core::TICK_NS, m_inputs);
        FollowPath(platform::NowNs());
        HandleInput(state);
        const bool quitAfter = m_options.quitAfterNs != 0 && platform::NowNs() - m_startNs >= m_options.quitAfterNs;
        const LoopEnd end = EndOf(m_logicFailed, state.quitRequested || m_quit, quitAfter);
        if (end == LoopEnd::Failed) {
            return {}; // Run() reads the logic thread's error once the thread has stopped
        }
        if (end == LoopEnd::Quit) {
            if (quitAfter) {
                PN_LOG_INFO("quitting after {} seconds, as --quit-after asked", m_options.quitAfterNs / core::NS_PER_SECOND);
            }
            else {
                PN_LOG_DEBUG("quit requested");
            }
            break;
        }
        WatchData(platform::NowNs());
        RepeatBurst(platform::NowNs());
        CastOnTimer(platform::NowNs());
        if (std::expected<void, Failure> presented = Present(state); !presented) {
            return presented;
        }
    }
    PN_LOG_INFO("shutting down; {} snapshots that moved were published after they were due", m_display.LateAdoptions());
    PN_LOG_INFO(
        "display delay: {:.1f} ms at the end, {:.1f} to {:.1f} ms over the run",
        Milliseconds(m_delay.Current()),
        Milliseconds(m_delay.Lowest()),
        Milliseconds(m_delay.Highest())
    );
    return {};
}

void App::RepeatBurst(u64 nowNs) {
    PN_PROFILE_ZONE();
    if (!m_options.burst || nowNs < m_nextBurstNs) {
        return;
    }
    m_nextBurstNs = nowNs + core::NS_PER_SECOND;
    m_handover.SendInput({ .kind = core::ActionKind::DebugBurst });
}

void App::CastOnTimer(u64 nowNs) {
    PN_PROFILE_ZONE();
    const core::Snapshot* drawn = m_display.Drawn();
    if (!m_options.castSwords || nowNs < m_nextCastNs || drawn == nullptr || !drawn->combat || !drawn->caster ||
        !drawn->follow) {
        return;
    }
    const core::CasterView& caster = *drawn->caster;
    const std::optional<core::Action> cast =
        TimedCast(caster, m_options.data.abilities, drawn->simulationTick, drawn->follow->current + CAST_SWORDS_OFFSET);
    if (!cast) {
        return;
    }
    m_handover.SendInput(*cast);
    if (cast->slot == core::Slot::Special1) {
        m_swordsLatency.Pressed(nowNs);
        m_nextCastNs = nowNs + CAST_SWORDS_NS;
        return;
    }
    // The generator again once its cooldown is over and a snapshot shows it: never a cast its cooldown refuses.
    const core::AbilityKind generator = caster.loadout.at(std::to_underlying(core::Slot::Generator));
    m_nextCastNs = nowNs + (m_options.data.abilities.Of(generator).cooldownTicks * core::TICK_NS);
}

void App::FollowPath(u64 nowNs) {
    PN_PROFILE_ZONE();
    m_pathSeconds.reset();
    if (!m_options.cameraPath) {
        return;
    }
    if (const core::Snapshot* drawn = m_display.Drawn();
        !m_pathStartNs && drawn != nullptr && drawn->sceneChange.stage == core::SceneStage::Running) {
        m_pathStartNs = nowNs + CAMERA_PATH_DELAY_NS;
    }
    if (!m_pathStartNs || nowNs < *m_pathStartNs) {
        return;
    }
    const f32 seconds = static_cast<f32>(nowNs - *m_pathStartNs) / static_cast<f32>(core::NS_PER_SECOND);
    if (CameraPathOver(seconds)) {
        if (!m_quit) {
            PN_LOG_INFO("camera path finished: quitting");
        }
        m_quit = true;
        return;
    }
    m_pathSeconds = seconds;
    if (m_monthStep.Due(seconds)) {
        PN_LOG_INFO("{}", MONTH_STEP_MESSAGE);
        PN_PROFILE_MESSAGE(MONTH_STEP_MESSAGE);
        m_handover.SendInput({ .kind = core::ActionKind::AdvanceCalendar });
    }
    const std::optional<std::size_t> segment = CameraPathSegment(seconds);
    if (segment && segment != m_pathSegment) {
        m_pathSegment = segment;
        const std::string started = std::format("camera path: {}", CameraPath()[*segment].name);
        PN_LOG_INFO("{}", started);
        PN_PROFILE_MESSAGE(started);
        if (m_paused) {
            PN_LOG_WARNING(
                "{} with the game paused (the window is unfocused or minimised): it measures no running game", started
            );
        }
    }
}

void App::MeasureLatency(const core::Snapshot& drawn, f64 alpha) {
    PN_PROFILE_ZONE();
    const u64 nowNs = platform::NowNs();
    if (const std::optional<u64> latency = m_swordsLatency.Drawn(drawn.volleys, nowNs)) {
        PN_LOG_DEBUG("swords first drawn {:.1f} ms after the press", Milliseconds(*latency));
    }
    if (!drawn.follow) {
        return;
    }
    if (const std::optional<u64> latency = m_moveLatency.Drawn(*drawn.follow, alpha, nowNs)) {
        PN_LOG_INFO("input latency: {:.1f} ms from a Move press to its first drawn frame", Milliseconds(*latency));
    }
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
    const std::expected<core::Effects, core::Error> effects = data::LoadEffects(m_options.dataFolder);
    if (!effects) {
        PN_LOG_ERROR("the effects changed but weren't reloaded; the old effects stay: {}", effects.error().message);
    }
    // The seals are drawn only, as the effects: a new table shows the next frame and the simulation never knows.
    std::expected<core::Seals, core::Error> seals = data::LoadSeals(m_options.dataFolder, *m_options.manifest);
    if (!seals) {
        PN_LOG_ERROR("the seals changed but weren't reloaded; the old seals stay: {}", seals.error().message);
    }
    else if (KeepSeals(m_options.seals, std::move(seals))) {
        m_renderer.SetSeals(m_options.seals);
        PN_LOG_INFO("the seals were read again: {} looks", m_options.seals.looks.size());
    }
    // The animations reach the logic thread with the next view, which picks the frames; they are never part of its state.
    std::expected<core::Animations, core::Error> animations = data::LoadAnimations(m_options.dataFolder, *m_options.manifest);
    if (!animations) {
        PN_LOG_ERROR("the animations changed but weren't reloaded; the old animations stay: {}", animations.error().message);
    }
    else if (KeepAnimations(m_options.animations, std::move(animations))) {
        PN_LOG_INFO("the animations were read again: {} walks", m_options.animations->walks.size());
    }
    // The world's look is drawn only too, and checked against the map the game runs: a look the map can't use keeps the old.
    if (m_options.data.world) {
        std::expected<core::WorldLook, core::Error> look =
            data::LoadWorldLook(m_options.dataFolder, *m_options.manifest, *m_options.data.world);
        if (!look) {
            PN_LOG_ERROR("the world's look changed but wasn't reloaded; the old look stays: {}", look.error().message);
        }
        else if (KeepLook(m_options.worldLook, std::move(look))) {
            m_renderer.SetWorld(m_options.data.world, m_options.worldLook);
            PN_LOG_INFO("the world's look was read again: {} kinds", m_options.worldLook.kinds.size());
        }
    }
    std::expected<core::GameData, core::Error> gameData = data::LoadGameData(m_options.dataFolder);
    KeepWorld(m_options.data, gameData);
    if (gameData) {
        if (const std::expected<void, core::Error> runs = game::CheckScenes(*m_options.manifest, *gameData); !runs) {
            gameData = std::unexpected{ runs.error() };
        }
    }
    if (!gameData) {
        PN_LOG_ERROR("the game data changed but wasn't reloaded; the old data stays: {}", gameData.error().message);
    }
    const DataReload reload = ReloadOf(m_options.effects, effects, m_options.data, gameData);
    if (reload.effects) {
        PN_LOG_INFO("the effects changed: reloading them");
        ApplyEffects(*effects);
    }
    if (reload.gameData) {
        PN_LOG_INFO("the game data changed: reloading it");
        m_options.data = *gameData; // what `--cast-swords` reads the costs from
        m_renderer.SetNames(m_options.data.names);
        m_handover.SendInput(
            { .kind = core::ActionKind::ReloadData, .data = std::make_shared<const core::GameData>(*gameData) }
        );
    }
    else if (gameData) {
        PN_LOG_DEBUG("the game data is as it was: the simulation keeps running it");
    }
}

void App::ApplyEffects(const core::Effects& effects) {
    m_options.effects = effects;
    m_renderer.SetEffects(effects);
    m_hud.SetEffects(effects);
}

void App::HandleInput(const platform::WindowState& state) {
    PN_PROFILE_ZONE();
    if (platform::PausesGame(state) != m_paused) {
        m_paused = !m_paused;
        PN_LOG_INFO("{}: the window is {}", m_paused ? "paused" : "resumed", m_paused ? "unfocused or minimised" : "back");
        m_handover.SendInput({ .kind = m_paused ? core::ActionKind::Pause : core::ActionKind::Resume });
        if (m_paused && m_pathSeconds) {
            PN_LOG_WARNING("the game paused during the camera path: from here it measures no running game");
        }
    }
    if (m_paused) {
        // Maybe while nothing is drawn (minimised): the cursor's return across the edge mustn't pan. A Move press
        // waiting for its first drawn step would count the pause.
        m_camera.LoseFocus();
        m_moveLatency.Forget();
    }
    if (m_device.TakeCursor(m_window.Cursor())) {
        PN_LOG_INFO("prompts for {}", DeviceName(m_device.Used(), m_device.Family()));
    }
    const std::span actionControls{ ACTION_CONTROLS }; // a span's iterator is a class everywhere, unlike std::array's
    for (const platform::InputEvent& input : m_inputs) {
        NoteDevice(input);
        const std::optional<ControlEvent> control = m_bindings.Handle(input);
        if (!control || SteerCamera(*control) || !control->pressed) {
            continue;
        }
        if (control->control == Control::Quit) {
            m_quit = true;
        }
        else if (TakeDisplayToggle(control->control) || TakeCursorControl(control->control)) {
            continue;
        }
        else if (
            const std::optional<core::Action> pause =
                DateLineClick(control->control, input.device, control->position, m_hud.DateLineArea())
        ) {
            PN_LOG_INFO("the world's pause asked for by the date line");
            m_handover.SendInput(*pause);
        }
        else if (const core::Snapshot* drawn = m_display.Drawn(); SlotOf(control->control) && drawn != nullptr) {
            if (SelectsInstead(control->control, drawn->combat, control->alt)) {
                Click(state, control->position);
            }
            else {
                Cast(state, *control, input.device, drawn->combat);
            }
        }
        else if (
            const auto action = std::ranges::find(actionControls, control->control, &ActionControl::control);
            action != actionControls.end()
        ) {
            PN_LOG_INFO("{}", action->asked);
            m_handover.SendInput({ .kind = action->kind });
        }
    }
    if (m_paused) {
        m_drag.End(); // after this frame's presses too: a button's release may never come while paused
    }
    SteerCursor();
    SendHeld();
}

void App::SendHeld() {
    PN_PROFILE_ZONE();
    // The cursor takes the movement while it shows, so the cultivator stands.
    const core::Vec2 walk = m_cursor.Walk(m_bindings.Move());
    const core::HeldInput held{ .move = m_pathSeconds ? CameraPathMove(*m_pathSeconds) : walk };
    if (held == m_held) {
        return;
    }
    const core::Snapshot* drawn = m_display.Drawn();
    const auto pressed = [](core::Vec2 move) { return (move.x * move.x) + (move.y * move.y) > 0.0F; };
    if (m_pathSeconds && pressed(held.move) && !pressed(m_held.move) && drawn != nullptr && drawn->follow) {
        m_moveLatency.Pressed(platform::NowNs(), *drawn->follow);
    }
    m_held = held;
    m_handover.SendHeld(held);
}

bool App::SteerCamera(const ControlEvent& control) {
    if (!control.pressed) {
        if (m_drag.Release(control.control, control.position)) {
            m_camera.SnapBack();
        }
        return false;
    }
    const core::Snapshot* drawn = m_display.Drawn();
    if (DragsCamera(control.control, drawn == nullptr || drawn->combat)) {
        m_drag.Press(control.control, control.position);
        return true;
    }
    if (control.control == Control::Zoom) {
        m_camera.Zoom(control.value);
        return true;
    }
    if (control.control == Control::SnapBack) {
        m_camera.SnapBack();
        return true;
    }
    return false;
}

void App::NoteDevice(const platform::InputEvent& input) {
    if (m_device.Take(input)) {
        PN_LOG_INFO("prompts for {}", DeviceName(m_device.Used(), m_device.Family()));
    }
}

void App::Cast(const platform::WindowState& state, const ControlEvent& control, platform::Device device, bool combat) {
    const std::optional<core::Vec2> cursor =
        device == platform::Device::Mouse ? std::optional{ control.position } : m_window.Cursor();
    const std::optional<core::Vec2> point = cursor.transform([&](core::Vec2 window) {
        return WindowToWorld(m_camera.Camera(), window, state.width, state.height);
    });
    const std::optional<core::Action> cast = CastAction(control.control, control.pressed, combat, control.alt, point);
    if (!cast) {
        if (combat && !point) {
            PN_LOG_DEBUG("a cast sent nothing: the cursor isn't over the window");
        }
        return;
    }
    m_handover.SendInput(*cast);
    if (cast->slot == core::Slot::Special1) {
        m_swordsLatency.Pressed(platform::NowNs());
    }
}

void App::Click(const platform::WindowState& state, core::Vec2 position) {
    const core::Snapshot* drawn = m_display.Drawn();
    if (drawn == nullptr) {
        return;
    }
    // The picture the player clicked on: the drawn snapshot at the render clock, as the camera shows it.
    const f64 alpha = core::Alpha(drawn->scheduledNs, platform::NowNs(), m_delay.Current(), drawn->advanced);
    const core::Vec2 point = WindowToWorld(m_camera.Camera(), position, state.width, state.height);
    const core::StableId target = Pick(*drawn, alpha, point, m_fog.Frame());
    HideCursor();
    const core::Action action = ClickAction(drawn->worldMap, target, point);
    if (action.kind == core::ActionKind::WalkTo) {
        PN_LOG_DEBUG("clicked the ground at ({}, {}): walking there", point.x, point.y);
    }
    else {
        PN_LOG_DEBUG("clicked on entity {}", std::to_underlying(target));
    }
    m_handover.SendInput(action);
}

bool App::TakeDisplayToggle(Control control) {
    if (control == Control::ToggleDebugSight) {
        m_debugSight = !m_debugSight;
        PN_LOG_INFO("the debug sight {}", m_debugSight ? "on" : "off");
        return true;
    }
    if (control == Control::ToggleShowcase) {
        m_showcase = !m_showcase;
        PN_LOG_INFO("the showcase seals {}", m_showcase ? "shown" : "hidden");
        return true;
    }
    return false;
}

bool App::TakeCursorControl(Control control) {
    if (control == Control::TargetCursor) {
        ToggleCursor();
    }
    else if (control == Control::Confirm) {
        ConfirmCursor();
    }
    return control == Control::TargetCursor || control == Control::Confirm;
}

void App::ToggleCursor() {
    const core::Snapshot* drawn = m_display.Drawn();
    const core::WorldMap* map = m_options.data.world.get();
    if (m_cursor.Shown()) {
        HideCursor();
    }
    else if (drawn == nullptr || map == nullptr) {
        return;
    }
    else if (
        const std::optional<u32> tile = CursorStart(
            drawn->worldMap, drawn->follow.transform([](const core::Follow& follow) { return follow.current; }), *map
        )
    ) {
        m_cursor.Show(*tile, map->Columns(), map->Rows(), m_bindings.Move());
        PN_LOG_DEBUG("the target cursor shows on the cultivator's tile {}", *tile);
    }
}

void App::HideCursor() {
    if (!m_cursor.Shown()) {
        return;
    }
    m_cursor.Hide();
    m_renderer.SetTargetTile(std::nullopt);
    PN_LOG_DEBUG("the target cursor is hidden");
}

void App::ConfirmCursor() {
    const core::WorldMap* map = m_options.data.world.get();
    const core::Snapshot* drawn = m_display.Drawn();
    if (map == nullptr) {
        return;
    }
    if (drawn == nullptr || !drawn->worldMap) {
        HideCursor();
        return;
    }
    const std::optional<u32> tile = m_cursor.Tile();
    if (const std::optional<core::Action> action = m_cursor.Confirm(*map)) {
        m_renderer.SetTargetTile(std::nullopt);
        PN_LOG_DEBUG("the target cursor confirmed tile {}: walking there", tile.value_or(0));
        m_handover.SendInput(*action);
    }
}

void App::SteerCursor() {
    PN_PROFILE_ZONE();
    const u64 nowNs = platform::NowNs();
    const f32 seconds =
        static_cast<f32>(nowNs - std::min(nowNs, std::exchange(m_cursorNs, nowNs))) / static_cast<f32>(core::NS_PER_SECOND);
    if (!m_cursor.Shown()) {
        return;
    }
    const core::Snapshot* drawn = m_display.Drawn();
    const core::WorldMap* map = m_options.data.world.get();
    if (drawn == nullptr || !drawn->worldMap || map == nullptr) {
        HideCursor();
        return;
    }
    m_cursor.Steer(m_bindings.Move(), seconds);
    m_renderer.SetTargetTile(map->TileCentre(m_cursor.Tile().value_or(0)));
}

std::optional<std::size_t> App::Hovered(const platform::WindowState& state, const core::Snapshot& drawn, f64 alpha) const {
    PN_PROFILE_ZONE();
    const std::optional<core::Vec2> cursor = m_window.Cursor();
    if (!cursor || m_device.Used() == platform::Device::Gamepad) {
        return std::nullopt; // a gamepad has no cursor to hover with
    }
    return PickInstance(drawn, alpha, WindowToWorld(m_camera.Camera(), *cursor, state.width, state.height), m_fog.Frame());
}

void App::MoveCamera(const platform::WindowState& state, const core::Snapshot& drawn, f64 alpha) {
    PN_PROFILE_ZONE();
    const u64 nowNs = platform::NowNs();
    const f32 seconds =
        static_cast<f32>(nowNs - std::min(nowNs, std::exchange(m_cameraNs, nowNs))) / static_cast<f32>(core::NS_PER_SECOND);
    const std::optional<core::Vec2> pointer = m_window.Cursor();
    const std::optional<core::Vec2> dragFrom = m_drag.Take(pointer, state.focused, state.width, state.height);
    // A gamepad has no cursor: a mouse left resting at the window's edge doesn't pan while one is in use.
    const std::optional<core::Vec2> cursor = dragFrom || m_device.Used() != platform::Device::Gamepad ? pointer : std::nullopt;
    const bool wasFree = m_camera.Free();
    m_camera.Update(
        {
            .kind = drawn.camera,
            .change = core::ShownChange(drawn.sceneChange),
            .followed = drawn.follow.transform([alpha](const core::Follow& follow) { return FollowedCentre(follow, alpha); }),
            .move = m_held.move,
            .pan = m_bindings.Pan(),
            .cursor = cursor,
            .dragFrom = dragFrom,
            .focused = state.focused,
            .edgePan = m_options.settings.edgePan,
            .width = state.width,
            .height = state.height,
            .seconds = seconds,
            .still = HoldsCamera(&drawn),
            .path = m_pathSeconds,
        }
    );
    if (const u64 change = core::ShownChange(drawn.sceneChange); change != m_cameraChange) {
        m_cameraChange = change;
        const bool follows = drawn.follow && drawn.camera != core::CameraKind::Pan;
        PN_LOG_DEBUG("the camera starts scene change {} {}", change, follows ? "following the player" : "at the scene's view");
    }
    else if (m_camera.Free() != wasFree) {
        PN_LOG_DEBUG("the camera {}", m_camera.Free() ? "was panned free of the player" : "snapped back to the player");
    }
}

std::expected<void, Failure> App::LoadScene(core::SceneId wanted) {
    PN_PROFILE_ZONE();
    if (wanted == m_reported) {
        return {};
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
        return std::unexpected{ Failure{ .what = std::format("the scene {} couldn't be loaded", name),
                                         .error = held.error() } };
    }
    if (*held) {
        m_loading.reset();
        m_reported = scene;
        m_handover.SendInput({ .kind = core::ActionKind::SceneResident, .scene = scene });
        PN_LOG_INFO("scene {} resident in {} ms", name, (platform::NowNs() - m_loadStartNs) / core::NS_PER_MS);
    }
    return {};
}

std::expected<void, Failure> App::Present(const platform::WindowState& state) {
    PN_PROFILE_ZONE();
    std::optional<Failure> sceneFailure = std::nullopt;
    // PresentFrame drains the events every iteration, drawing or not, so the queue never grows.
    const bool presented = core::PresentFrame(
        m_display,
        m_handover,
        m_delay,
        m_pending,
        state.visible,
        [this] { m_renderer.Acquire(); },
        platform::NowNs,
        // A scene's upload step can hold the acquired image for LOAD_BUDGET_NS before the draw; that delays nothing that
        // moves, as the logic runs no tick until the scene is resident.
        [this, &sceneFailure](const core::Snapshot& drawn) {
            if (std::expected<void, Failure> loaded = LoadScene(drawn.scene); !loaded) {
                sceneFailure = std::move(loaded.error());
                return false;
            }
            // Nothing draws the ways out of and into a scene yet: each is reported played as soon as it shows.
            if (const std::optional<core::Action> report = m_sceneReports.Answer(drawn.sceneChange)) {
                m_handover.SendInput(*report);
            }
            if (m_numbersEpoch != drawn.epoch) {
                m_numbers.Clear(); // a new timeline: none of the old one's hits happened in it
                m_hud.Clear();     // nor its refused casts
                m_numbersEpoch = drawn.epoch;
            }
            return true;
        },
        // Audio and particles will play the events too.
        [this](const core::Event& event) {
            m_numbers.Play(event);
            m_hud.Play(event, static_cast<f64>(event.tick));
        },
        [this, &state](const core::Snapshot& drawn, f64 alpha) {
            MoveCamera(state, drawn, alpha);
            // Only drawn frames send one: hidden, the logic keeps the last view, and the first frame back sends a fresh one.
            m_handover.SendView(
                { .area = CullArea(m_camera.Camera(), state.width, state.height),
                  .change = core::ShownChange(drawn.sceneChange),
                  .debugSight = m_debugSight,
                  .animations = m_options.animations }
            );
            const f64 renderTick = core::RenderTick(drawn.simulationTick, alpha);
            m_numberTexts.clear();
            m_numbers.Texts(renderTick, m_numberTexts);
            m_hudLines.clear();
            m_hudBars.clear();
            if (drawn.caster) {
                // The window isn't made high-density, so its size in window coordinates is its size in pixels.
                m_hud.Build(*drawn.caster, renderTick, static_cast<u32>(std::max(state.height, 0)), m_hudLines, m_hudBars);
            }
            m_hud.BuildDate(
                drawn.calendar,
                static_cast<u32>(std::max(state.width, 0)),
                [this](std::string_view text) { return m_renderer.HudLineWidth(text); },
                m_hudLines
            );
            // Labels and the fog on the frame clock, which runs while the game is paused; numbers on the render clock, which
            // holds.
            const f64 frameSeconds = m_frameClock.Advance(platform::NowNs());
            if (const core::WorldMap* map = m_options.data.world.get(); map != nullptr) {
                m_fog.Update(drawn, *map, static_cast<f32>(frameSeconds - m_frameSeconds), m_options.worldLook.fog.fadeSeconds);
            }
            m_frameSeconds = frameSeconds;
            gfx::View view = FrameView(m_camera.Camera(), alpha, Hovered(state, drawn, alpha), frameSeconds);
            view.showcase = m_showcase;
            view.fog = m_fog.Frame();
            m_renderer.Draw(drawn, view, m_jobs, m_numberTexts, { .lines = m_hudLines, .bars = m_hudBars });
            MeasureLatency(drawn, alpha);
            PN_PROFILE_FRAME();
        }
    );
    const core::Snapshot* drawn = m_display.Drawn();
    if (!presented) {
        // The frame stops only when the scene can't be loaded, which sets the failure.
        PN_ASSERT(sceneFailure.has_value(), "a frame stopped without a failure");
        return std::unexpected{ std::move(*sceneFailure) }; // NOLINT(bugprone-unchecked-optional-access): asserted above
    }
    if (drawn == nullptr) {
        return {};
    }
    if (!state.visible) {
        // Hidden, numbers still age out, so the pool doesn't grow while nothing is drawn: by the earliest the render
        // clock can be at for this snapshot, which is near enough for numbers no one sees.
        m_numbers.Forget(core::RenderTick(drawn->simulationTick, 0.0));
    }
    if (drawn->transition) {
        // Nothing draws the transition itself yet.
        platform::WaitUntilNs(platform::NowNs() + TRANSITION_FRAME_NS);
    }
    return {};
}

} // namespace app
