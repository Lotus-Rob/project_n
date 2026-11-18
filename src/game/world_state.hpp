#pragma once

/// @file
/// @ingroup game
/// A world's state in one place: the entities, what the NPCs think of each other, their travel and the calendar, so a
/// host can keep one world while a scene runs on another.

#include "core/game_data.hpp"
#include "core/paths.hpp"
#include "core/state.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "game/npcs.hpp"
#include "game/travel.hpp"

#include <optional>
#include <utility>

namespace game {

/// The version of what GenerateWorld() makes from a seed and the game data: raised whenever generation changes what it
/// makes (StartingPower() included), as a world made by another version is another world.
inline constexpr u32 GENERATOR_VERSION = 1;

/// Everything a world is: its entities, relationships and travel, its world clock, the month steps run on it and the
/// player's world pause.
///
/// Its order is part of what it is: a save sorts the state into canonical order and a load gives it back sorted, and
/// GenerateWorld() leaves it sorted, so a game that saves runs on exactly as one that never did only while nothing
/// creates, destroys or moves an entity of the game's world between saves, outside GenerateWorld() and a load. The month
/// step and travel change components only, and no game scene brings an NPC into its own domain yet, so a scene build's
/// Deactivate() moves none back. A change that does (folding or materialising NPCs in a game scene) must leave the
/// world in canonical order again (core::State::SortCanonical()) before the tick ends.
struct WorldState {
    /// A world of `worldState`'s entities, with no relationships, no travel, the calendar at its start, not paused.
    /// A constructor rather than an aggregate: core::State has no default, and MSVC warns (C4623) where an aggregate's
    /// default constructor is implicitly deleted.
    explicit WorldState(core::State worldState)
      : state(std::move(worldState)) {
    }

    /// The entities: the scene's in the active domain, the NPCs in the world domain.
    core::State state;
    /// What the NPCs think of each other.
    Relationships relationships;
    /// The NPCs' journeys under way and their travel journals.
    Travel travel;
    /// The world clock: the ticks that ran while the world map showed, unpaused, and no month was due, plus those an
    /// early month step skipped.
    u64 worldTicks = 0;
    /// How many month steps have run.
    u64 months = 0;
    /// Whether the player has paused the world.
    bool paused = false;
    /// Where the player's cultivator comes back to on the world map, kept from when it left the map until it is back
    /// there; none while it stands on the map, or in a game that hasn't left it.
    std::optional<core::Vec2> mapPlace;
};

/// Makes the game's world in `world`, which must be fresh (no entities, its calendar at the start), from its state's seed
/// and `data`: each city of the world map's `people` as world NPCs homed in it, city by city in place order, seven tenths
/// on the city's tile and each of the rest on a tile of its region drawn for its running index, each with StartingPower();
/// then the travel of the month before the calendar's start and of its first month (Travel::Seed(), over `paths`, which
/// must route over that map), so the roads are busy from the first frame. Its state is left in canonical order
/// (core::State::SortCanonical()), as a save leaves it (WorldState says what keeps it so). Nothing else feeds it, so the same
/// seed, data and GENERATOR_VERSION make the same world, entity for entity, in the same order. Without a world map it makes
/// nothing and `paths` may be null.
void GenerateWorld(WorldState& world, const core::GameData& data, core::Paths* paths);

} // namespace game
