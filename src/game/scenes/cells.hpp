#pragma once

/// @file
/// @ingroup game
/// The cell grid (design doc 0002 §16 phase 3): a still grid of 640 map cells round the ground's middle, wider than the
/// view at the smallest zoom, for judging the mips as the view zooms out.

#include "game/scene.hpp"

namespace game {

/// The cell grid: 32 by 20 map cells on the ground layer, the camera panned by hand.
class CellsScene final : public Scene {
public:
    /// Named `cells`, viewing the whole grid, with no creatures and no world NPCs, the camera panned by hand.
    [[nodiscard]] const SceneTraits& Traits() const override;

    /// The cells, row by row, the five kinds taking turns so equal cells don't line up in columns.
    void Build(SceneContext& context) const override;
};

} // namespace game
