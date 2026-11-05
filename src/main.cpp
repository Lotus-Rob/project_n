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
// The logic thread's loop (design doc 0002 §6): runs the ticks due, then publishes the burst's last.
void Tick(const std::stop_token& stop, core::GameHandover& handover, u64 startNs, game::Scene scene) {
    core::math::SetFloatingPointEnvironment();
    PN_LOG_INFO("logic thread ticking {} times a second", core::TICKS_PER_SECOND);
    core::TickClock clock{ startNs };
    core::Logic logic;
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
            logic.RunBurst(burst, handover, arena, jobs, platform::NowNs);
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
    std::atomic<bool>& failed
) {
    try {
        Tick(stop, handover, startNs, scene);
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

i32 Run(game::Scene scene) {
    platform::RouteSdlLogs();
    std::expected<platform::Window, core::Error> window = platform::Window::Open("project_n", WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!window) {
        PN_LOG_ERROR("no window: {}", window.error().message);
        return 1;
    }
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    if (!assets) {
        PN_LOG_ERROR("no assets: {}", assets.error().message);
        return 1;
    }
    std::expected<gfx::Renderer, core::Error> renderer = gfx::Renderer::Create(*window, *assets);
    if (!renderer) {
        PN_LOG_ERROR("no renderer: {}", renderer.error().message);
        return 1;
    }

    core::GameHandover handover;
    core::GameDisplay display;
    std::atomic<bool> logicFailed = false;
    std::jthread logic{ RunLogic, std::ref(handover), platform::NowNs(), scene, std::ref(logicFailed) };
    bool focused = true;
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
        if (logicFailed) {
            return 1;
        }
        if (scene == game::Scene::Cells) {
            camera.zoom = gfx::ZoomedBy(camera.zoom, state.wheelNotches);
        }
        if (state.focused != focused) {
            focused = state.focused;
            PN_LOG_INFO("{}: the window {} focus", focused ? "resumed" : "paused", focused ? "has" : "lost");
            handover.SendInput({ .kind = focused ? core::ActionKind::Resume : core::ActionKind::Pause });
        }
        // Events are drained every iteration, drawing or not, so the queue never grows (§5).
        handover.TakeEvents(incoming);
        pending.insert(pending.end(), incoming.begin(), incoming.end());
        static_cast<void>(display.Update(handover, platform::NowNs(), core::DEFAULT_DISPLAY_DELAY_NS));
        const core::Snapshot* drawn = display.Drawn();
        if (drawn == nullptr) {
            continue;
        }
        const f64 alpha = core::Alpha(drawn->scheduledNs, platform::NowNs(), core::DEFAULT_DISPLAY_DELAY_NS, drawn->advanced);
        const f64 renderTick = core::RenderTick(drawn->simulationTick, alpha);
        // shortcut: nothing plays events yet; audio and particles (phase 7) replace the empty player.
        core::PlayDue(pending, drawn->epoch, renderTick, [](const core::Event& /*event*/) {});
        if (!state.visible) {
            continue;
        }
        renderer->Draw(*drawn, alpha, camera);
        PN_PROFILE_FRAME();
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
    try {
        const i32 status = Run(choice.scene);
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
