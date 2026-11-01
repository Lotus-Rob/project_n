#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/log.hpp"
#include "core/logic.hpp"
#include "core/math.hpp"
#include "core/profile.hpp"
#include "core/tick_clock.hpp"
#include "platform/clock.hpp"
#include "platform/paths.hpp"
#include "platform/sdl_log.hpp"
#include "platform/window.hpp"

namespace {

constexpr i32 WINDOW_WIDTH = 1280;
constexpr i32 WINDOW_HEIGHT = 720;
// The clear colour breathes between these over BREATH_TICKS of the render clock: until there is
// anything to draw, it shows the snapshots arriving, interpolating, and freezing while paused.
constexpr platform::Colour DARK{ .red = 0.08F, .green = 0.10F, .blue = 0.14F };
constexpr platform::Colour LIGHT{ .red = 0.14F, .green = 0.17F, .blue = 0.23F };
constexpr f64 BREATH_TICKS = 4.0 * core::TICKS_PER_SECOND;

platform::Colour Breathe(f64 renderTick) {
    PN_PROFILE_ZONE();
    // fmod keeps Cos's argument in its range however long the game runs.
    const f64 phase = std::fmod(renderTick, BREATH_TICKS) * 2.0 * std::numbers::pi / BREATH_TICKS;
    const auto amount = static_cast<f32>(0.5 - (0.5 * core::math::Cos(phase)));
    const auto mix = [amount](f32 from, f32 into) { return from + ((into - from) * amount); };
    return { .red = mix(DARK.red, LIGHT.red), .green = mix(DARK.green, LIGHT.green), .blue = mix(DARK.blue, LIGHT.blue) };
}

// The logic thread's loop (design doc 0002 §6): runs the ticks due, then publishes the burst's last.
void Tick(const std::stop_token& stop, core::GameHandover& handover, u64 startNs) {
    core::math::SetFloatingPointEnvironment();
    PN_LOG_INFO("logic thread ticking {} times a second", core::TICKS_PER_SECOND);
    core::TickClock clock{ startNs };
    core::Logic logic;
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
        logic.RunBurst(burst, handover, platform::NowNs);
    }
    const core::TickCounters& counters = logic.Counters();
    PN_LOG_INFO("logic thread stopped after {} ticks, {} simulated", counters.wall, counters.simulation);
}

// The logic thread's entry: an exception that escapes is logged and stops the game (§12).
void RunLogic(const std::stop_token& stop, core::GameHandover& handover, u64 startNs, std::atomic<bool>& failed) {
    try {
        Tick(stop, handover, startNs);
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

i32 Run() {
    platform::RouteSdlLogs();
    std::expected<platform::Window, core::Error> window = platform::Window::Open("project_n", WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!window) {
        PN_LOG_ERROR("no window: {}", window.error().message);
        return 1;
    }

    core::GameHandover handover;
    core::GameDisplay display;
    std::atomic<bool> logicFailed = false;
    std::jthread logic{ RunLogic, std::ref(handover), platform::NowNs(), std::ref(logicFailed) };
    bool focused = true;
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
        window->Clear(Breathe(renderTick));
        PN_PROFILE_FRAME();
    }

    PN_LOG_INFO("shutting down; {} snapshots were published after they were due", display.LateAdoptions());
    logic.request_stop();
    logic.join();
    return 0;
}

} // namespace

int main(int /*argc*/, char** /*argv*/) {
    const std::expected<std::filesystem::path, core::Error> userData = platform::UserDataDirectory();
    core::log::Start(userData ? *userData / "logs" : std::filesystem::path{});
    if (!userData) {
        PN_LOG_WARNING("logs go to the console only: {}", userData.error().message);
    }
    PN_LOG_NOTICE("project_n starting");
    try {
        const i32 status = Run();
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
