#pragma once

/// @file
/// @ingroup game
/// A world's state in one place: the entities, what the NPCs think of each other, their travel and the calendar, so a
/// host can keep one world while a scene runs on another.

#include "core/state.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "game/npcs.hpp"
#include "game/travel.hpp"

#include <optional>
#include <utility>

namespace game {

/// Everything a world is: its entities, relationships and travel, its world clock, the month steps run on it and the
/// player's world pause.
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
    /// The point on the world map kept as where the player's cultivator stands there; none when no point is kept.
    std::optional<core::Vec2> mapPlace;
};

} // namespace game
