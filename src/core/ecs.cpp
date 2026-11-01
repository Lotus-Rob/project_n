#include "core/ecs.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

namespace core {

Entity Domain::Create(StableId stableId) {
    const Entity entity = m_registry.create();
    m_registry.emplace<StableId>(entity, stableId);
    const bool inserted = m_entities.emplace(std::to_underlying(stableId), entity).second;
    PN_ASSERT(inserted, "a stable ID was created twice in one domain");
    return entity;
}

void Domain::Destroy(StableId stableId) {
    const auto found = m_entities.find(std::to_underlying(stableId));
    PN_ASSERT(found != m_entities.end(), "a dangling stable ID inside the state");
    m_registry.destroy(found->second);
    m_entities.erase(found);
}

Entity Domain::Find(StableId stableId) const {
    const auto found = m_entities.find(std::to_underlying(stableId));
    return found == m_entities.end() ? Entity{ entt::null } : found->second;
}

void Domain::SortCanonical() {
    m_registry.sort<StableId>([](StableId lhs, StableId rhs) { return lhs < rhs; });
    const entt::sparse_set& ids = m_registry.storage<StableId>(); // its entities, in stable-ID order
    for (auto [type, storage] : m_registry.storage()) {
        if (&storage != &ids) {
            storage.sort_as(ids.begin(), ids.end());
        }
    }
}

} // namespace core
