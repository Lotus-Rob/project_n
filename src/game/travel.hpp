#pragma once

/// @file
/// @ingroup game
/// The world's travel: the journeys under way (core::Journey records, where each traveller is being a pure function of
/// its record and the world clock) and each NPC's travel journal, its last journeys, written as each ends. Both are kept
/// apart from the NPCs' hot data, and saved and hashed with the world. Each month a share of the world NPCs set out on
/// visits, to cities weighed by their pull (size and distance), the visitor's reach (its power) and their character (what
/// kind of visitor each draws).

#include "core/archive.hpp"
#include "core/error.hpp"
#include "core/game_data.hpp"
#include "core/jobs.hpp"
#include "core/journey.hpp"
#include "core/paths.hpp"
#include "core/stable_id.hpp"
#include "core/state.hpp"
#include "core/types.hpp"
#include "core/world_map.hpp"
#include "game/npcs.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

namespace game {

/// One journey in an NPC's travel journal.
struct JournalEntry {
    /// The city it travelled to, a place index.
    u16 destination = core::NO_PLACE;
    /// The day it set out on, counted from the world clock's tick 0 (negative for a journey that left before it), held
    /// within an i32's range: a journey more than about 2^31 days from tick 0 counts as that far.
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

/// Whether `leg`, read from a save, can be placed on `map`: its shape and profile exist, it starts from a finite point, its
/// tiles are on the map, one arrive tick each from 0 and rising, and its end tick fits an i64.
[[nodiscard]] bool LegSound(const core::Leg& leg, const core::WorldMap& map);

/// Gives `data` a world map whose travel share (core::TravelRules::sharePerMille) is `sharePerMille`, the rest of the map
/// and its rules alike, so the data's hash follows it; the map it had stays as it was for whoever else holds it. Refused,
/// leaving `data` as it was, for data without a world map or a share past core::MOST_SHARE_PER_MILLE.
[[nodiscard]] std::expected<void, core::Error> SetTravelShare(core::GameData& data, u16 sharePerMille);

/// The kind of visitor an NPC of `power` is: a cultivator from `rules.cultivatorPower` up, a trader below.
[[nodiscard]] core::VisitorKind VisitorOf(const core::TravelRules& rules, f32 power);

/// The profile a traveller of `power` moves by: that of the last of `rules.bands` whose `fromPower` it reaches, the first
/// band's for a power no band reaches (NaN, or below 0), and core::WALKING when there are no bands.
[[nodiscard]] core::ProfileIndex ProfileFor(const core::TravelRules& rules, f32 power);

/// How strongly city `destination` of `map` draws a visitor of `power` (finite) whose walk there costs `cost` (in the
/// profile's cost units, core::UNREACHABLE when it can't get there), in whole numbers so every machine draws alike: the
/// pull (the city's people × 1,000,000 ÷ cost², a cost counted as at least 1), times the reach (max(0, R − cost) × 1,000 ÷
/// R, rounded down, R being `rules.reachBase` plus the power's whole part, at least 0, times `rules.reachPerPower`), times
/// the city's draw for the visitor's kind (VisitorOf()); each product saturates at a u64's most. 0 for an unreachable
/// cost, a cost at or past the reach, or a draw of 0.
[[nodiscard]] u64 DestinationWeight(
    const core::WorldMap& map,
    const core::TravelRules& rules,
    f32 power,
    u32 cost,
    u16 destination
);

/// Where `npc`, a world NPC homed in a city of `map` and standing on one of its tiles, goes on a visit, drawn by `random`:
/// the cities but its home, in place order, each weighed by DestinationWeight() with what the way there from its tile costs
/// by its profile (`paths`, by ProfileFor() of its power), and the city picked whose cumulative weight first passes
/// `random` scaled to the total. None when every weight is 0. Reads `paths` only once that profile's maps are built
/// (Paths::Warm()), so chunks may call it at once after that.
[[nodiscard]] std::optional<u16> ChooseDestination(
    const core::WorldMap& map,
    const core::TravelRules& rules,
    core::Paths& paths,
    const Npc& npc,
    u64 random
);

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

    /// Sends world NPCs of `state` on visits in the month from world tick `monthStart`, by `map`'s travel rules: first it
    /// builds `paths`' maps of every city by core::WALKING and by each band's profile, then, in MonthStep::CHUNK-sized
    /// chunks of the world NPCs forked over `jobs`, each one homed in a city, not travelling and standing on a tile its
    /// profile (ProfileFor() of its power) can enter sets out with a chance of `sharePerMille` in a thousand, to a city
    /// ChooseDestination() picks (none, and it stays, when it reaches none), along its profile's route from its tile
    /// (core::RouteDown()), leaving on a tick drawn uniformly over the month's and staying a number of days drawn uniformly
    /// from the rules' fewest to their most (core::Visit()), so it comes back to its own tile. Every draw is keyed on
    /// `seed`, `stateCounter`, `monthStart` and the NPC (core/random.hpp), so the journeys are the same on any worker
    /// count; they join the journeys under way in their first members' order, and each member's month is Action::Travel
    /// until Finish() ends its journey. On the thread that created `jobs`.
    void Month(
        core::State& state,
        const core::WorldMap& map,
        core::Paths& paths,
        core::Jobs& jobs,
        u64 seed,
        u64 stateCounter,
        i64 monthStart
    );

