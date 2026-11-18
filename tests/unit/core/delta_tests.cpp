#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/delta.hpp"
#include "core/ecs.hpp"
#include "core/state.hpp"

#include <doctest/doctest.h>

namespace {

constexpr core::DomainIndex WORLD{ 0 };
constexpr u64 SEED = 7;
constexpr u64 BASE_ENTITIES = 5;
/// The world counter every state here starts from: it has handed out IDs up to 9, so 6 to 9 are valid created IDs
/// beside the base's 1 to 5, and 10 is the first never handed out.
constexpr core::StableIdCounter COUNTER{ .next = 10 };
/// The scene counter every state here starts from: it has handed out the scene range's first two IDs.
constexpr core::StableIdCounter SCENE_COUNTER{ .next = core::SCENE_ID_BASE + 2 };
constexpr u64 NONE = 0;

/// A component of one field.
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

/// A component of three fields, of three sizes; a rank past 9 is malformed.
struct Stats {
    i32 power = 0;
    f32 speed = 0.0F;
    u8 rank = 0;

    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.power, self.speed, self.rank);
    }

    [[nodiscard]] bool Valid() const {
        return rank <= 9;
    }
};

/// A component whose one field is a vector.
struct Path {
    std::vector<i32> tiles;

    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.tiles);
    }
};

struct Tagged {};

using Saved = core::ComponentList<Health, Stats, Path, Tagged>;
/// A shorter list, so a hand-written delta stays readable: its sections are Stats's gained places and values, lost
/// places, changed places, masks and values, then Tagged's gained and lost places.
using Short = core::ComponentList<Stats, Tagged>;
/// A list of Path alone, whose sections are its gained places and values, lost places, and changed places, masks and
/// values: the even entities lack a Path, so one can gain it.
using Paths = core::ComponentList<Path>;

/// The base as the seed regenerates it: entities 1 to 5, each with Health and Stats, the odd ones a Path, the even
/// ones Tagged, in a state whose counters are `ids` and `sceneIds` (a loaded state's counters are the saved ones).
core::State Base(core::StableIdCounter ids = COUNTER, core::StableIdCounter sceneIds = SCENE_COUNTER) {
    core::State state{ 1, SEED, ids, sceneIds };
    for (u64 id = 1; id <= BASE_ENTITIES; ++id) {
        const bool restored = state.Restore(WORLD, core::StableId{ id });
        REQUIRE(restored);
        core::Registry& registry = state[WORLD].Entities();
        const core::Entity entity = state[WORLD].Find(core::StableId{ id });
        const auto value = static_cast<i32>(id);
        registry.emplace<Health>(entity, 10 * value);
        registry.emplace<Stats>(entity, value, 1.5F * static_cast<f32>(value), static_cast<u8>(id));
        if (id % 2 == 1) {
            registry.emplace<Path>(entity, std::vector<i32>{ value, value + 1 });
        }
        else {
            registry.emplace<Tagged>(entity);
        }
    }
    return state;
}

core::Entity Of(const core::State& state, u64 stableId) {
    return state[WORLD].Find(core::StableId{ stableId });
}

template<typename Component>
Component& Get(core::State& state, u64 stableId) {
    return state[WORLD].Entities().get<Component>(Of(state, stableId));
}

template<typename Component>
bool Has(const core::State& state, u64 stableId) {
    return state[WORLD].Entities().all_of<Component>(Of(state, stableId));
}

template<typename... Components>
std::vector<std::byte> Delta(const core::State& base, const core::State& now, core::ComponentList<Components...> list) {
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    core::SaveDomainDelta(writer, base[WORLD], now[WORLD], list);
    return bytes;
}

/// Applies `bytes` to `into`'s world domain, which holds the base; false when the delta is refused or leaves bytes.
template<typename... Components>
bool Loads(std::span<const std::byte> bytes, core::State& into, core::ComponentList<Components...> list) {
    core::Reader reader{ bytes };
    core::LoadDomainDelta(reader, into, WORLD, list);
    return !reader.Failed() && reader.Remaining() == 0;
}

