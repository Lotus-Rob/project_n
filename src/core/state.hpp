#pragma once

/// @file
/// @ingroup core
/// The game state (design doc 0002 §7.1): its domains, the stable-ID counter, the seed, and the
/// map from stable ID to domain, so a command or relationship can reach an entity in any domain
/// and an entity can move between domains without anything that names it noticing.

#include "core/archive.hpp"
#include "core/assert.hpp"
#include "core/ecs.hpp"
#include "core/types.hpp"

#include <cstddef>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core {

/// Which of the state's domains: an index the game gives its domains a name for (the active
/// region, the world, ...).
enum class DomainIndex : u8 {
};

/// The game state's domains and identity: everything about entities that a save keeps.
class State {
public:
    /// A state of `domains` empty domains, its random numbers drawn from `seed`.
    State(std::size_t domains, u64 seed)
      : m_domains(domains)
      , m_seed{ seed } {
    }

    /// A state being loaded from a save: `domains` empty domains, `seed`, and the stable-ID counter
    /// as saved; Restore() fills it.
    State(std::size_t domains, u64 seed, StableIdCounter ids)
      : m_domains(domains)
      , m_seed{ seed }
      , m_ids{ ids } {
    }

    /// The domain `index`, which must exist.
    [[nodiscard]] Domain& operator[](DomainIndex index) {
        return m_domains.at(std::to_underlying(index));
    }
    /// The domain `index`, read-only.
    [[nodiscard]] const Domain& operator[](DomainIndex index) const {
        return m_domains.at(std::to_underlying(index));
    }

    /// How many domains it has.
    [[nodiscard]] std::size_t Domains() const {
        return m_domains.size();
    }

    /// The seed every random number in the state is drawn from (core::Random).
    [[nodiscard]] u64 Seed() const {
        return m_seed;
    }

    /// The counter new stable IDs come from.
    [[nodiscard]] const StableIdCounter& Ids() const {
        return m_ids;
    }

    /// Creates an entity with a new stable ID in domain `index`. Never inside a job chunk.
    std::pair<StableId, Entity> Create(DomainIndex index) {
        const StableId stableId = m_ids.Allocate();
        m_where.emplace(std::to_underlying(stableId), index);
        return { stableId, (*this)[index].Create(stableId) };
    }

    /// Creates the entity for `stableId`, read from a save, in domain `index`. Returns false,
    /// creating nothing, when the ID can't be one this state handed out (0, or not below the
    /// counter) or is in the state already: the save is malformed.
    [[nodiscard]] bool Restore(DomainIndex index, StableId stableId) {
        const u64 key = std::to_underlying(stableId);
        if (key == 0 || key >= m_ids.next || !m_where.emplace(key, index).second) {
            return false;
        }
        static_cast<void>((*this)[index].Create(stableId));
        return true;
    }

    /// Destroys `stableId`'s entity, which must be in the state. Never inside a job chunk.
    void Destroy(StableId stableId) {
        const auto found = m_where.find(std::to_underlying(stableId));
        PN_ASSERT(found != m_where.end(), "a dangling stable ID inside the state");
        (*this)[found->second].Destroy(stableId);
        m_where.erase(found);
    }

    /// The domain `stableId`'s entity is in, or nothing when it isn't in the state (a stale ID).
    [[nodiscard]] std::optional<DomainIndex> DomainOf(StableId stableId) const {
        const auto found = m_where.find(std::to_underlying(stableId));
        return found == m_where.end() ? std::nullopt : std::optional{ found->second };
    }

    /// Moves `stableId`'s entity, which must be in the state, into domain `target`: it keeps its
    /// stable ID and gets a new entity there. `transfer(const Registry& from, Entity old, Registry&
    /// into, Entity moved)` derives its components in `target` from those it had, before the old entity
    /// is destroyed. Returns the new entity. Never inside a job chunk.
    template<typename Transfer>
    Entity Move(StableId stableId, DomainIndex target, Transfer&& transfer) {
        const auto found = m_where.find(std::to_underlying(stableId));
        PN_ASSERT(found != m_where.end(), "a dangling stable ID inside the state");
        PN_ASSERT(found->second != target, "an entity moved into the domain it is in");
        Domain& from = (*this)[found->second];
        Domain& into = (*this)[target];
        const Entity moved = into.Create(stableId);
        std::forward<Transfer>(transfer)(std::as_const(from).Entities(), from.Find(stableId), into.Entities(), moved);
        from.Destroy(stableId);
        found->second = target;
        return moved;
    }