    /// Gives a newly built world the travel it would have had running a month already: Month() for the month before
    /// `monthStart`, the journeys over by `monthStart` ended (Finish()), then Month() for the month from `monthStart`. Its
    /// chunks run in turn on the calling thread, as a scene's build has no job system: the journeys are those any worker
    /// count would give.
    void Seed(core::State& state, const core::WorldMap& map, core::Paths& paths, u64 seed, u64 stateCounter, i64 monthStart);

    /// Writes the journeys and the journals into `archive` (a Hasher, or a Writer for a test's table), every journey whole.
    template<typename Archive>
    void Save(Archive& archive) const {
        archive(m_journeys, m_journals);
    }

    /// Writes the table into `archive` as its difference from `base` (the table of the world as it was generated): the
    /// places in `base`'s order of its journeys no longer under way exactly as they were, then each other journey, as the
    /// core::VisitPlan that makes it (core::PlanOf()) when, over `map` and `paths`, core::Rebuild() gives it back exactly,
    /// else whole, then the journals whole. Without a map and its paths every other journey is written whole.
    void SaveDelta(core::Writer& archive, const Travel& base, const core::WorldMap* map, core::Paths* paths) const;

    /// Replaces the journeys and journals with those SaveDelta() wrote against `base`, its plans rebuilt over `map` and
    /// `paths` (core::Rebuild()), checked against `state` and `map` (none in a scene without the world map, where the
    /// table must be empty). False, failing `archive` and changing nothing, on a malformed table: base places out of
    /// order, repeated or past `base`'s journeys; a plan with no `map` and `paths` to rebuild it, with no member, a
    /// city that isn't one of `map`, a tile off it, a profile it lacks, a route that is empty (the tile impassable to
    /// the profile or the destination out of its reach), a departure too near an i64's most for its journey's ticks to
    /// fit, or a rebuild that doesn't start at its departure; then, of the table made, journeys out of their first
    /// members' order, a journey without members or parts, a member that isn't a world NPC of `state`, is in two
    /// journeys or is listed twice in one, a city or stay that isn't a city of `map`, parts that don't follow on from
    /// each other, a leg of an unknown shape or profile, with a tile off the map, a non-finite origin, arrive ticks not
    /// one per tile, from 0 and rising, or an end tick past an i64's range, a stay ending before it starts, lasting
    /// more ticks than a u32 holds or ending within a day of an i64's most; journals out of their NPCs' order, empty,
    /// for an NPC not in `state`, longer than its NPC keeps, or naming a city or profile `map` lacks; or months that
    /// don't match the journeys: a member whose month isn't Action::Travel, or an NPC whose month is Action::Travel on
    /// no journey (with or without the map).
    [[nodiscard]] bool LoadDelta(
        core::Reader& archive,
        const Travel& base,
        const core::State& state,
        const core::WorldMap* map,
        core::Paths* paths
    );

private:
    /// Month(), forking its chunks over `jobs`, or running them in turn on the calling thread without one.
    void SendOut(
        core::State& state,
        const core::WorldMap& map,
        core::Paths& paths,
        core::Jobs* jobs,
        u64 seed,
        u64 stateCounter,
        i64 monthStart
    );
    /// Adds `journeys`, none of whose members is travelling, emptying it.
    void StartAll(std::vector<core::Journey>& journeys);

    /// One NPC's journal.
    struct Journal {
        core::StableId npc{};
        std::vector<JournalEntry> entries;

        template<typename Self, typename Archive>
        void Fields(this Self& self, Archive& archive) {
            archive(self.npc, self.entries);
        }
    };

    /// Replaces the journeys and journals with `journeys` and `journals`, read from `archive`, if they pass LoadDelta()'s
    /// checks on the table made.
    [[nodiscard]] bool Adopt(
        core::Reader& archive,
        const core::State& state,
        const core::WorldMap* map,
        std::vector<core::Journey> journeys,
        std::vector<Journal> journals
    );

    std::vector<core::Journey> m_journeys;                          // by first member
    std::vector<Journal> m_journals;                                // by NPC
    std::unordered_set<core::StableId> m_travellers;                // every journey's members; looked up only, never
                                                                    // iterated, so its order can't reach the state
    std::vector<std::pair<core::StableId, JournalEntry>> m_written; // Finish()'s new entries, kept for its capacity
    std::vector<Journal> m_merged;                                  // Finish()'s output, swapped in, as m_written
    std::vector<std::vector<core::Journey>> m_setOut;               // per chunk of a month: the visits its NPCs make
    std::vector<core::Journey> m_departing;                         // a month's visits, gathered in chunk order
};

} // namespace game
