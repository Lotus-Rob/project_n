#pragma once

/// @file
/// @ingroup game
/// The world's travel: the journeys under way (core::Journey records, where each traveller is being a pure function of
/// its record and the world clock) and each NPC's travel journal, its last journeys, written as each ends. Both are kept
/// apart from the NPCs' hot data, and saved and hashed with the world.

#include "core/archive.hpp"
#include "core/journey.hpp"
#include "core/stable_id.hpp"
#include "core/state.hpp"
#include "core/types.hpp"
#include "core/world_map.hpp"

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace game {

/// One journey in an NPC's travel journal.
struct JournalEntry {
    /// The city it travelled to, a place index.
    u16 destination = core::NO_PLACE;
    /// The day it set out on, counted from the world clock's tick 0 (negative for a journey that left before it).
    i32 departureDay = 0;
    /// The days from the day it set out to the day it ended, at most 255.
    u8 daysAway = 0;
    /// The profile its first leg moved by.
    core::ProfileIndex profile = core::WALKING;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.destination, self.departureDay, self.daysAway, self.profile);
    }
};

/// How many journeys an NPC's journal keeps, the oldest dropped first.
inline constexpr std::size_t JOURNAL_LENGTH = 10;
/// How many journeys the journal of an NPC the player has met (Met) keeps.
inline constexpr std::size_t MET_JOURNAL_LENGTH = 50;

/// The journeys under way and every NPC's travel journal. An NPC is a member of at most one journey.
class Travel {
public:
    /// The journeys under way, in their first members' order.
    [[nodiscard]] std::span<const core::Journey> Journeys() const {
        return m_journeys;
    }

    /// Adds `journey`, which has at least one member and one part, and none of whose members is travelling already.
    void Start(core::Journey journey);

    /// Whether `npc` is a member of a journey under way (or waiting to leave).
    [[nodiscard]] bool Travelling(core::StableId npc) const;

    /// Ends every journey whose end (core::EndTick()) is at or before `tick`: each member's journal gains an entry for it
    /// (the oldest dropped past JOURNAL_LENGTH, or MET_JOURNAL_LENGTH for a member with Met), each member's month goes back
    /// to Action::Rest, and the journey is dropped. Its members are world NPCs of `state`.
    void Finish(i64 tick, core::State& state);

    /// `npc`'s journal, oldest first; empty for an NPC that has never ended a journey.
    [[nodiscard]] std::span<const JournalEntry> JournalOf(core::StableId npc) const;

    /// Drops `npc`, which is leaving the state, from every journey (a journey left without members ends unwritten) and
    /// drops its journal.
    void Forget(core::StableId npc);

    /// Writes the journeys and the journals into `archive` (a Writer or a Hasher).
    template<typename Archive>
    void Save(Archive& archive) const {
        archive(m_journeys, m_journals);
    }

    /// Replaces the journeys and journals with those Save() wrote, read from `archive`, checked against `state` and `map`
    /// (none in a scene without the world map, where both must be empty). False, failing `archive` and changing nothing,
    /// on a malformed table: journeys out of their first members' order, a journey without members or parts, a member that
    /// isn't a world NPC of `state` or is in two journeys, a city or stay that isn't a city of `map`, parts that don't
    /// follow on from each other, a leg of an unknown shape or profile, with a tile off the map, a non-finite origin, or
    /// arrive ticks not one per tile, from 0 and rising, a stay ending before it starts; journals out of their NPCs' order,
    /// empty, for an NPC not in `state`, longer than its NPC keeps, or naming a city or profile `map` lacks.
    [[nodiscard]] bool Load(core::Reader& archive, const core::State& state, const core::WorldMap* map);

private:
    /// One NPC's journal.
    struct Journal {
        core::StableId npc{};
        // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
        std::vector<JournalEntry> entries{};

        template<typename Self, typename Archive>
        void Fields(this Self& self, Archive& archive) {
            archive(self.npc, self.entries);
        }
    };

    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<core::Journey> m_journeys{};                          // by first member
    std::vector<Journal> m_journals{};                                // by NPC
    std::vector<core::StableId> m_travellers{};                       // every journey's members, sorted; not saved
    std::vector<std::pair<core::StableId, JournalEntry>> m_written{}; // Finish()'s new entries, kept for its capacity
    std::vector<Journal> m_merged{};                                  // Finish()'s output, swapped in, as m_written
    // NOLINTEND(readability-redundant-member-init)
};

} // namespace game
