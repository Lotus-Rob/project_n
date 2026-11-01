#pragma once

/// @file
/// @ingroup core
/// The game state's entities (design doc 0002 §7.1): EnTT behind our names, stable game IDs, and
/// domains that map one to the other and sort into canonical order. The only header that
/// includes EnTT.

#include "core/types.hpp"

#ifdef _MSC_VER
#pragma warning(push)
// C4868 (braced-list evaluation order) comes from EnTT's templates instantiated in our code, where
// /external:W0 no longer applies.
#pragma warning(disable : 4868)
#endif
#include <entt/entity/registry.hpp> // IWYU pragma: export
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <unordered_map>
#include <utility>

namespace core {

/// An entity's game ID, and a component every entity in the state carries: from a counter in the
/// state, never reused, saved with it. Saves, input, view models, relationships, commands, random
/// numbers and time slicing refer to entities by it; EnTT handles never leave a domain or a tick.
enum class StableId : u64 {
};

/// Hands out stable IDs; its count is saved with the state.
struct StableIdCounter {
    /// The ID the next Allocate() returns; 0 is never used, so it can mean "none".
    u64 next = 1;

    /// A new stable ID, never handed out before.
    [[nodiscard]] constexpr StableId Allocate() {
        return StableId{ next++ };
    }
};

/// EnTT's registry: components as plain structs, iterated through views.
using Registry = entt::registry;
/// An EnTT entity handle: valid only inside its domain and within one tick.
using Entity = entt::entity;

/// One domain of the state (the active region, the world, ...): a registry whose entities each
/// carry a StableId, and the map from stable ID to entity.
class Domain {
public:
    Domain() = default;
    Domain(const Domain&) = delete;
    Domain& operator=(const Domain&) = delete;
    /// Moves the domain's entities, keeping their handles valid.
    Domain(Domain&&) = default;
    /// Moves the domain's entities, keeping their handles valid.
    Domain& operator=(Domain&&) = default;
    ~Domain() = default;

    /// The domain's entities and components.
    [[nodiscard]] Registry& Entities() {
        return m_registry;
    }
    /// The domain's entities and components, read-only.
    [[nodiscard]] const Registry& Entities() const {
        return m_registry;
    }

    /// Creates the entity for `stableId`, which must not be in the domain yet.
    Entity Create(StableId stableId);

    /// Destroys the entity for `stableId`, which must be in the domain.
    void Destroy(StableId stableId);

    /// The entity for `stableId`, or `entt::null` when it isn't in the domain (a stale ID from input).
    [[nodiscard]] Entity Find(StableId stableId) const;

    /// Sorts every component storage by stable ID, so iteration no longer depends on the order of
    /// past creations and destructions: done at every save and when a recording starts, and a
    /// load creates entities in this order (design doc 0002 §7.1).
    void SortCanonical();

private:
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer for
    // members whose default constructor is defaulted.
    Registry m_registry{};
    std::unordered_map<u64, Entity> m_entities{}; // looked up only, never iterated
    // NOLINTEND(readability-redundant-member-init)
};

} // namespace core
