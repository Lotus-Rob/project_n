#pragma once

/// @file
/// @ingroup game
/// Components every scene's entities share: where an entity stands, how it is drawn, and how it turns and circles.

#include "core/draw_list.hpp"
#include "core/ecs.hpp"
#include "core/frame.hpp"
#include "core/manifest.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

#include "core/generated/atlas.hpp" // by `project_n atlas`

#include <cmath>
#include <numbers>
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
        return std::to_underlying(sprite) < core::atlas::SPRITE_NAMES.size() && layer <= core::Layer::Effects &&
               std::to_underlying(pipeline) < core::PIPELINES && std::isfinite(scale) && scale > 0.0F;
    }
};

/// Turns an entity about its centre at a steady rate.
struct Spin {
    /// Its clockwise rotation at the previous tick, in radians.
    f32 previous = 0.0F;
    /// Its rotation at this tick, within half a turn of zero.
    f32 current = 0.0F;
    /// Radians per tick; negative turns anticlockwise.
    f32 speed = 0.0F;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.previous, self.current, self.speed);
    }

    /// Whether a value read from a save can be used: finite angles and speed.
    [[nodiscard]] bool Valid() const {
        return std::isfinite(previous) && std::isfinite(current) && std::isfinite(speed);
    }
};

/// Which kind of creature in the game data (core::GameData) an entity is: its sprite and, when it
/// has an Orbit, its orbit's radius, wobble and speed come from there.
enum class CreatureKind : u8 {
    Cultivator, ///< core::GameData::cultivator
    Fox,        ///< core::GameData::fox
};

/// Marks an entity as a creature of the game data.
struct Creature {
    /// Its kind.
    CreatureKind kind = CreatureKind::Cultivator;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.kind);
    }

    /// Whether a value read from a save can be used: a kind that exists.
    [[nodiscard]] bool Valid() const {
        return kind <= CreatureKind::Fox;
    }
};

/// A full turn, in radians.
inline constexpr f32 TURN = 2.0F * std::numbers::pi_v<f32>;

/// How much flatter than wide a circle on the ground looks: the view looks down at an angle.
inline constexpr f32 VIEW_TILT = 0.6F;

/// A path around a point: the entity's foot circles `centre` at `radius`, a little in and out.
struct Orbit {
    /// The point circled.
    core::Vec2 centre{};
    /// The mean distance from it.
    f32 radius = 0.0F;
    /// How far it strays in and out of that distance, three times a lap.
    f32 wobble = 0.0F;
    /// The angle at tick 0, in radians.
    f32 phase = 0.0F;
    /// Radians per tick; negative goes anticlockwise.
    f32 speed = 0.0F;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.centre, self.radius, self.wobble, self.phase, self.speed);
    }

    /// Whether a value read from a save can be used: finite numbers.
    [[nodiscard]] bool Valid() const {
        return core::IsFinite(centre) && std::isfinite(radius) && std::isfinite(wobble) && std::isfinite(phase) &&
               std::isfinite(speed);
    }
};

/// Where an Orbit puts its entity's foot at `tick`.
[[nodiscard]] core::Vec2 OrbitPoint(const Orbit& orbit, u64 tick);

/// Where `entity`'s head is: its foot raised by its sprite's foot height at its scale. It must have a Body and a Look.
[[nodiscard]] core::Vec2 HeadOf(const core::Registry& entities, const core::Manifest& manifest, core::Entity entity);

} // namespace game
