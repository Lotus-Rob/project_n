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

TEST_CASE("a tick moves the foxes, and their previous position is where they were") {
    game::Arena arena;
    const core::Snapshot before = Published(arena);
    arena.Tick(1);
    const core::Snapshot after = Published(arena);
    REQUIRE(before.instances.size() == after.instances.size());
    std::size_t moved = 0;
    for (std::size_t i = 0; i < after.instances.size(); ++i) {
        CHECK(after.instances[i].previous.x == doctest::Approx(before.instances[i].current.x));
        CHECK(after.instances[i].previous.y == doctest::Approx(before.instances[i].current.y));
        if (std::abs(after.instances[i].current.x - before.instances[i].current.x) > 0.0F) {
            ++moved;
        }
    }
    CHECK(moved > 0);
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
