#pragma once

/// @file
/// @ingroup core
/// The game state: its domains, the stable-ID counters, the seed, and the
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

/// Which counter a new stable ID comes from.
enum class IdRange : u8 {
    /// The game's world (its NPCs and groups): from 1 up, so the world generated from a seed numbers alike whatever scenes
    /// ran before it.
    World,
    /// A scene's own entities (the cultivator, a fight's creatures): from SCENE_ID_BASE up; the counter moves on to the
    /// next state through State::SetSceneIds().
    Scene,
};

/// The first stable ID of the scene range; every world ID is below it.
inline constexpr u64 SCENE_ID_BASE = u64{ 1 } << 62;

/// The stable ID just past the scene range's last: a bound no session reaches, so the scene counter never wraps round
/// to the world's IDs.
inline constexpr u64 SCENE_ID_END = u64{ 1 } << 63;

/// The game state's domains and identity: everything about entities that a save keeps.
class State {
public:
    /// A state of `domains` empty domains, its random numbers drawn from `seed`, both counters at their range's start.
    State(std::size_t domains, u64 seed)
      : m_domains(domains)
      , m_seed{ seed } {
    }

    /// A state being loaded from a save: `domains` empty domains, `seed`, and the world's and the scene's stable-ID
    /// counters as saved, each within its own range; Restore() fills it.
    State(std::size_t domains, u64 seed, StableIdCounter ids, StableIdCounter sceneIds)
      : m_domains(domains)
      , m_seed{ seed }
      , m_ids{ ids }
      , m_sceneIds{ sceneIds } {
        PN_ASSERT(CountersFit(ids, sceneIds), "a stable-ID counter outside its range");
    }

    /// Whether `ids` (the world's counter) and `sceneIds` (the scene's) each have an ID of their own range left to hand
    /// out: the world's next in [1, SCENE_ID_BASE), the scene's in [SCENE_ID_BASE, SCENE_ID_END).
    [[nodiscard]] static constexpr bool CountersFit(StableIdCounter ids, StableIdCounter sceneIds) {
        return ids.next >= 1 && ids.next < SCENE_ID_BASE && sceneIds.next >= SCENE_ID_BASE && sceneIds.next < SCENE_ID_END;
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

    /// The counter new world stable IDs (IdRange::World) come from.
    [[nodiscard]] const StableIdCounter& Ids() const {
        return m_ids;
    }

    /// The counter new scene stable IDs (IdRange::Scene) come from.
    [[nodiscard]] const StableIdCounter& SceneIds() const {
        return m_sceneIds;
    }

    /// Takes over the scene counter `ids` of another state of the same session, so this one never hands out a scene ID
    /// that one did. It never goes down.
    void SetSceneIds(StableIdCounter ids) {
        PN_ASSERT(ids.next >= m_sceneIds.next, "the scene ID counter went down, so scene IDs could repeat");
        m_sceneIds = ids;
    }

    /// Creates an entity with a new stable ID from `range` in domain `index`. Never inside a job chunk.
    std::pair<StableId, Entity> Create(DomainIndex index, IdRange range) {
        PN_ASSERT(range == IdRange::Scene || m_ids.next < SCENE_ID_BASE, "the world IDs ran into the scene range");
        PN_ASSERT(range == IdRange::World || m_sceneIds.next < SCENE_ID_END, "the scene IDs ran past their range");
        const StableId stableId = range == IdRange::World ? m_ids.Allocate() : m_sceneIds.Allocate();
        const bool added = m_where.emplace(std::to_underlying(stableId), index).second;
        PN_ASSERT(added, "a stable ID handed out twice");
        return { stableId, (*this)[index].Create(stableId) };
    }

    /// Creates the entity for `stableId`, read from a save, in domain `index`. Returns false,
    /// creating nothing, when the ID can't be one this state handed out (0, or in neither range's
    /// part below its counter) or is in the state already: the save is malformed.
    [[nodiscard]] bool Restore(DomainIndex index, StableId stableId) {
        const u64 key = std::to_underlying(stableId);
        const bool world = key != 0 && key < m_ids.next;
        const bool scene = key >= SCENE_ID_BASE && key < m_sceneIds.next;
        if (!(world || scene) || !m_where.emplace(key, index).second) {
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
    StableIdCounter m_sceneIds{ .next = SCENE_ID_BASE };
    std::unordered_map<u64, DomainIndex> m_where; // looked up only, never iterated
};

/// The component types a domain saves, in the order it saves them (SaveDomain, LoadDomain).
template<typename... Components>
struct ComponentList {};

namespace detail {

/// Reads a whole `Component` into `value`, failing `archive` when its type's `Valid()` (when it has one) rejects it.
template<typename Component>
void ReadWhole(Reader& archive, Component& value) {
    archive(value);
    if constexpr (requires { value.Valid(); }) {
        if (!archive.Failed() && !value.Valid()) {
            archive.Fail();
        }
    }
}

/// Gives `entity`, which lacks one, its `Component`, read whole from `archive` (nothing to read for an empty one);
/// emplaces nothing when the read fails or the value is invalid.
template<typename Component>
void EmplaceRead(Reader& archive, Registry& registry, Entity entity) {
    if constexpr (std::is_empty_v<Component>) {
        registry.emplace<Component>(entity);
    }
    else {
        Component value{};
        ReadWhole(archive, value);
        if (!archive.Failed()) {
            registry.emplace<Component>(entity, std::move(value));
        }
    }
}

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
        EmplaceRead<Component>(archive, domain.Entities(), entity);
    }
}

} // namespace detail

/// Writes `domain`'s entities, then each of `Components` by stable ID, into `archive` (a Writer
/// or a Hasher), each storage in its own order, so that LoadDomain() rebuilds every storage in
/// the same order and the loaded game iterates exactly as the saved one.
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
