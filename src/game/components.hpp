#pragma once

/// @file
/// @ingroup game
/// Components every scene's entities share: where an entity stands and how it is drawn.

#include "core/draw_list.hpp"
#include "core/frame.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

#include "core/generated/atlas.hpp" // by `project_n atlas`

namespace game {

/// Where an entity stands: its foot point at the previous tick and at this one, in world units.
struct Body {
    /// The foot point at the previous tick.
    core::Vec2 previous{};
    /// The foot point at this tick.
    core::Vec2 current{};
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
};

} // namespace game
