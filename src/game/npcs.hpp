#pragma once

/// @file
/// @ingroup game
/// NPCs in three tiers: active NPCs in the active domain, simulated every
/// tick with their decisions time-sliced; world NPCs in the world domain, simulated only at
/// calendar steps; and aggregate groups, also in the world domain, that stand for individuals
/// nobody has met yet. Moving between tiers keeps an NPC's stable ID. The needs and actions here
/// are placeholders for the engine's machinery until the gameplay doc names the real ones.

#include "core/archive.hpp"
#include "core/ecs.hpp"
#include "core/jobs.hpp"
#include "core/state.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

#include <cmath>
#include <cstddef>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace game {

class Travel;

/// The domain of the active region: the arena, or the world map around the player.
inline constexpr core::DomainIndex ACTIVE{ 0 };
/// The domain of everyone and everything else.
inline constexpr core::DomainIndex WORLD{ 1 };
/// How many domains the game state has.
inline constexpr std::size_t DOMAINS = 2;

/// Cells in each row of the world map.
inline constexpr u32 MAP_COLUMNS = 64;
/// A map cell's width and height, in world units.
inline constexpr f32 CELL_SIZE = 256.0F;
/// The most people an aggregate Group stands for: materialising it makes one NPC each.
inline constexpr u32 MAX_GROUP_MEMBERS = 1'000'000;
/// An active NPC decides once every this many simulation ticks: about 10 times a second.
inline constexpr u32 DECISION_TICKS = 6;

/// The centre of map cell `cell` of the 64-column grid (MAP_COLUMNS), in world units: for scenes without the world map,
/// where Npc::cell is such a cell (with the map it is a core::WorldMap tile index, whose centre is TileCentre's).
[[nodiscard]] core::Vec2 CellCentre(u32 cell);
/// The 64-column grid's cell `point` lies in; a point off the map's edge counts as in the nearest edge cell. For scenes
/// without the world map only, as CellCentre().
[[nodiscard]] u32 CellAt(core::Vec2 point);

/// An NPC's `home` in a scene with no world map: it lives in no city.
inline constexpr u16 NO_HOME = 0xFFFF;

/// What a world NPC does with a month.
enum class Action : u8 {
    Rest,      ///< Recovers from fatigue.
    Cultivate, ///< Grows in power, and tires.
    Socialise, ///< Meets another NPC in its cell: both are less lonely, and the other thinks better of it.
    Travel,    ///< Is on a journey (game/travel.hpp), or waiting to set out on one: it doesn't socialise.
};

/// A world or active NPC's hot state: what a calendar step reads every month.
struct Npc {
    /// Where it lives: a 64-column grid cell (MAP_COLUMNS) in the arena and the stress arena, a core::WorldMap tile index
    /// (column + row * the map's columns) in a scene with the world map.
    u32 cell = 0;
    /// The place index (core::WorldMap::Places) of the city it belongs to in a scene with the world map, NO_HOME otherwise.
    u16 home = NO_HOME;
    /// What it did last month.
    Action action = Action::Rest;
    /// How far its cultivation has come.
    f32 power = 0.0F;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.cell, self.home, self.action, self.power);
    }

    /// Whether a value read from a save can be used: an action that exists and a finite power.
    [[nodiscard]] bool Valid() const {
        return action <= Action::Travel && std::isfinite(power);
    }
};

/// How much an NPC wants each thing, from 0 (content) to 1 (desperate).
struct Needs {
    /// Fatigue.
    f32 rest = 0.0F;
    /// Loneliness.
    f32 company = 0.0F;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.rest, self.company);
    }

    /// Whether a value read from a save can be used: each need from 0 to 1 (NaN fails).
    [[nodiscard]] bool Valid() const {
        return rest >= 0.0F && rest <= 1.0F && company >= 0.0F && company <= 1.0F;
    }
};

/// A tag: the player has met this NPC, so it never folds back into a group.
struct Met {};

/// Whether the player has met `npc` (it has `Met`), in whichever domain it is in; false for an NPC in neither.
[[nodiscard]] bool IsMet(const core::State& state, core::StableId npc);

