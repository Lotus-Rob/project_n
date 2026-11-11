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

} // namespace game
