#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/jobs.hpp"
#include "core/state.hpp"
#include "game/arena.hpp"
#include "game/npcs.hpp"

#include <doctest/doctest.h>

namespace {

using core::atlas::SpriteId;

core::Snapshot Published(game::Arena& arena) {
    core::Jobs jobs{ 1 };
    core::Snapshot snapshot;
    arena.Publish(snapshot, jobs);
    return snapshot;
}

// Whether an instance draws `sprite`: its region starts where the sprite's does.
bool Draws(const core::Instance& instance, SpriteId sprite) {
    return instance.regionMin == core::MakeInstance({ .sprite = sprite }).regionMin;
}

TEST_CASE("the ground is drawn first") {
    game::Arena arena;
    const core::Snapshot snapshot = Published(arena);
    REQUIRE_FALSE(snapshot.instances.empty());
    CHECK(Draws(snapshot.instances.front(), SpriteId::Ground));
}

TEST_CASE("shadows are drawn before what casts them") {
    game::Arena arena;
    const core::Snapshot snapshot = Published(arena);
    const auto firstShadow =
        std::ranges::find_if(snapshot.instances, [](const auto& instance) { return Draws(instance, SpriteId::Shadow); });
    const auto firstFox =
        std::ranges::find_if(snapshot.instances, [](const auto& instance) { return Draws(instance, SpriteId::Fox); });
    REQUIRE(firstShadow != snapshot.instances.end());
    REQUIRE(firstFox != snapshot.instances.end());
    CHECK(firstShadow < firstFox);
}

// The positions of every instance, sorted, so two snapshots compare whatever order they drew in.
std::vector<std::pair<f32, f32>> Positions(const core::Snapshot& snapshot, core::Vec2 core::Instance::* which) {
    std::vector<std::pair<f32, f32>> positions;
    positions.reserve(snapshot.instances.size());
    for (const core::Instance& instance : snapshot.instances) {
        positions.emplace_back((instance.*which).x, (instance.*which).y);
    }
    std::ranges::sort(positions);
    return positions;
}

TEST_CASE("a tick moves the foxes, and their previous position is where they were") {
    game::Arena arena;
    const core::Snapshot before = Published(arena);
    arena.Tick({ .wall = 1, .simulation = 1, .state = 1 }, {});
    const core::Snapshot after = Published(arena);
    // Copied, not recomputed: the previous positions are the same floats.
    CHECK(Positions(after, &core::Instance::previous) == Positions(before, &core::Instance::current));
    CHECK(Positions(after, &core::Instance::current) != Positions(before, &core::Instance::current));
}

// The sprite an instance draws, and where its foot stands: MakeInstance offsets the quad's
// centre from the foot by a fixed amount per sprite.
std::optional<std::pair<SpriteId, f32>> SpriteAndFootY(const core::Instance& instance) {
    for (std::size_t id = 0; id < core::atlas::SPRITES.size(); ++id) {
        const auto sprite = static_cast<SpriteId>(id);
        if (Draws(instance, sprite)) {
            return std::pair{ sprite, instance.current.y - core::MakeInstance({ .sprite = sprite }).current.y };
        }
    }
    return std::nullopt;
}

// The arena after enough ticks that foxes have moved in front of and behind things.
core::Snapshot MovedArena() {
    constexpr u64 TICKS = 90;
    game::Arena arena;
    for (u64 tick = 1; tick <= TICKS; ++tick) {
        arena.Tick({ .wall = tick, .simulation = tick, .state = tick }, {});
    }
    return Published(arena);
}

TEST_CASE("the arena publishes a normal batch, then an additive one that ends the snapshot") {
    const core::Snapshot snapshot = MovedArena();
    REQUIRE(snapshot.batches.size() == 2);
    CHECK(snapshot.batches[0].pipeline == core::Pipeline::Normal);
    CHECK(snapshot.batches[1].pipeline == core::Pipeline::Additive);
    CHECK(snapshot.batches[1].first + snapshot.batches[1].count == snapshot.instances.size());
}

TEST_CASE("the world draws in foot-y order, after the ground and its decals") {
    const core::Snapshot snapshot = MovedArena();
    REQUIRE_FALSE(snapshot.batches.empty());
    f32 lastFoot = -std::numeric_limits<f32>::infinity();
    std::size_t world = 0;
    for (u32 i = 0; i < snapshot.batches.back().first; ++i) {
        const auto drawn = SpriteAndFootY(snapshot.instances[i]);
        if (!drawn) {
            FAIL("an instance draws no sprite of the manifest");
            break;
        }
        if (drawn->first == SpriteId::Ground || drawn->first == SpriteId::Shadow) {
            CHECK(world == 0); // the ground and its decals come before the world
            continue;
        }
        ++world;
        CHECK(drawn->second >= lastFoot);
        lastFoot = drawn->second;
    }
    CHECK(world > 0);
}

TEST_CASE("the additive batch holds only the orbs") {
    const core::Snapshot snapshot = MovedArena();
    REQUIRE_FALSE(snapshot.batches.empty());
    for (u32 i = snapshot.batches.back().first; i < snapshot.instances.size(); ++i) {
        CHECK((Draws(snapshot.instances[i], SpriteId::OrbBlue) || Draws(snapshot.instances[i], SpriteId::OrbRed)));
    }
}

TEST_CASE("the stress arena publishes 100,000 instances") {
    constexpr std::size_t INSTANCES = 100'000;
    game::Arena arena{ game::Scene::Stress };
    CHECK(Published(arena).instances.size() >= INSTANCES);
}

TEST_CASE("the stress arena's 10,000 bullets draw last, in an additive batch") {
    constexpr u32 BULLETS = 10'000;
    game::Arena arena{ game::Scene::Stress };
    const core::Snapshot snapshot = Published(arena);
    REQUIRE_FALSE(snapshot.batches.empty());
    const core::Batch& effects = snapshot.batches.back();
    CHECK(effects.pipeline == core::Pipeline::Additive);
    CHECK(effects.count >= BULLETS);
    CHECK(effects.first + effects.count == snapshot.instances.size());
}

TEST_CASE("the painted scene's glows draw last, in an additive batch") {
    constexpr u32 GLOWS = 5;
    game::Arena arena{ game::Scene::Painted };
    const core::Snapshot snapshot = Published(arena);
    REQUIRE_FALSE(snapshot.batches.empty());
    CHECK(snapshot.batches.back().pipeline == core::Pipeline::Additive);
    CHECK(snapshot.batches.back().count == GLOWS);
}

TEST_CASE("the painted scene holds still") {
    constexpr u64 TICK = 7;
    game::Arena arena{ game::Scene::Painted };
    const core::Snapshot before = Published(arena);
    arena.Tick({ .wall = TICK, .simulation = TICK, .state = TICK }, {});
    const core::Snapshot after = Published(arena);
    CHECK(std::ranges::equal(std::as_bytes(std::span{ before.instances }), std::as_bytes(std::span{ after.instances })));
}

TEST_CASE("the cell grid publishes every cell, with none culled at its far corners") {
    constexpr std::size_t CELLS = 640;
    game::Arena arena{ game::Scene::Cells };
    const core::Snapshot snapshot = Published(arena);
    CHECK(snapshot.instances.size() == CELLS + 1); // and the ground, hidden beneath them
}

TEST_CASE("the stress arena publishes the same bytes with one worker and with many") {
    constexpr u64 TICK = 7;
    constexpr u32 MANY_WORKERS = 7;
    std::vector<std::vector<core::Instance>> published;
    for (const u32 workers : { 1U, MANY_WORKERS }) {
        game::Arena arena{ game::Scene::Stress };
        arena.Tick({ .wall = TICK, .simulation = TICK, .state = TICK }, {});
        core::Jobs jobs{ workers };
        core::Snapshot snapshot;
        arena.Publish(snapshot, jobs);
        published.push_back(std::move(snapshot.instances));
    }
    REQUIRE_FALSE(published[0].empty());
    CHECK(std::ranges::equal(std::as_bytes(std::span{ published[0] }), std::as_bytes(std::span{ published[1] })));
}

TEST_CASE("two arenas ticked alike publish the same bytes") {
    constexpr u64 TICKS = 120;
    game::Arena first;
    game::Arena second;
    for (u64 tick = 1; tick <= TICKS; ++tick) {
        first.Tick({ .wall = tick, .simulation = tick, .state = tick }, {});
        second.Tick({ .wall = tick, .simulation = tick, .state = tick }, {});
    }
    const std::vector<core::Instance> mine = Published(first).instances;
    const std::vector<core::Instance> theirs = Published(second).instances;
    CHECK(std::ranges::equal(std::as_bytes(std::span{ mine }), std::as_bytes(std::span{ theirs })));
}

TEST_CASE("an orbit stays on its circle after many ticks") {
    constexpr f64 RADIUS = 150.0;
    constexpr f32 SPEED = -0.02F;
    constexpr game::Orbit ORBIT{ .radius = static_cast<f32>(RADIUS), .speed = SPEED };
    constexpr f64 VIEW_TILT = 0.6;                                     // arena.cpp's: the circle is drawn flattened in y
    constexpr f64 TOLERANCE = 0.01;                                    // world units
    constexpr std::array<u64, 2> TICKS{ { 16'777'217, 100'000'000 } }; // 2^24 + 1, and about 19 days at 60 Hz
    for (const u64 tick : TICKS) {
        CAPTURE(tick);
        const f64 angle = static_cast<f64>(SPEED) * static_cast<f64>(tick); // exact in f64
        const core::Vec2 point = game::OrbitPoint(ORBIT, tick);
        CHECK(std::abs(static_cast<f64>(point.x) - (RADIUS * std::cos(angle))) < TOLERANCE);
        CHECK(std::abs(static_cast<f64>(point.y) - (RADIUS * std::sin(angle) * VIEW_TILT)) < TOLERANCE);
    }
}

TEST_CASE("months pass for the stress arena's world NPCs, and its active NPCs stay put") {
    constexpr u32 WORLD_NPCS = 50'000;
    constexpr std::size_t ACTIVE_NPCS = 6;
    game::Arena arena{ game::Scene::Stress };
    core::Jobs jobs{ 1 };
    const core::State& state = arena.State();
    CHECK(state[game::WORLD].Entities().storage<game::Npc>()->size() == WORLD_NPCS);
    CHECK(state[game::ACTIVE].Entities().storage<game::Npc>()->size() == ACTIVE_NPCS);

    constexpr u64 MONTHS = 12; // loneliness outweighs ambition after several
    for (u64 month = 1; month <= MONTHS; ++month) {
        arena.AdvanceCalendar({ .wall = month, .simulation = 0, .state = month }, jobs, {});
    }
    CHECK_FALSE(arena.NpcRelationships().All().empty());
    CHECK(state[game::ACTIVE].Entities().storage<game::Npc>()->size() == ACTIVE_NPCS);
}

} // namespace
