#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/state.hpp"

#include <doctest/doctest.h>

namespace {

constexpr core::DomainIndex ACTIVE{ 0 };
constexpr core::DomainIndex WORLD{ 1 };
constexpr u64 SEED = 42;

struct Place {
    i32 cell = 0;
};

struct Position {
    f32 x = 0.0F;
};

TEST_CASE("the state knows which domain each stable ID is in") {
    core::State state{ 2, SEED };
    const auto [npc, entity] = state.Create(WORLD);
    CHECK(state.DomainOf(npc) == WORLD);
    CHECK(state[WORLD].Find(npc) == entity);
    CHECK(state[ACTIVE].Find(npc) == core::Entity{ entt::null });

    state.Destroy(npc);
    CHECK_FALSE(state.DomainOf(npc).has_value());
    CHECK(state[WORLD].Find(npc) == core::Entity{ entt::null });
}

TEST_CASE("stable IDs are unique across domains") {
    core::State state{ 2, SEED };
    const core::StableId first = state.Create(ACTIVE).first;
    const core::StableId second = state.Create(WORLD).first;
    CHECK(first != second);
    CHECK(state.Ids().next == 3);
}

TEST_CASE("an entity moving between domains keeps its stable ID and derives its components") {
    constexpr i32 CELL = 7;
    constexpr f32 CELL_WIDTH = 10.0F;
    core::State state{ 2, SEED };
    const auto [npc, was] = state.Create(WORLD);
    state[WORLD].Entities().emplace<Place>(was, CELL);

    const core::Entity moved =
        state.Move(npc, ACTIVE, [](const core::Registry& from, core::Entity old, core::Registry& into, core::Entity now) {
            into.emplace<Position>(now, static_cast<f32>(from.get<Place>(old).cell) * CELL_WIDTH);
        });

    CHECK(state.DomainOf(npc) == ACTIVE);
    CHECK(state[ACTIVE].Find(npc) == moved);
    CHECK(state[ACTIVE].Entities().get<core::StableId>(moved) == npc);
    CHECK(state[ACTIVE].Entities().get<Position>(moved).x == doctest::Approx(CELL * CELL_WIDTH));
    CHECK(state[WORLD].Find(npc) == core::Entity{ entt::null });
    CHECK(state[WORLD].Entities().storage<Place>().empty());
}

} // namespace