/// The world domain's hash in canonical order, as a save would hash it.
u64 Hash(core::State& state) {
    state.SortCanonical();
    core::Hasher hasher;
    core::SaveDomain(hasher, state[WORLD], Saved{});
    return hasher.Value();
}

/// The stable IDs in the order a view over Health visits them, with their points.
std::vector<std::pair<u64, i32>> Visits(const core::State& state) {
    std::vector<std::pair<u64, i32>> visits;
    for (const auto [entity, stableId, health] : state[WORLD].Entities().view<const core::StableId, const Health>().each()) {
        visits.emplace_back(std::to_underlying(stableId), health.points);
    }
    return visits;
}

template<typename... T>
std::vector<std::byte> Bytes(const T&... values) {
    std::vector<std::byte> bytes;
    core::Writer{ bytes }(values...);
    return bytes;
}

TEST_CASE("an unchanged domain's delta has no entries") {
    const core::State base = Base();
    core::State now = Base();
    const core::DeltaCounts counts = core::CountDelta(base[WORLD], now[WORLD], Saved{});
    CHECK(counts.removed == 0);
    CHECK(counts.created == 0);
    CHECK(counts.changed == 0);

    const std::vector<std::byte> delta = Delta(base, now, Saved{});
    // Only zero counts: removed and created, then gained, lost and changed for each of Health, Stats and Path, and
    // gained and lost for the empty Tagged.
    CHECK(delta == std::vector<std::byte>(13 * sizeof(u64)));
    core::State loaded = Base();
    const bool loads = Loads(delta, loaded, Saved{});
    REQUIRE(loads);
    CHECK(Hash(loaded) == Hash(now));
}

TEST_CASE("a changed field is written once, and only it") {
    const core::State base = Base();
    core::State now = Base();
    const std::size_t unchanged = Delta(base, now, Saved{}).size();
    Get<Stats>(now, 2).speed = 40.0F;
    Get<Stats>(now, 4).speed = 80.0F;

    const std::vector<std::byte> delta = Delta(base, now, Saved{});
    // Each changed entity adds its place and its mask (a u64 each) and the one new value, never the other two fields.
    CHECK(delta.size() == unchanged + (2 * (sizeof(u64) + sizeof(u64) + sizeof(f32))));
    CHECK(core::CountDelta(base[WORLD], now[WORLD], Saved{}).changed == 2);

    core::State loaded = Base();
    const bool loads = Loads(delta, loaded, Saved{});
    REQUIRE(loads);
    CHECK(Get<Stats>(loaded, 2).speed == doctest::Approx(40.0F));
    CHECK(Get<Stats>(loaded, 4).speed == doctest::Approx(80.0F));
    CHECK(Get<Stats>(loaded, 4).power == 4);
    CHECK(Get<Stats>(loaded, 4).rank == 4);
    CHECK(Hash(loaded) == Hash(now));
}

