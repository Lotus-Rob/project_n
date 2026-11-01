#pragma once

/// @file
/// @ingroup game
/// The demonstration arena (design doc 0002 §2, §16 phase 3): a painted ground with rocks, pines
/// and a shrine, a cultivator, a pack of foxes circling it and glowing orbs circling closer, drawn through the sprite pipeline.
/// Its stress variant adds a crowd and a bullet spiral, about 100,000 instances in all (§8.1).

#include "core/draw_list.hpp"
#include "core/ecs.hpp"
#include "core/frame.hpp"
#include "core/logic.hpp"
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
};

/// Where an Orbit puts its entity's foot at `tick`.
[[nodiscard]] core::Vec2 OrbitPoint(const Orbit& orbit, u64 tick);

/// Which arena to build.
enum class Scene : u8 {
    Arena,  ///< The demonstration arena.
    Stress, ///< The arena with a crowd of 90,000 and a spiral of 10,000 bullets, for profiling.
};

/// The arena scene as a simulation: it moves its creatures each tick and publishes every sprite.
class Arena final : public core::Simulation {
public:
    /// Builds `scene`: the same entities, in the same places, every time.
    explicit Arena(Scene scene = Scene::Arena);

    /// Moves every entity on an Orbit to where it is at `simulationTick`.
    void Tick(u64 simulationTick) override;

    /// Publishes the ground, then a shadow under each entity that casts one, then the world's
    /// entities in foot-y order, then the effects.
    void Publish(core::Snapshot& snapshot) override;

private:
    core::Entity Spawn(core::Vec2 foot, const Look& look);

    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer for
    // members whose default constructor is defaulted.
    core::Domain m_domain{};
    core::StableIdCounter m_ids{};
    core::DrawList m_drawList{};
    // NOLINTEND(readability-redundant-member-init)
};

} // namespace game