/// An aggregate: people in one cell nobody has met, kept as a count until the player comes near.
struct Group {
    /// The map cell they live in.
    u32 cell = 0;
    /// How many people it stands for.
    u32 members = 0;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.cell, self.members);
    }

    /// Whether a value read from a save can be used: at most MAX_GROUP_MEMBERS members, as materialising makes one NPC each.
    [[nodiscard]] bool Valid() const {
        return members <= MAX_GROUP_MEMBERS;
    }
};

/// Where an active NPC is walking to.
struct Wander {
    /// The point it walks towards, in world units.
    core::Vec2 target{};

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.target);
    }

    /// Whether a value read from a save can be used: a finite target.
    [[nodiscard]] bool Valid() const {
        return core::IsFinite(target);
    }
};

/// What `holder` thinks of `subject`: one edge of the relationship table.
struct Relationship {
    /// The NPC holding the opinion.
    core::StableId holder{};
    /// The NPC it is about.
    core::StableId subject{};
    /// How much `holder` likes `subject`.
    f32 opinion = 0.0F;
    /// The month (the calendar's months passed) of their latest meeting.
    u64 lastMet = 0;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.holder, self.subject, self.opinion, self.lastMet);
    }
};

/// How many relationships an NPC keeps: chosen to match its travel journal's length (`JOURNAL_LENGTH`), though each
/// limit changes on its own.
inline constexpr std::size_t RELATIONSHIP_LENGTH = 10;
/// How many it keeps once it has `Met` the player, chosen to match `MET_JOURNAL_LENGTH`.
inline constexpr std::size_t MET_RELATIONSHIP_LENGTH = 50;

/// How many relationships Relationships::Trim() keeps per holder.
struct RelationshipLimits {
    /// For an NPC without `Met`.
    std::size_t kept = RELATIONSHIP_LENGTH;
    /// For an NPC with `Met`; at least `kept`, which Relationships::Trim() relies on to skip looking a holder up.
    std::size_t keptOnceMet = MET_RELATIONSHIP_LENGTH;
};

/// Every relationship as one edge table, sorted by holder then subject, so one NPC's
/// relationships are contiguous; NPCs are named by stable ID, whatever domain they are in.
class Relationships {
public:
    /// Every edge, in order.
    [[nodiscard]] std::span<const Relationship> All() const {
        return m_edges;
    }

    /// `holder`'s relationships, by subject.
    [[nodiscard]] std::span<const Relationship> Of(core::StableId holder) const;

    /// Adds 1 to each (holder, subject)'s opinion, once per pair given (repeats add again),
    /// creating missing edges at 0 first, and sets `lastMet` to `month` on each edge it adds to. `changes` must be sorted
    /// by holder, then subject.
    void Merge(std::span<const std::pair<core::StableId, core::StableId>> changes, u64 month);

    /// Cuts every holder down to the limit for its kind (`limits.keptOnceMet` with `Met` in `state`, else `limits.kept`),
    /// keeping the edges met most recently; among those met the same month the higher opinion, then the lower subject ID.
    /// The edges stay sorted. A holder not in `state`'s domains counts as unmet.
    void Trim(const core::State& state, RelationshipLimits limits = {});

    /// Drops every edge holding or naming `npc`, which is leaving the state.
    void Forget(core::StableId npc);

    /// Writes every edge into `archive` (a Writer or a Hasher).
    template<typename Archive>
    void Save(Archive& archive) const {
        archive(u64{ m_edges.size() });
        for (const Relationship& edge : m_edges) {
            archive(edge);
        }
    }

    /// Replaces the edges with those Save() wrote, read from `archive`. Fails `archive`, changing nothing, on a malformed
    /// table: out of order, repeated, an NPC about itself, an opinion that isn't finite, or an edge last met after month
    /// `months` (the calendar's). A holder over its limit loads whole: the next Trim() cuts it. Whether the NPCs it names
    /// are there is Within()'s to say, once every domain they may be in is read.
    void Load(core::Reader& archive, u64 months);

