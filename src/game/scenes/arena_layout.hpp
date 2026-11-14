#pragma once

/// @file
/// @ingroup game
/// What the arena and its stress variant share: the props and painted art on the ground, the player's cultivator, the
/// foxes circling it and the orbs circling its start, the NPCs walking about, the foxes' respawns at the ground's edge
/// and the tablet's caption.

#include "core/frame.hpp"
#include "core/glow.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "game/scene.hpp"

#include <array>

namespace game {

/// Where the player's cultivator starts, which its foxes circle wherever it walks and its orbs circle for good.
inline constexpr core::Vec2 ARENA_START{ .x = 960.0F, .y = 620.0F };

/// Where the ice crystal stands, whose aura reaches round it in the arena.
inline constexpr core::Vec2 CRYSTAL_FOOT{ .x = 420.0F, .y = 620.0F };

/// The arena's demo glows (SceneTraits::demoGlows): its layout holds one prop of each of these sprites, the shrine, the
/// stele and the ice crystal, so each glow shows once.
inline constexpr std::array<DemoGlowSprite, 3> ARENA_DEMO_GLOWS{ {
    { .sprite = core::atlas::SpriteId::Shrine, .role = core::GlowRole::DemoPulse },
    { .sprite = core::atlas::SpriteId::Stele, .role = core::GlowRole::DemoCharge },
    { .sprite = core::atlas::SpriteId::CrystalIce, .role = core::GlowRole::DemoProgress },
} };

/// Spawns the props and painted art, the turning ice crystal, the cultivator (made the player), its 12 foxes and 8
/// orbs, in that order.
void BuildArenaLayout(SceneContext& context);

/// Spawns the 6 NPCs walking about the ground in the active domain, each in its own map cell.
void SpawnArenaNpcs(SceneContext& context);

/// Spawns the `index`th fox due at `counters` at a random point on the ground's edge, arriving into an orbit round
/// the player (or the start, when there is none or the data stops foxes circling it) whose angle starts on that side,
/// so it eases in rather than crossing the circle.
void RespawnFox(SceneContext& context, const core::TickCounters& counters, u32 index);

/// Adds the tablet's vertical caption, beside its right edge, to `snapshot`'s texts while there is a player.
void PublishCaption(const SceneContext& context, core::Snapshot& snapshot);

} // namespace game
