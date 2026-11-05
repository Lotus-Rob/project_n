// The headless benchmark (design doc 0002 §16): ticks a large world without a window or a GPU and
// reports tick and publish times. Phase 3's world is the stress arena (about 100,000 instances,
// §8.1), published through the handover each tick; phase 4 adds the real systems and fails the run
// over its budgets. Run it optimised: `pixi run project_n test --mode release --target headless_bench`.
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/log.hpp"
#include "core/math.hpp"
#include "game/arena.hpp"

namespace {

constexpr u64 TICKS = 600;

f64 MsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void Report(std::string_view what, std::vector<f64>& times, std::size_t instances) {
    std::ranges::sort(times);
    const f64 mean = std::accumulate(times.begin(), times.end(), 0.0) / static_cast<f64>(times.size());
    const f64 p99 = times[(times.size() * 99) / 100];
    PN_LOG_INFO(
        "{} of {} instances, {} times: mean {:.3f} ms, 99th percentile {:.3f} ms", what, instances, times.size(), mean, p99
    );
}

// Ticks and publishes the stress arena on a job system of `workers`, as core::Logic does: the
// buffers are cleared, keeping their capacity.
void Measure(u32 workers) {
    core::Jobs jobs{ workers };
    game::Arena arena{ game::Scene::Stress };
    core::GameHandover handover;
    std::vector<core::Event> events;
    std::vector<f64> tickMs;
    std::vector<f64> publishMs;
    tickMs.reserve(TICKS);
    publishMs.reserve(TICKS);
    std::size_t instances = 0;
    for (u64 tick = 1; tick <= TICKS; ++tick) {
        const auto start = std::chrono::steady_clock::now();
        arena.Tick(tick);
        tickMs.push_back(MsSince(start));

        core::Snapshot& snapshot = handover.Writing();
        snapshot.simulationTick = tick;
        snapshot.instances.clear();
        snapshot.batches.clear();
        const auto publishStart = std::chrono::steady_clock::now();
        arena.Publish(snapshot, jobs);
        publishMs.push_back(MsSince(publishStart));
        instances = snapshot.instances.size();
        handover.Publish(events);
    }
    PN_LOG_INFO("with {} workers:", workers);
    Report("ticks", tickMs, instances);
    Report("publishes", publishMs, instances);
}

} // namespace

int main(int /*argc*/, char** /*argv*/) {
    core::math::SetFloatingPointEnvironment();
    Measure(1);
    Measure(core::Jobs::DefaultWorkers());
    return 0;
}
