#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "game/arena.hpp"

#include <doctest/doctest.h>

namespace {

using core::atlas::SpriteId;

core::Snapshot Published(game::Arena& arena) {
    core::Snapshot snapshot;
    arena.Publish(snapshot);
    return snapshot;
}

// Whether an instance draws `sprite`: its region starts where the sprite's does.
bool Draws(const core::Instance& instance, SpriteId sprite) {
    return instance.regionMin == core::MakeInstance({ .sprite = sprite }).regionMin;
}

TEST_CASE("the ground is drawn first, and shadows before what casts them") {
    game::Arena arena;
    const core::Snapshot snapshot = Published(arena);
    REQUIRE_FALSE(snapshot.instances.empty());
    CHECK(Draws(snapshot.instances.front(), SpriteId::Ground));
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
    arena.Tick(1);
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

TEST_CASE("the world draws in foot-y order, and the additive orbs over it in their own batch") {
    constexpr u64 TICKS = 90; // far enough that foxes have moved in front of and behind things
    game::Arena arena;
    for (u64 tick = 1; tick <= TICKS; ++tick) {
        arena.Tick(tick);
    }
    const core::Snapshot snapshot = Published(arena);
    REQUIRE(snapshot.batches.size() == 2);
    CHECK(snapshot.batches[0].pipeline == core::Pipeline::Normal);
    CHECK(snapshot.batches[1].pipeline == core::Pipeline::Additive);
    CHECK(snapshot.batches[1].first + snapshot.batches[1].count == snapshot.instances.size());
    f32 lastFoot = -std::numeric_limits<f32>::infinity();
    std::size_t world = 0;
    for (u32 i = 0; i < snapshot.batches[1].first; ++i) {
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
    for (u32 i = snapshot.batches[1].first; i < snapshot.instances.size(); ++i) {
        CHECK((Draws(snapshot.instances[i], SpriteId::OrbBlue) || Draws(snapshot.instances[i], SpriteId::OrbRed)));
    }
}

TEST_CASE("two arenas ticked alike publish the same bytes") {
    constexpr u64 TICKS = 120;
    game::Arena first;
    game::Arena second;
    for (u64 tick = 1; tick <= TICKS; ++tick) {
        first.Tick(tick);
        second.Tick(tick);
    }
    const std::vector<core::Instance> mine = Published(first).instances;
    const std::vector<core::Instance> theirs = Published(second).instances;
    CHECK(std::ranges::equal(std::as_bytes(std::span{ mine }), std::as_bytes(std::span{ theirs })));
}

} // namespace
