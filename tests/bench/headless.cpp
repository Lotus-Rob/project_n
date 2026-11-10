// The headless benchmark (design doc 0002 §16): ticks a large world without a window or a GPU, reports tick, publish
// and month-step times, and fails over the phase 4 gates, measured on the minimum spec's 2 workers plus this thread:
// the combat tick (the stress arena's horde and 10,000 bullets, publishing 100,000 instances) inside 8 ms (§7.3), and
// a month step for 50,000 world NPCs inside 0.5 s (§7.7). Run it optimised:
// `pixi run project_n test --mode release --target headless_bench`. On a machine faster than the minimum spec a pass
// shows only that nothing grew out of all proportion.
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/log.hpp"
#include "core/math.hpp"
#include "game/arena.hpp"
#include "support/manifest.hpp"

namespace {

constexpr u64 TICKS = 600;
constexpr u64 MONTHS = 12;          // NPCs' choices change over the first months; meetings start after several
constexpr u32 MIN_SPEC_WORKERS = 2; // design doc 0002 §13, on whatever CPU this runs
constexpr f64 COMBAT_TICK_BUDGET_MS = 8.0;
constexpr f64 MONTH_STEP_BUDGET_MS = 500.0;

f64 MsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// Logs the mean and 99th percentile of `times`, and returns the 99th percentile.
f64 Report(std::string_view what, std::vector<f64>& times, std::size_t items) {
    std::ranges::sort(times);
    const f64 mean = std::accumulate(times.begin(), times.end(), 0.0) / static_cast<f64>(times.size());
    const f64 p99 = times[(times.size() * 99) / 100];
    PN_LOG_INFO("{} of {}, {} times: mean {:.3f} ms, 99th percentile {:.3f} ms", what, items, times.size(), mean, p99);
    return p99;
}

struct Worst {
    f64 combatTickMs = 0.0; // the 99th percentile of a tick and its publish
    f64 monthStepMs = 0.0;  // the slowest month
};

// Ticks and publishes the stress arena on a job system of `workers`, as core::Logic does (the buffers are cleared,
// keeping their capacity), then lets months pass for its world NPCs.
Worst Measure(u32 workers) {
    core::Jobs jobs{ workers };
    game::Arena arena(test::TestManifest(), game::Scene::Stress);
    core::GameHandover handover;
    std::vector<core::Event> events;
    std::vector<f64> tickMs;
    std::vector<f64> publishMs;
    std::vector<f64> combatMs;
    tickMs.reserve(TICKS);
    publishMs.reserve(TICKS);
    combatMs.reserve(TICKS);
    std::size_t instances = 0;
    for (u64 tick = 1; tick <= TICKS; ++tick) {
        const auto start = std::chrono::steady_clock::now();
        events.clear();
        arena.Tick({ .wall = tick, .simulation = tick, .state = tick }, {}, events);
        tickMs.push_back(MsSince(start));

        core::Snapshot& snapshot = handover.Writing();
        snapshot.simulationTick = tick;
        snapshot.instances.clear();
        snapshot.batches.clear();
        const auto publishStart = std::chrono::steady_clock::now();
        arena.Publish(snapshot, jobs);
        publishMs.push_back(MsSince(publishStart));
        combatMs.push_back(MsSince(start));
        instances = snapshot.instances.size();
        handover.Publish(events);
    }

    std::vector<f64> monthMs;
    for (u64 month = 1; month <= MONTHS; ++month) {
        const auto start = std::chrono::steady_clock::now();
        arena.AdvanceCalendar({ .wall = TICKS + month, .simulation = TICKS, .state = TICKS + month }, jobs, {});
        monthMs.push_back(MsSince(start));
    }
    const std::size_t npcs = arena.State()[game::WORLD].Entities().storage<game::Npc>()->size();

    PN_LOG_INFO("with {} workers:", workers);
    Report("ticks", tickMs, instances);
    Report("publishes", publishMs, instances);
    const f64 combat = Report("combat ticks (tick and publish)", combatMs, instances);
    Report("month steps", monthMs, npcs);
    return { .combatTickMs = combat, .monthStepMs = monthMs.back() }; // sorted by Report: the slowest
}

} // namespace

int main(int /*argc*/, char** /*argv*/) {
    core::math::SetFloatingPointEnvironment();
    static_cast<void>(Measure(1));
    const Worst minSpec = Measure(MIN_SPEC_WORKERS);
    static_cast<void>(Measure(core::Jobs::DefaultWorkers()));

    bool passed = true;
    if (minSpec.combatTickMs > COMBAT_TICK_BUDGET_MS) {
        PN_LOG_ERROR(
            "the combat tick took {:.3f} ms on {} workers, over its {} ms budget",
            minSpec.combatTickMs,
            MIN_SPEC_WORKERS,
            COMBAT_TICK_BUDGET_MS
        );
        passed = false;
    }
    if (minSpec.monthStepMs > MONTH_STEP_BUDGET_MS) {
        PN_LOG_ERROR(
            "a month step took {:.1f} ms on {} workers, over its {} ms budget",
            minSpec.monthStepMs,
            MIN_SPEC_WORKERS,
            MONTH_STEP_BUDGET_MS
        );
        passed = false;
    }
    if (passed) {
        PN_LOG_INFO(
            "within the phase 4 budgets on {} workers: combat tick {:.3f} ms of {} ms, month step {:.1f} ms of {} ms",
            MIN_SPEC_WORKERS,
            minSpec.combatTickMs,
            COMBAT_TICK_BUDGET_MS,
            minSpec.monthStepMs,
            MONTH_STEP_BUDGET_MS
        );
    }
    return passed ? 0 : 1;
}