    /// Whether every NPC an edge names is in `state`, in any of its domains, under a world-range ID (an NPC's, never a
    /// scene entity's, which a scene change destroys).
    [[nodiscard]] bool Within(const core::State& state) const;

private:
    std::vector<Relationship> m_edges;
    std::vector<Relationship> m_merged; // Merge()'s and Trim()'s output, swapped in, to keep both capacities
};

/// The power a world NPC starts with, drawn for its `identity` under `seed`: 80% in [0, 5) (mortals), 18% in [5, 50)
/// (cultivators), 2% in [50, 500) (a rare few strong), uniform within each band. The same for the same arguments. Only the
/// world scene uses it: the arena and the stress arena start every NPC at 0.
[[nodiscard]] f32 StartingPower(u64 seed, core::StableId identity);

/// Creates a world NPC in `cell`, rested and content, belonging to the city of place index `home` (NO_HOME: none), with
/// `power` (StartingPower() in the world scene, 0 elsewhere).
core::StableId SpawnNpc(core::State& state, u32 cell, u16 home = NO_HOME, f32 power = 0.0F);
/// Creates an aggregate of `members` people in `cell`.
core::StableId SpawnGroup(core::State& state, u32 cell, u32 members);

/// Moves world NPC `npc` into the active tier, standing at its cell's centre. Only in a scene without the world map, where
/// the cell is a grid cell (CellCentre()): `npc` has no home (NO_HOME, asserted).
void Activate(core::State& state, core::StableId npc);
/// Moves active NPC `npc` back into the world tier under its stable ID, its needs and Met as they were: one without a home
/// (NO_HOME) in the grid cell it stands in (CellAt()), one homed in a city of the world map on the tile it had.
void Deactivate(core::State& state, core::StableId npc);
/// Turns aggregate `group` into world NPCs in its cell, one per member, homeless (NO_HOME), and returns their stable IDs.
/// Only in a scene without the world map, where a group's cell is a grid cell.
std::vector<core::StableId> Materialise(core::State& state, core::StableId group);
/// Folds world NPC `npc`, whom the player has never met, into aggregate `group`, forgetting its relationships, journeys and
/// journal so none names an ID that is gone.
void Fold(core::State& state, Relationships& relationships, Travel& travel, core::StableId npc, core::StableId group);

/// The active tier's decisions, time-sliced: each active NPC picks a new point in its cell to
/// walk to once every DECISION_TICKS ticks, from random numbers keyed on `seed` and `stateCounter`.
void DecideActive(core::Domain& active, u64 seed, u64 stateCounter, u64 simulationTick);
/// The active tier's movement, every tick: each active NPC steps towards its Wander target.
void WalkActive(core::Domain& active);

/// A calendar step of one month over the world tier, forked over fixed-size chunks of NPCs: each
/// NPC at home scores its actions, takes the best and changes itself; what it does to others are
/// commands, merged after the join in (target, source, emission) order. An NPC whose month is Action::Travel (away on a
/// journey, game/travel.hpp) sits the step out, its action, needs and power as they were, and is nobody's partner. Its
/// results are the same whatever the job system's worker count. Keeps its buffers between steps.
class MonthStep {
public:
    /// NPCs per chunk: the unit of determinism, independent of the worker count.
    static constexpr u32 CHUNK = 1'024;

    /// Runs month `month` for every world NPC on `jobs`, from random numbers keyed on the state's seed
    /// and `stateCounter`. On the thread that created `jobs`. Once `stop` is requested, chunks not
    /// yet started are skipped and nothing is merged or trimmed: the month is left half done, for a game
    /// that is closing. Afterwards no NPC holds more relationships than its limit.
    void Run(
        core::State& state,
        Relationships& relationships,
        core::Jobs& jobs,
        u64 stateCounter,
        u64 month,
        const std::stop_token& stop = {}
    );

private:
    std::vector<std::pair<u32, core::StableId>> m_byCell;                      // (cell, NPC), sorted
    std::vector<std::vector<std::pair<core::StableId, core::StableId>>> m_met; // per chunk: (met, by)
    std::vector<std::pair<core::StableId, core::StableId>> m_merged;
};

} // namespace game
