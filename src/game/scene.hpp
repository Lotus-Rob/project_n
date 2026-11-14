#pragma once

/// @file
/// @ingroup game
/// What a scene is to the host that runs it: its traits, the context the host hands it, and the hooks it implements.
/// A scene keeps no state of its own (everything lives in the host's saved state), so one instance of each serves
/// every game. Adding a scene is its class in game/scenes/, and in game/scenes.cpp an instance and its place in the list,
/// with its key constant in game/scenes.hpp.

#include "core/draw_list.hpp"
#include "core/ecs.hpp"
#include "core/frame.hpp"
#include "core/game_data.hpp"
#include "core/glow.hpp"
#include "core/manifest.hpp"
#include "core/state.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "game/components.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace game {

/// The painted ground a scene draws under it unless it draws its own (Scene::PublishGround(); core::atlas::SpriteId::Ground
/// at the origin), 1920 by 1080 world units: what the camera shows whole in a still scene.
inline constexpr core::Rect GROUND{ .min = {}, .max = { .x = 1'920.0F, .y = 1'080.0F } };

/// How far a name label stands from the head it names (core::TextItem::offset), in world units, so it scales with the
/// zoom as the text does (the phase 6 chunk 3 spec): clear of the head, with room between them for a health bar.
inline constexpr core::Vec2 LABEL_OFFSET{ .x = 0.0F, .y = -12.0F };

/// A sprite that wears a demo glow (DemoGlow()) in a scene, and which.
struct DemoGlowSprite {
    /// The sprite.
    core::atlas::SpriteId sprite = core::atlas::SpriteId{};
    /// The glow it wears.
    core::GlowRole role = core::GlowRole::None;
};

/// What the host reads of a scene: plain facts, the same every time.
struct SceneTraits {
    /// Its name, which is also its manifest scene's (art/scenes.toml) and its command line flag's (`--<name>`).
    std::string_view name;
    /// Where a still camera starts; with no player to follow, a publish sent no view yet culls to the widest view a
    /// window shows (core::WIDEST_HALF_VIEW) round its centre.
    core::Rect view{};
    /// Where orbits return when the game data stops them circling the player.
    core::Vec2 home{};
    /// Whether it is a combat scene: casts act, and its foxes show health bars.
    bool combat = false;
    /// Whether it has creatures, which draw the game data's sprites (CheckScenes()).
    bool creatures = false;
    /// The sprites that wear the demo glows (DemoGlow()) and which each wears; empty for a scene without them.
    std::span<const DemoGlowSprite> demoGlows{}; // NOLINT(readability-redundant-member-init): Clang's
                                                 // -Wmissing-designated-field-initializers warns where a scene's traits omit it
    /// How the camera moves over it.
    core::CameraKind camera = core::CameraKind::Pan;
    /// How many world NPCs the host spawns after it builds.
    u32 worldNpcs = 0;
};

/// The host as a scene sees it: the state, what it draws with, and the spawns every scene shares.
class SceneContext {
public:
    SceneContext() = default;
    SceneContext(const SceneContext&) = delete;
    SceneContext& operator=(const SceneContext&) = delete;
    SceneContext(SceneContext&&) = delete;
    SceneContext& operator=(SceneContext&&) = delete;
    virtual ~SceneContext();

    /// The game state: the scene's entities in the active domain, NPCs in the world domain.
    [[nodiscard]] virtual core::State& State() = 0;

    /// The game state, read-only.
    [[nodiscard]] virtual const core::State& State() const = 0;

    /// The atlas manifest the scene draws with.
    [[nodiscard]] virtual const core::Manifest& Manifest() const = 0;

    /// The game data in use.
    [[nodiscard]] virtual const core::GameData& Data() const = 0;

    /// Creates an entity in the active domain standing at `foot`, drawn as `look`.
    virtual core::Entity Spawn(core::Vec2 foot, const Look& look) = 0;

    /// Creates a fox of the game data standing at `foot`, on `orbit`, with its kind's sprite and full health.
    virtual core::Entity SpawnFox(const Orbit& orbit, core::Vec2 foot) = 0;

    /// Creates the game data's cultivator standing at `foot`, with full health and its starting Qi, and makes it the
    /// player's.
    virtual core::Entity SpawnPlayer(core::Vec2 foot) = 0;

    /// The cultivator the player walks, or zero when there is none.
    [[nodiscard]] virtual core::StableId Player() const = 0;

    /// Deals `amount` to `target` at simulation tick `tick` (Combat::Damage()): a negative amount heals.
    virtual void Damage(core::StableId target, i64 amount, u64 tick) = 0;
};

/// One scene: its traits, how it builds, and its own behaviour through hooks the host calls. It holds no state, so its
/// hooks are const and everything they change goes through the context into the host's state.
class Scene {
public:
    /// Its traits, the same every call.
    [[nodiscard]] virtual const SceneTraits& Traits() const = 0;

    /// Spawns its entities through `context` into a fresh state: the same entities, in the same order and places,
    /// every time.
    virtual void Build(SceneContext& context) const = 0;

    /// Its own part of each tick at `counters`, after the active NPCs walk, appending any events it starts to `events`.
    /// Nothing by default.
    virtual void Tick(SceneContext& context, const core::TickCounters& counters, std::vector<core::Event>& events) const;

    /// Brings back a fox that died, the `index`th of those due at `counters`. Nothing by default: a scene without foxes
    /// never has one due.
    virtual void Respawn(SceneContext& context, const core::TickCounters& counters, u32 index) const;

    /// Adds its ground-layer content (core::Layer::Ground) to part `part` of `drawList`, just after the host began it for
    /// `view` (the area the publish culls to), so it draws under every entity. By default the painted ground (GROUND),
    /// whole; a scene that overrides it draws its own ground instead (the world, its tiles).
    virtual void PublishGround(const SceneContext& context, core::Rect view, core::DrawList& drawList, u32 part) const;

    /// Adds its own content to `snapshot` after the host has published the entities, their texts and the caster, and
    /// before the seals raised in play; `view` is the area the host culled to. Nothing by default.
    virtual void Publish(const SceneContext& context, core::Rect view, core::Snapshot& snapshot) const;

protected:
    /// A scene; only the scenes themselves make one.
    Scene() = default;
    /// Copies a scene, which has nothing to copy.
    Scene(const Scene&) = default;
    /// Copies a scene, which has nothing to copy.
    Scene& operator=(const Scene&) = default;
    /// Moves a scene, which has nothing to move.
    Scene(Scene&&) = default;
    /// Moves a scene, which has nothing to move.
    Scene& operator=(Scene&&) = default;
    /// Never deleted through the interface (the scenes are constants of game/scenes.cpp), so not virtual: each scene stays
    /// a constant with nothing to destroy.
    ~Scene() = default;
};

} // namespace game
