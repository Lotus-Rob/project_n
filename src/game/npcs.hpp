#pragma once

/// @file
/// @ingroup game
/// NPCs in three tiers (design doc 0002 §7.7): active NPCs in the active domain, simulated every
/// tick with their decisions time-sliced; world NPCs in the world domain, simulated only at
/// calendar steps; and aggregate groups, also in the world domain, that stand for individuals
/// nobody has met yet. Moving between tiers keeps an NPC's stable ID. The needs and actions here
/// are placeholders for the engine's machinery until the gameplay doc names the real ones.

#include "core/ecs.hpp"
#include "core/jobs.hpp"
#include "core/state.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace game {

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
/// An active NPC decides once every this many simulation ticks: about 10 times a second.
inline constexpr u32 DECISION_TICKS = 6;

/// The centre of map cell `cell`, in world units.
[[nodiscard]] core::Vec2 CellCentre(u32 cell);
/// The map cell `point` lies in; a point off the map's edge counts as in the nearest edge cell.
[[nodiscard]] u32 CellAt(core::Vec2 point);

/// What a world NPC does with a month.
enum class Action : u8 {
    Rest,      ///< Recovers from fatigue.
    Cultivate, ///< Grows in power, and tires.
    Socialise, ///< Meets another NPC in its cell: both are less lonely, and the other thinks better of it.
};

/// A world or active NPC's hot state: what a calendar step reads every month.
struct Npc {
    /// The map cell it lives in.
    u32 cell = 0;
    /// What it did last month.
    Action action = Action::Rest;
    /// How far its cultivation has come.
    f32 power = 0.0F;
};

/// How much an NPC wants each thing, from 0 (content) to 1 (desperate).
struct Needs {
    /// Fatigue.
    f32 rest = 0.0F;
    /// Loneliness.
    f32 company = 0.0F;
};

/// A tag: the player has met this NPC, so it never folds back into a group.
struct Met {};

/// An aggregate: people in one cell nobody has met, kept as a count until the player comes near.
struct Group {
    /// The map cell they live in.
    u32 cell = 0;
    /// How many people it stands for.
    u32 members = 0;
};

/// Where an active NPC is walking to.
struct Wander {
    /// The point it walks towards, in world units.
    core::Vec2 target{};
};

/// What `holder` thinks of `subject`: one edge of the relationship table.
struct Relationship {
    /// The NPC holding the opinion.
    core::StableId holder{};
    /// The NPC it is about.
    core::StableId subject{};
    /// How much `holder` likes `subject`.
    f32 opinion = 0.0F;
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
    /// creating missing edges at 0 first. `changes` must be sorted by holder, then subject.
    void Merge(std::span<const std::pair<core::StableId, core::StableId>> changes);

    /// Drops every edge holding or naming `npc`, which is leaving the state.
    void Forget(core::StableId npc);

private:
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<Relationship> m_edges{};
    std::vector<Relationship> m_merged{}; // Merge()'s output, swapped in, to keep both capacities
    // NOLINTEND(readability-redundant-member-init)
};

/// Creates a world NPC in `cell`, rested and content.
core::StableId SpawnNpc(core::State& state, u32 cell);
/// Creates an aggregate of `members` people in `cell`.
core::StableId SpawnGroup(core::State& state, u32 cell, u32 members);

/// Moves world NPC `npc` into the active tier, standing at its cell's centre.
void Activate(core::State& state, core::StableId npc);
/// Moves active NPC `npc` back into the world tier, in the cell it stands in.
void Deactivate(core::State& state, core::StableId npc);
/// Turns aggregate `group` into world NPCs in its cell, one per member, and returns their stable IDs.
std::vector<core::StableId> Materialise(core::State& state, core::StableId group);
/// Folds world NPC `npc`, whom the player has never met, into aggregate `group`, forgetting
/// its relationships so none names an ID that is gone.
void Fold(core::State& state, Relationships& relationships, core::StableId npc, core::StableId group);

/// The active tier's decisions, time-sliced: each active NPC picks a new point in its cell to
/// walk to once every DECISION_TICKS ticks, from random numbers keyed on `seed` and `stateCounter`.
void DecideActive(core::Domain& active, u64 seed, u64 stateCounter, u64 simulationTick);
/// The active tier's movement, every tick: each active NPC steps towards its Wander target.
void WalkActive(core::Domain& active);

/// A calendar step of one month over the world tier, forked over fixed-size chunks of NPCs: each
/// NPC scores its actions, takes the best and changes itself; what it does to others are
/// commands, merged after the join in (target, source, emission) order. Its results are the same
/// whatever the job system's worker count. Keeps its buffers between steps.
class MonthStep {
public:
    /// NPCs per chunk: the unit of determinism, independent of the worker count.
    static constexpr u32 CHUNK = 1'024;

    /// Runs one month for every world NPC on `jobs`, from random numbers keyed on the state's seed
    /// and `stateCounter`. On the thread that created `jobs`.
    void Run(core::State& state, Relationships& relationships, core::Jobs& jobs, u64 stateCounter);

private:
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<std::pair<u32, core::StableId>> m_byCell{};                      // (cell, NPC), sorted
    std::vector<std::vector<std::pair<core::StableId, core::StableId>>> m_met{}; // per chunk: (met, by)
    std::vector<std::pair<core::StableId, core::StableId>> m_merged{};
    // NOLINTEND(readability-redundant-member-init)
};

} // namespace game
