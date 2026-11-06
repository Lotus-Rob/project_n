#pragma once

/// @file
/// @ingroup game
/// Components every scene's entities share: where an entity stands and how it is drawn.

#include "core/draw_list.hpp"
#include "core/frame.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

#include "core/generated/atlas.hpp" // by `project_n atlas`

#include <utility>

namespace game {

/// Where an entity stands: its foot point at the previous tick and at this one, in world units.
struct Body {
    /// The foot point at the previous tick.
    core::Vec2 previous{};
    /// The foot point at this tick.
    core::Vec2 current{};

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.previous, self.current);
    }

    /// Whether a value read from a save can be used: finite positions.
    [[nodiscard]] bool Valid() const {
        return core::IsFinite(previous) && core::IsFinite(current);
    }
};

/// How an entity is drawn.
struct Look {
    /// Its sprite in the atlas manifest.
    core::atlas::SpriteId sprite = core::atlas::SpriteId{};
    /// Whether a blob shadow lies under its feet.
    bool shadow = false;
    /// The layer it's drawn in.
    core::Layer layer = core::Layer::World;
    /// How it blends.
    core::Pipeline pipeline = core::Pipeline::Normal;
    /// How much larger than its image it is drawn.
    f32 scale = 1.0F;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.sprite, self.shadow, self.layer, self.pipeline, self.scale);
    }

    /// Whether a value read from a save can be used: a sprite, layer and pipeline that exist, and a
    /// finite, positive scale.
    [[nodiscard]] bool Valid() const {
        return std::to_underlying(sprite) < core::atlas::SPRITES.size() && layer <= core::Layer::Effects &&
               std::to_underlying(pipeline) < core::PIPELINES && std::isfinite(scale) && scale > 0.0F;
    }
};

} // namespace game
