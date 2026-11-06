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

struct Health {
    i32 points = 0;

    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.points);
    }

    [[nodiscard]] bool Valid() const {
        return points >= 0;
    }
};

struct Tagged {};

using Saved = core::ComponentList<Health, Tagged>;

// The stable IDs in the order a view over Health visits them, with their points.
std::vector<std::pair<u64, i32>> Visits(const core::Domain& domain) {
    std::vector<std::pair<u64, i32>> visits;
    for (const auto [entity, stableId, health] : domain.Entities().view<const core::StableId, const Health>().each()) {
        visits.emplace_back(std::to_underlying(stableId), health.points);
    }
    return visits;
}

// A world domain with history: created, given components and destroyed out of order.
core::State Populated() {
    constexpr i32 POINTS = 10;
    constexpr u32 ENTITIES = 6;
    core::State state{ 2, SEED };
    for (u32 i = 0; i < ENTITIES; ++i) {
        const auto [stableId, entity] = state.Create(WORLD);
        state[WORLD].Entities().emplace<Health>(entity, POINTS * static_cast<i32>(i));
        if (i % 2 == 0) {
            state[WORLD].Entities().emplace<Tagged>(entity);
        }
    }
    state.Destroy(core::StableId{ 2 });
    return state;
}

std::vector<std::byte> SavedWorld(const core::State& state) {
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    core::SaveDomain(writer, state[WORLD], Saved{});
    return bytes;
}

// Loads `bytes` into a fresh state's world domain; false when the bytes are malformed.
bool Loads(std::span<const std::byte> bytes, core::State& into) {
    core::Reader reader{ bytes };
    core::LoadDomain(reader, into, WORLD, Saved{});
    return !reader.Failed() && reader.Remaining() == 0;
}

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

TEST_CASE("a domain loaded from a save iterates as the saved one, components and all") {
    const core::State saved = Populated();
    core::State loaded{ 2, SEED, saved.Ids() };
    const std::vector<std::byte> bytes = SavedWorld(saved);
    const bool loads = Loads(bytes, loaded);
    REQUIRE(loads);
    CHECK(Visits(loaded[WORLD]) == Visits(saved[WORLD]));
    CHECK(loaded.DomainOf(core::StableId{ 3 }) == WORLD);
    CHECK_FALSE(loaded.DomainOf(core::StableId{ 2 }).has_value());
    CHECK(loaded[WORLD].Entities().all_of<Tagged>(loaded[WORLD].Find(core::StableId{ 1 })));
    CHECK_FALSE(loaded[WORLD].Entities().all_of<Tagged>(loaded[WORLD].Find(core::StableId{ 4 })));
    CHECK(SavedWorld(loaded) == SavedWorld(saved));
}

TEST_CASE("a save naming an ID the counter never handed out is malformed") {
    const core::State saved = Populated();
    core::State loaded{ 2, SEED, core::StableIdCounter{ .next = 3 } };
    const std::vector<std::byte> bytes = SavedWorld(saved);
    const bool loads = Loads(bytes, loaded);
    CHECK_FALSE(loads);
}

TEST_CASE("a save with the same entity twice is malformed") {
    constexpr u64 TWICE = 1;
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    writer(u64{ 2 }, core::StableId{ TWICE }, core::StableId{ TWICE }, u64{ 0 }, u64{ 0 });
    core::State loaded{ 2, SEED, core::StableIdCounter{ .next = 2 } };
    const bool loads = Loads(bytes, loaded);
    CHECK_FALSE(loads);
}

TEST_CASE("a component for an entity not in the domain, or a value it rejects, is malformed") {
    constexpr i32 NEGATIVE = -1;
    constexpr u64 MISSING = 2;
    std::vector<std::byte> missing;
    core::Writer toMissing{ missing };
    toMissing(u64{ 1 }, core::StableId{ 1 }, u64{ 1 }, core::StableId{ MISSING }, i32{ 0 }, u64{ 0 });
    core::State first{ 2, SEED, core::StableIdCounter{ .next = 3 } };
    const bool loadsMissing = Loads(missing, first);
    CHECK_FALSE(loadsMissing);

    std::vector<std::byte> invalid;
    core::Writer toInvalid{ invalid };
    toInvalid(u64{ 1 }, core::StableId{ 1 }, u64{ 1 }, core::StableId{ 1 }, NEGATIVE, u64{ 0 });
    core::State second{ 2, SEED, core::StableIdCounter{ .next = 2 } };
    const bool loadsInvalid = Loads(invalid, second);
    CHECK_FALSE(loadsInvalid);
}

} // namespace