TEST_CASE("removed and created entities round-trip") {
    const core::State base = Base();
    core::State now = Base();
    now.Destroy(core::StableId{ 2 });
    const auto [walker, walkerEntity] = now.Create(WORLD, core::IdRange::World);
    now[WORLD].Entities().emplace<Health>(walkerEntity, 7);
    now[WORLD].Entities().emplace<Path>(walkerEntity, std::vector<i32>{ 3, 4, 5 });
    const auto [marker, markerEntity] = now.Create(WORLD, core::IdRange::Scene);
    now[WORLD].Entities().emplace<Tagged>(markerEntity);

    const core::DeltaCounts counts = core::CountDelta(base[WORLD], now[WORLD], Saved{});
    CHECK(counts.removed == 1);
    CHECK(counts.created == 2);
    CHECK(counts.changed == 0);

    const std::vector<std::byte> delta = Delta(base, now, Saved{});
    core::State loaded = Base(now.Ids(), now.SceneIds());
    const bool loads = Loads(delta, loaded, Saved{});
    REQUIRE(loads);
    CHECK_FALSE(loaded.DomainOf(core::StableId{ 2 }).has_value());
    CHECK(loaded.DomainOf(walker) == WORLD);
    CHECK(loaded.DomainOf(marker) == WORLD);
    CHECK(Get<Path>(loaded, std::to_underlying(walker)).tiles == std::vector<i32>{ 3, 4, 5 });
    CHECK_FALSE(Has<Stats>(loaded, std::to_underlying(walker)));
    CHECK(Has<Tagged>(loaded, std::to_underlying(marker)));
    CHECK_FALSE(Has<Health>(loaded, std::to_underlying(marker)));
    CHECK(Hash(loaded) == Hash(now));
}

TEST_CASE("a component gained or lost round-trips") {
    const core::State base = Base();
    core::State now = Base();
    now[WORLD].Entities().emplace<Tagged>(Of(now, 1));
    now[WORLD].Entities().remove<Tagged>(Of(now, 2));
    now[WORLD].Entities().remove<Path>(Of(now, 3));
    now[WORLD].Entities().emplace<Path>(Of(now, 4), std::vector<i32>{ 9 });
    CHECK(core::CountDelta(base[WORLD], now[WORLD], Saved{}).changed == 4);

    const std::vector<std::byte> delta = Delta(base, now, Saved{});
    core::State loaded = Base();
    const bool loads = Loads(delta, loaded, Saved{});
    REQUIRE(loads);
    CHECK(Has<Tagged>(loaded, 1));
    CHECK_FALSE(Has<Tagged>(loaded, 2));
    CHECK_FALSE(Has<Path>(loaded, 3));
    CHECK(Get<Path>(loaded, 4).tiles == std::vector<i32>{ 9 });
    CHECK(Hash(loaded) == Hash(now));
}

TEST_CASE("a vector field changed in length round-trips") {
    const core::State base = Base();
    core::State now = Base();
    Get<Path>(now, 1).tiles = { 1, 2, 3, 4, 5 };
    Get<Path>(now, 3).tiles.clear();
    CHECK(core::CountDelta(base[WORLD], now[WORLD], Saved{}).changed == 2);

    const std::vector<std::byte> delta = Delta(base, now, Saved{});
    core::State loaded = Base();
    const bool loads = Loads(delta, loaded, Saved{});
    REQUIRE(loads);
    CHECK(Get<Path>(loaded, 1).tiles == std::vector<i32>{ 1, 2, 3, 4, 5 });
    CHECK(Get<Path>(loaded, 3).tiles.empty());
    CHECK(Get<Path>(loaded, 5).tiles == std::vector<i32>{ 5, 6 });
}

TEST_CASE("the loaded domain hashes and iterates as the saved one") {
    const core::State base = Base();
    core::State now = Base();
    // A history out of order: an entity destroyed between two creations, components changed, gained and lost.
    const core::StableId first = now.Create(WORLD, core::IdRange::World).first;
    now.Destroy(core::StableId{ 4 });
    const auto [second, secondEntity] = now.Create(WORLD, core::IdRange::Scene);
    now[WORLD].Entities().emplace<Health>(secondEntity, 3);
    now[WORLD].Entities().emplace<Health>(now[WORLD].Find(first), 1);
    Get<Health>(now, 1).points = 99;
    Get<Stats>(now, 3).power = -5;
    Get<Stats>(now, 3).rank = 9;
    now[WORLD].Entities().remove<Health>(Of(now, 5));
    now[WORLD].Entities().emplace<Tagged>(Of(now, 5));

    const core::DeltaCounts counts = core::CountDelta(base[WORLD], now[WORLD], Saved{});
    CHECK(counts.removed == 1);
    CHECK(counts.created == 2);
    CHECK(counts.changed == 3);

    const std::vector<std::byte> delta = Delta(base, now, Saved{});
    core::State loaded = Base(now.Ids(), now.SceneIds());
    const bool loads = Loads(delta, loaded, Saved{});
    REQUIRE(loads);
    CHECK(Hash(loaded) == Hash(now));
    CHECK(Visits(loaded) == Visits(now));
}

