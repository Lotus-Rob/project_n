#pragma once

/// @file
/// @ingroup game
/// The game module's random-number purposes (core::Random): one value per kind of use, so no two uses share numbers.

#include "core/types.hpp"

namespace game {

/// An active NPC picking a point to walk to.
inline constexpr u32 PURPOSE_WANDER = 1;
/// An NPC's choice of action at a month step.
inline constexpr u32 PURPOSE_CHOICE = 2;
/// An NPC picking a partner to meet.
inline constexpr u32 PURPOSE_PARTNER = 3;
/// The crystal's aura rolling a hit's value.
inline constexpr u32 PURPOSE_AURA = 4;
/// The debug burst rolling a hit's value.
inline constexpr u32 PURPOSE_BURST = 5;
/// A respawned fox picking where on the ground's edge it appears.
inline constexpr u32 PURPOSE_RESPAWN = 6;
/// A cast drawing its volley's seed (core::Volley::seed), from which the volley draws its own numbers.
inline constexpr u32 PURPOSE_VOLLEY = 7;
/// A world NPC drawing which tile of its city's region it lives on (game/scenes/world.cpp).
inline constexpr u32 PURPOSE_HOME_TILE = 10;
/// A world NPC drawing its starting power (game/npcs.hpp).
inline constexpr u32 PURPOSE_STARTING_POWER = 11;
/// A month's key for its visits, drawn from the game's seed, the state counter and the month's first tick
/// (game/travel.hpp).
inline constexpr u32 PURPOSE_VISIT_MONTH = 12;
/// A world NPC drawing, under its month's key, whether it sets out on a visit, when, for how long and where.
inline constexpr u32 PURPOSE_VISIT = 13;
/// A visit drawing its journey's own seed (core::Stay::seed), from which its stay draws its points.
inline constexpr u32 PURPOSE_JOURNEY_SEED = 14;
// 9 is retired from game (the old world terrain noise): gfx/terrain_draw.cpp keys the world map's tile variants with it, so
// nothing here may reuse it.

} // namespace game
