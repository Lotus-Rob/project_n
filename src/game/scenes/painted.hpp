#pragma once

/// @file
/// @ingroup game
/// The painted scene (design doc 0002 §8.4): a still scene of painted sprites and overlapping glows, for judging art and
/// blending. Nothing in it moves, so the reference pictures draw the same every run.

#include "game/scene.hpp"

namespace game {

/// The painted scene: still sprites with shadows and additive glows over the ground, the camera panned by hand.
class PaintedScene final : public SceneClass {
public:
    /// Named `painted`, viewing the ground, with no creatures and no world NPCs, the camera panned by hand.
    [[nodiscard]] const SceneTraits& Traits() const override;

    /// The painted sprites, each with a shadow, then the glows.
    void Build(SceneContext& context) const override;
};

} // namespace game
