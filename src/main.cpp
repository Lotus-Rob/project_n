#include "core/command_line.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/log.hpp"
#include "core/logic.hpp"
#include "core/math.hpp"
#include "core/profile.hpp"
#include "core/tick_clock.hpp"
#include "game/arena.hpp"
#include "gfx/renderer.hpp"
#include "platform/clock.hpp"
#include "platform/paths.hpp"
#include "platform/sdl_log.hpp"
#include "platform/window.hpp"

namespace {

constexpr i32 WINDOW_WIDTH = 1280;
constexpr i32 WINDOW_HEIGHT = 720;
// Frames during a calendar step's transition: few, leaving the cores to the step (design doc 0002 §7.3).
constexpr u64 TRANSITION_FRAME_NS = core::NS_PER_SECOND / 20;
// The logic thread's loop (design doc 0002 §6): runs the ticks due, then publishes the burst's last.
void Tick(
    const std::stop_token& stop,
    core::GameHandover& handover,
    u64 startNs,
    game::Scene scene,
    const std::filesystem::path& saveFile
) {
    core::math::SetFloatingPointEnvironment();
    PN_LOG_INFO("logic thread ticking {} times a second", core::TICKS_PER_SECOND);
    core::TickClock clock{ startNs };
    core::Logic logic{ saveFile };
    {
        core::Jobs jobs;
        PN_LOG_INFO("job system running {} workers", jobs.Workers());
        game::Arena arena{ scene };
        bool overloaded = false;
        while (!stop.stop_requested()) {
            platform::WaitUntilNs(clock.NextNs());
            PN_PROFILE_ZONE();
            const u64 now = platform::NowNs();
            const core::TickBurst burst = clock.Advance(now);
            if (burst.droppedTicks > 0) {
                PN_LOG_DEBUG("{} ticks dropped after a hitch: the game slowed down", burst.droppedTicks);
            }
            if (clock.Overloaded(now) != overloaded) {
                overloaded = !overloaded;
                if (overloaded) {
                    PN_LOG_WARNING("ticks have run late for over a second: the machine can't keep up");
                }
                else {
                    PN_LOG_INFO("ticks are on time again");
                }
            }
            if (logic.RunBurst(burst, handover, arena, jobs, platform::NowNs, stop)) {
                const u64 stalledNs = platform::NowNs();
                PN_LOG_INFO(
                    "an expected stall (a month, a save or a load) took {} ms",
                    (stalledNs - now) / (core::NS_PER_SECOND / 1'000)
                );
                clock.Restart(stalledNs);
            }
        }
    } // the workers join here: a hang at shutdown shows as no "stopped" line
    PN_LOG_INFO("job system stopped");
    const core::TickCounters& counters = logic.Counters();
    PN_LOG_INFO("logic thread stopped after {} ticks, {} simulated", counters.wall, counters.simulation);
}

// The logic thread's entry: an exception that escapes is logged and stops the game (§12).
void RunLogic(
    const std::stop_token& stop,
    core::GameHandover& handover,
    u64 startNs,
    game::Scene scene,
    const std::filesystem::path& saveFile,
    std::atomic<bool>& failed
) {
    try {
        Tick(stop, handover, startNs, scene, saveFile);
    }
    catch (const std::exception& e) {
        PN_LOG_CRITICAL("logic thread failed: {}", e.what());
        failed = true;
    }
    catch (...) {
        PN_LOG_CRITICAL("logic thread failed: an exception of unknown type");
        failed = true;
    }
}

struct SceneChoice {
    game::Scene scene = game::Scene::Arena;
    std::string_view name = "the arena";
};

SceneChoice ChooseScene(std::span<char*> args) {
    if (std::ranges::contains(args, std::string_view{ "--painted" })) {
        return { .scene = game::Scene::Painted, .name = "the painted test scene" };
    }
    if (std::ranges::contains(args, std::string_view{ "--cells" })) {
        return { .scene = game::Scene::Cells, .name = "the map cell grid" };
    }
    if (std::ranges::contains(args, std::string_view{ "--stress" })) {
        return { .scene = game::Scene::Stress, .name = "the stress arena" };
    }
    return {};
}

// `--quit-after SECONDS`: quit by itself, as on a quit request, so `project_n profile` records with no click into a
// window that would take the focus. 0: never.
std::expected<u64, core::Error> QuitAfterNs(std::span<char*> args) {
    const auto flag = std::ranges::find(args, std::string_view{ "--quit-after" });
    if (flag == args.end()) {
        return 0;
    }
    return core::SecondsToNs(std::next(flag) == args.end() ? std::string_view{} : std::string_view{ *std::next(flag) })
        .transform_error([](const core::Error& error) { return core::Error{ "--quit-after: " + error.message }; });
}

// A start-up failure: reported (logged and shown), and the game's exit status.
i32 FailToStart(std::string_view what, const core::Error& error) {
    platform::ReportStartFailure(what, error);
    return 1;
}

// Turns what the player did in the window into the logic's input. `paused` is whether the window pauses the game, as
// last sent.
void SendInput(const platform::WindowState& state, core::GameHandover& handover, bool& paused) {
    if (platform::PausesGame(state) != paused) {
        paused = !paused;
        PN_LOG_INFO("{}: the window is {}", paused ? "paused" : "resumed", paused ? "unfocused or minimised" : "back");
        handover.SendInput({ .kind = paused ? core::ActionKind::Pause : core::ActionKind::Resume });
    }
    if (state.saveRequested) {
        PN_LOG_INFO("a save asked for");
        handover.SendInput({ .kind = core::ActionKind::Save });
    }
    if (state.loadRequested) {
        PN_LOG_INFO("a load asked for");
        handover.SendInput({ .kind = core::ActionKind::Load });
    }
    if (state.monthRequested) {
        PN_LOG_INFO("a month asked to pass");
        handover.SendInput({ .kind = core::ActionKind::AdvanceCalendar });
    }
}

i32 Run(game::Scene scene, u64 quitAfterNs, const std::filesystem::path& saveFile) {
    platform::RouteSdlLogs();
    std::expected<platform::Window, core::Error> window = platform::Window::Open("project_n", WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!window) {
        return FailToStart("the window couldn't be opened", window.error());
    }
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    if (!assets) {
        return FailToStart("the game's files weren't found", assets.error());
    }
    std::expected<gfx::Renderer, core::Error> renderer = gfx::Renderer::Create(*window, *assets);
    if (!renderer) {
        return FailToStart("the graphics card couldn't be used", renderer.error());
    }

    core::GameHandover handover;
    core::GameDisplay display;
    std::atomic<bool> logicFailed = false;
    const u64 startNs = platform::NowNs();
    std::jthread logic{ RunLogic, std::ref(handover), startNs, scene, saveFile, std::ref(logicFailed) };
    bool paused = false;
    gfx::Camera camera{}; // shortcut: a fixed view, but for the cell grid's zoom; the camera follows input and the snapshot
                          // from phase 5
    std::vector<core::Event> incoming;
    std::vector<core::Event> pending; // taken, waiting for the render clock
    for (;;) {
        PN_PROFILE_ZONE();
        const platform::WindowState& state = window->Poll(core::TICK_NS);
        if (state.quitRequested) {
            PN_LOG_DEBUG("quit requested");
            break;
        }
        if (quitAfterNs != 0 && platform::NowNs() - startNs >= quitAfterNs) {
            PN_LOG_INFO("quitting after {} seconds, as --quit-after asked", quitAfterNs / core::NS_PER_SECOND);
            break;
        }
        if (logicFailed) {
            return 1;
        }
        if (scene == game::Scene::Cells) {
            camera.zoom = gfx::ZoomedBy(camera.zoom, state.wheelNotches);
        }
        SendInput(state, handover, paused);
        // Events are drained every iteration, drawing or not, so the queue never grows (§5).
        handover.TakeEvents(incoming);
        pending.insert(pending.end(), incoming.begin(), incoming.end());
        static_cast<void>(display.Update(handover, platform::NowNs(), core::DEFAULT_DISPLAY_DELAY_NS));
        const core::Snapshot* drawn = display.Drawn();
        if (drawn == nullptr) {
            continue;
        }
        core::PresentFrame(
            *drawn,
            pending,
            state.visible,
            [&renderer] { renderer->Acquire(); },
            platform::NowNs,
            // shortcut: nothing plays events yet; audio and particles (phase 7) replace the empty player.
            [](const core::Event& /*event*/) {},
            [&renderer, drawn, &camera](f64 alpha) {
                renderer->Draw(*drawn, alpha, camera);
                PN_PROFILE_FRAME();
            }
        );
        if (drawn->transition) {
            // shortcut: nothing draws the transition itself yet; its view model and screen come with the UI (phase 6).
            platform::WaitUntilNs(platform::NowNs() + TRANSITION_FRAME_NS);
        }
    }

    PN_LOG_INFO("shutting down; {} snapshots were published after they were due", display.LateAdoptions());
    logic.request_stop();
    logic.join();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::expected<std::filesystem::path, core::Error> userData = platform::UserDataDirectory();
    core::log::Start(userData ? *userData / "logs" : std::filesystem::path{});
    if (!userData) {
        PN_LOG_WARNING("logs go to the console only: {}", userData.error().message);
    }
    PN_LOG_NOTICE("project_n starting");
#ifdef __clang__
#pragma clang diagnostic push
// The C runtime hands the arguments over as a pointer and a count: nothing can check them.
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
    const std::span<char*> args{ argv, static_cast<std::size_t>(argc) };
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    const SceneChoice choice = ChooseScene(args);
    PN_LOG_INFO("scene: {}", choice.name);
    const std::expected<u64, core::Error> quitAfterNs = QuitAfterNs(args);
    if (!quitAfterNs) {
        PN_LOG_ERROR("{}", quitAfterNs.error().message);
        return 1;
    }
    try {
        // shortcut: one save slot; slots and their screen come with the UI (phase 6).
        const i32 status =
            Run(choice.scene, *quitAfterNs, userData ? *userData / "saves" / "quicksave.pns" : std::filesystem::path{});
        PN_LOG_NOTICE("project_n stopped with status {}", status);
        return status;
    }
    catch (const std::exception& e) {
        PN_LOG_CRITICAL("fatal: {}", e.what());
        return 1;
    }
    catch (...) {
        PN_LOG_CRITICAL("fatal: an exception of unknown type");
        return 1;
    }
}