    /// Sorts every domain into canonical order (Domain::SortCanonical).
    void SortCanonical() {
        for (Domain& domain : m_domains) {
            domain.SortCanonical();
        }
    }

private:
    std::vector<Domain> m_domains;
    u64 m_seed;
    StableIdCounter m_ids{};
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::unordered_map<u64, DomainIndex> m_where{}; // looked up only, never iterated
};

/// The component types a domain saves, in the order it saves them (SaveDomain, LoadDomain).
template<typename... Components>
struct ComponentList {};

namespace detail {

/// Writes `registry`'s `Component` storage into `archive`, in storage order: its size, then each
/// entity's stable ID and (unless the component is empty) its value.
template<typename Component, typename Archive>
void SaveStorage(Archive& archive, const Registry& registry) {
    const auto* storage = registry.storage<Component>();
    const u64 count = storage == nullptr ? 0 : storage->size();
    archive(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Entity entity = (*storage)[i];
        archive(registry.get<StableId>(entity));
        if constexpr (!std::is_empty_v<Component>) {
            archive(storage->get(entity));
        }
    }
}

/// Reads what SaveStorage() wrote into `domain`, emplacing in the same order; fails `archive` on
/// anything malformed (LoadDomain()).
template<typename Component>
void LoadStorage(Reader& archive, Domain& domain) {
    u64 count = 0;
    archive(count);
    if (count > archive.Remaining() / sizeof(StableId)) {
        archive.Fail();
    }
    for (u64 i = 0; i < count && !archive.Failed(); ++i) {
        StableId stableId{};
        archive(stableId);
        const Entity entity = domain.Find(stableId);
        if (archive.Failed() || entity == Entity{ entt::null } || domain.Entities().all_of<Component>(entity)) {
            archive.Fail();
            return;
        }
        if constexpr (std::is_empty_v<Component>) {
            domain.Entities().emplace<Component>(entity);
        }
        else {
            Component value{};
            archive(value);
            if constexpr (requires { value.Valid(); }) {
                if (!value.Valid()) {
                    archive.Fail();
                }
            }
            if (!archive.Failed()) {
                domain.Entities().emplace<Component>(entity, value);
            }
        }
    }
}

} // namespace detail

/// Writes `domain`'s entities, then each of `Components` by stable ID, into `archive` (a Writer
/// or a Hasher), each storage in its own order, so that LoadDomain() rebuilds every storage in
/// the same order and the loaded game iterates exactly as the saved one (design doc 0002 §7.1).
/// A component missing from the list is neither saved nor hashed.
template<typename Archive, typename... Components>
void SaveDomain(Archive& archive, const Domain& domain, ComponentList<Components...> /*components*/) {
    const Registry& registry = domain.Entities();
    const auto* ids = registry.storage<StableId>();
    const u64 count = ids == nullptr ? 0 : ids->size();
    archive(count);
    for (std::size_t i = 0; i < count; ++i) {
        archive(ids->get((*ids)[i]));
    }
    (detail::SaveStorage<Components>(archive, registry), ...);
}

/// Reads what SaveDomain() wrote into domain `index` of `state`, which must be empty, creating
/// its entities through State::Restore(). Fails `archive` on anything malformed: an ID out of
/// range or repeated, a component for an entity not in the domain or given twice, or a value its
/// type's `Valid()` (when it has one) rejects. A state that failed to load is to be thrown away.
template<typename... Components>
void LoadDomain(Reader& archive, State& state, DomainIndex index, ComponentList<Components...> /*components*/) {
    u64 count = 0;
    archive(count);
    if (count > archive.Remaining() / sizeof(StableId)) {
        archive.Fail();
    }
    for (u64 i = 0; i < count && !archive.Failed(); ++i) {
        StableId stableId{};
        archive(stableId);
        if (!archive.Failed() && !state.Restore(index, stableId)) {
            archive.Fail();
        }
    }
    (detail::LoadStorage<Components>(archive, state[index]), ...);
}

} // namespace core
