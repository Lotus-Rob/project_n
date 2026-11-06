#pragma once

/// @file
/// @ingroup core
/// The game state (design doc 0002 §7.1): its domains, the stable-ID counter, the seed, and the
/// map from stable ID to domain, so a command or relationship can reach an entity in any domain
/// and an entity can move between domains without anything that names it noticing.

#include "core/assert.hpp"
#include "core/ecs.hpp"
#include "core/types.hpp"

#include <cstddef>
#include <optional>
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

} // namespace core
