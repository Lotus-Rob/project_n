#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/ecs.hpp"

#include <doctest/doctest.h>

namespace {

struct Health {
    i32 points = 0;
};

struct Speed {
    f32 value = 0.0F;
};

// The stable IDs in the order a view over `Component` visits them.
template<typename Component>
std::vector<u64> VisitOrder(core::Domain& domain) {
    std::vector<u64> order;
    for (const auto [entity, stableId, component] : domain.Entities().view<const core::StableId, const Component>().each()) {
        order.push_back(std::to_underlying(stableId));
    }
    return order;
}

TEST_CASE("stable IDs count up from 1 and are never reused") {
    core::StableIdCounter counter;
    CHECK(counter.Allocate() == core::StableId{ 1 });
    CHECK(counter.Allocate() == core::StableId{ 2 });
    CHECK(counter.next == 3);
}

TEST_CASE("a domain finds an entity by its stable ID until it is destroyed") {
    constexpr core::StableId NPC{ 7 };
    core::Domain domain;
    const core::Entity entity = domain.Create(NPC);
    CHECK(domain.Find(NPC) == entity);
    CHECK(domain.Entities().get<core::StableId>(entity) == NPC);

    domain.Destroy(NPC);
    CHECK(domain.Find(NPC) == core::Entity{ entt::null });
}

TEST_CASE("an unknown stable ID finds nothing") {
    constexpr core::StableId STALE{ 99 };
    const core::Domain domain;
    CHECK(domain.Find(STALE) == core::Entity{ entt::null });
}

TEST_CASE("canonical order visits every storage by stable ID, whatever the history") {
    constexpr std::array<u64, 5> CREATED{ { 5, 2, 9, 1, 7 } };
    core::Domain domain;
    for (const u64 created : CREATED) {
        const core::Entity entity = domain.Create(core::StableId{ created });
        domain.Entities().emplace<Health>(entity);
        if (created % 2 == 1) {
            domain.Entities().emplace<Speed>(entity);
        }
    }
    domain.Destroy(core::StableId{ 2 }); // moves the last entity into its place

    domain.SortCanonical();
    CHECK(VisitOrder<Health>(domain) == std::vector<u64>{ 1, 5, 7, 9 });
    CHECK(VisitOrder<Speed>(domain) == std::vector<u64>{ 1, 5, 7, 9 });
}

} // namespace
