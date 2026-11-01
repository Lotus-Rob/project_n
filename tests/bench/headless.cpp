// The headless benchmark (design doc 0002 §16): ticks a large world without a window or a GPU and
// reports tick times. Phase 1's world is entities moving in a domain, published through the
// handover each tick; phase 4 adds the real systems and fails the run over its budgets. Run it
// optimised: `pixi run project_n test --mode release --target headless_bench`.
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/ecs.hpp"
#include "core/frame.hpp"
#include "core/log.hpp"
#include "core/math.hpp"
#include "core/tick_clock.hpp"

namespace {

constexpr u64 ENTITIES = 100'000;
constexpr u64 TICKS = 600;
constexpr f32 STEP = 1.0F / core::TICKS_PER_SECOND;

struct Position {
    f32 x = 0.0F;
    f32 y = 0.0F;
};

struct Velocity {
    f32 dx = 0.0F;
    f32 dy = 0.0F;
};

} // namespace

int main(int /*argc*/, char** /*argv*/) {
    core::math::SetFloatingPointEnvironment();
    core::Domain world;
    core::StableIdCounter ids;
    for (u64 i = 0; i < ENTITIES; ++i) {
        const core::Entity entity = world.Create(ids.Allocate());
        const auto angle = static_cast<f32>(i);
        world.Entities().emplace<Position>(entity);
        world.Entities().emplace<Velocity>(entity, core::math::Cos(angle), core::math::Sin(angle));
    }
    world.SortCanonical();

    core::GameHandover handover;
    core::TickCounters counters;
    std::vector<core::Event> events;
    std::vector<f64> tickMs;
    tickMs.reserve(TICKS);
    for (u64 tick = 0; tick < TICKS; ++tick) {
        const auto start = std::chrono::steady_clock::now();
        counters.Count(false, false);
        for (auto [entity, position, velocity] : world.Entities().view<Position, const Velocity>().each()) {
            position.x += velocity.dx * STEP;
            position.y += velocity.dy * STEP;
        }
        handover.Writing() = { .wallTick = counters.wall, .simulationTick = counters.simulation, .advanced = true };
        handover.Publish(events);
        tickMs.push_back(std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count());
    }

    std::ranges::sort(tickMs);
    const f64 mean = std::accumulate(tickMs.begin(), tickMs.end(), 0.0) / static_cast<f64>(TICKS);
    const f64 p99 = tickMs[(TICKS * 99) / 100];
    PN_LOG_INFO("{} ticks of {} entities: mean {:.3f} ms, 99th percentile {:.3f} ms", TICKS, ENTITIES, mean, p99);
    return 0;
}