TEST_CASE("a malformed delta is refused") {
    core::State state = Base();

    SUBCASE("well formed: Stats's power changed on entity 1 (the samples' layout, accepted)") {
        CHECK(Loads(Bytes(NONE, NONE, NONE, NONE, u64{ 1 }, u64{ 0 }, u64{ 0b1 }, i32{ 99 }, NONE, NONE), state, Short{}));
        CHECK(Get<Stats>(state, 1).power == 99);
    }
    SUBCASE("a removed place past the base's count") {
        CHECK_FALSE(Loads(Bytes(u64{ 1 }, BASE_ENTITIES, NONE, NONE, NONE, NONE, NONE, NONE), state, Short{}));
    }
    SUBCASE("a removed place given twice") {
        CHECK_FALSE(Loads(Bytes(u64{ 2 }, u64{ 1 }, u64{ 1 }, NONE, NONE, NONE, NONE, NONE, NONE), state, Short{}));
    }
    SUBCASE("a count past the base's") {
        CHECK_FALSE(Loads(Bytes(~u64{ 0 }, NONE, NONE, NONE, NONE, NONE, NONE), state, Short{}));
    }
    SUBCASE("a created count past the bytes left") {
        CHECK_FALSE(Loads(Bytes(NONE, ~u64{ 0 }, NONE, NONE, NONE, NONE, NONE), state, Short{}));
    }
    SUBCASE("well formed: a created ID the world counter handed out (accepted)") {
        CHECK(Loads(Bytes(NONE, u64{ 1 }, core::StableId{ 9 }, NONE, NONE, NONE, NONE, NONE, NONE), state, Short{}));
        CHECK(state.DomainOf(core::StableId{ 9 }) == WORLD);
    }
    SUBCASE("well formed: a created ID the scene counter handed out (accepted)") {
        const core::StableId scene{ core::SCENE_ID_BASE + 1 };
        CHECK(Loads(Bytes(NONE, u64{ 1 }, scene, NONE, NONE, NONE, NONE, NONE, NONE), state, Short{}));
        CHECK(state.DomainOf(scene) == WORLD);
    }
    SUBCASE("a created ID the scene counter never handed out") {
        CHECK_FALSE(Loads(
            Bytes(NONE, u64{ 1 }, core::StableId{ SCENE_COUNTER.next }, NONE, NONE, NONE, NONE, NONE, NONE), state, Short{}
        ));
    }
    SUBCASE("a created ID the state already has") {
        CHECK_FALSE(Loads(Bytes(NONE, u64{ 1 }, core::StableId{ 3 }, NONE, NONE, NONE, NONE, NONE, NONE), state, Short{}));
    }
    SUBCASE("a created ID the same delta removes") {
        // IDs are never reused, so a removed base entity can't come back as a created one.
        CHECK_FALSE(
            Loads(Bytes(u64{ 1 }, u64{ 2 }, u64{ 1 }, core::StableId{ 3 }, NONE, NONE, NONE, NONE, NONE, NONE), state, Short{})
        );
    }
    SUBCASE("a created ID the counter never handed out") {
        CHECK_FALSE(
            Loads(Bytes(NONE, u64{ 1 }, core::StableId{ COUNTER.next }, NONE, NONE, NONE, NONE, NONE, NONE), state, Short{})
        );
    }
    SUBCASE("a created ID of 0") {
        CHECK_FALSE(Loads(Bytes(NONE, u64{ 1 }, core::StableId{ 0 }, NONE, NONE, NONE, NONE, NONE, NONE), state, Short{}));
    }
    SUBCASE("a created entity's component past the list") {
        CHECK_FALSE(
            Loads(Bytes(NONE, u64{ 1 }, core::StableId{ 6 }, u64{ 0b100 }, NONE, NONE, NONE, NONE, NONE), state, Short{})
        );
    }
    SUBCASE("a created entity's value that fails Valid()") {
        CHECK_FALSE(Loads(
            Bytes(NONE, u64{ 1 }, core::StableId{ 6 }, u64{ 0b1 }, Stats{ .rank = 10 }, NONE, NONE, NONE, NONE, NONE),
            state,
            Short{}
        ));
    }
    SUBCASE("a gained place past the base's count") {
        CHECK_FALSE(Loads(Bytes(NONE, NONE, NONE, NONE, NONE, u64{ 1 }, BASE_ENTITIES, NONE), state, Short{}));
    }
    SUBCASE("a component gained by an entity that has it") {
        // Entity 2, at place 1, is Tagged.
        CHECK_FALSE(Loads(Bytes(NONE, NONE, NONE, NONE, NONE, u64{ 1 }, u64{ 1 }, NONE), state, Short{}));
    }
    SUBCASE("a component gained and lost in one section") {
        // Entity 1, at place 0, lacks Tagged.
        CHECK_FALSE(Loads(Bytes(NONE, NONE, NONE, NONE, NONE, u64{ 1 }, u64{ 0 }, u64{ 1 }, u64{ 0 }), state, Short{}));
    }
    SUBCASE("a component gained and changed in one section") {
        // Entity 2, at place 1, lacks a Path.
        CHECK_FALSE(Loads(
            Bytes(
                NONE,
                NONE,
                u64{ 1 },
                u64{ 1 },
                std::vector<i32>{ 1 },
                NONE,
                u64{ 1 },
                u64{ 1 },
                u64{ 0b1 },
                std::vector<i32>{ 2 }
            ),
            state,
            Paths{}
        ));
    }
    SUBCASE("a component lost by an entity without it") {
        CHECK_FALSE(Loads(Bytes(NONE, NONE, NONE, NONE, NONE, NONE, u64{ 1 }, u64{ 0 }), state, Short{}));
    }
    SUBCASE("a changed entity the same delta removes") {
        CHECK_FALSE(Loads(
            Bytes(u64{ 1 }, u64{ 0 }, NONE, NONE, NONE, u64{ 1 }, u64{ 0 }, u64{ 0b1 }, i32{ 99 }, NONE, NONE), state, Short{}
        ));
    }
    SUBCASE("a mask bit past the type's field count") {
        CHECK_FALSE(Loads(Bytes(NONE, NONE, NONE, NONE, u64{ 1 }, u64{ 0 }, u64{ 0b1000 }, NONE, NONE), state, Short{}));
    }
    SUBCASE("a mask with no field") {
        CHECK_FALSE(Loads(Bytes(NONE, NONE, NONE, NONE, u64{ 1 }, u64{ 0 }, NONE, NONE, NONE), state, Short{}));
    }
    SUBCASE("a changed value that fails Valid()") {
        CHECK_FALSE(
            Loads(Bytes(NONE, NONE, NONE, NONE, u64{ 1 }, u64{ 0 }, u64{ 0b100 }, u8{ 10 }, NONE, NONE), state, Short{})
        );
    }
    SUBCASE("a truncated section") {
        core::State now = Base();
        Get<Stats>(now, 2).speed = 40.0F;
        std::vector<std::byte> delta = Delta(Base(), now, Short{});
        delta.pop_back();
        CHECK_FALSE(Loads(delta, state, Short{}));
    }
    SUBCASE("a delta cut off after its first section") {
        CHECK_FALSE(Loads(Bytes(NONE), state, Short{}));
    }
}

} // namespace
