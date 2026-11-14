#pragma once

/// @file
/// @ingroup game
/// The stress arena (design doc 0002 §8.1): the arena's layout with a crowd of 90,000 over the ground, each circling its
/// own spot, and a spiral of 10,000 bullets turning round the cultivator's start, about 100,000 instances in all, and a
/// world tier of 50,000 NPCs, for profiling. No combat scene: casts do nothing there.

#include "core/frame.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"
#include "game/scene.hpp"

namespace game {

/// The stress arena: the arena's layout and its crowds, the camera following the player.
class StressScene final : public Scene {
public:
    /// Named `stress`, viewing the ground, with creatures, the camera following the player, with 50,000 world NPCs and its
    /// portal where the arena's stands (ARENA_PORTAL). A known limit: the crowd covering the ground hides the portal's
    /// ground seal, so only its caption shows (thinning the crowd there would change the scene's replay hashes).
    [[nodiscard]] const SceneTraits& Traits() const override;

    /// The shared layout (game/scenes/arena_layout.hpp), the crowd, the bullet spiral, then the walking NPCs.
    void Build(SceneContext& context) const override;

    /// A fox at the ground's edge (RespawnFox()).
    void Respawn(SceneContext& context, const core::TickCounters& counters, u32 index) const override;

    /// The tablet's caption while there is a player (PublishCaption()).
    void Publish(const SceneContext& context, core::Rect view, core::Snapshot& snapshot) const override;
};

} // namespace game
