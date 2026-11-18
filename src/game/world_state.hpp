#pragma once

/// @file
/// @ingroup game
/// A world's state in one place: the entities, what the NPCs think of each other, their travel and the calendar, so a
/// host can keep one world while a scene runs on another; how the game's world and a dev scene's are made, and a scene's
/// way into a world.

#include "core/ecs.hpp"
#include "core/game_data.hpp"
#include "core/manifest.hpp"
#include "core/paths.hpp"
#include "core/stable_id.hpp"
#include "core/state.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "game/components.hpp"
#include "game/npcs.hpp"
#include "game/scene.hpp"
#include "game/travel.hpp"

#include <optional>
#include <utility>

namespace game {

/// The version of what GenerateWorld() makes from a seed and the game data: raised whenever generation changes what it
/// makes (StartingPower() included), as a world made by another version is another world.
inline constexpr u32 GENERATOR_VERSION = 1;

/// Everything a world is: its entities, relationships and travel, its world clock, the month steps run on it and the
/// player's world pause.
///
/// Its order is part of what it is, as iteration follows it: a save leaves it as it is and holds it (the active domain
/// written whole in storage order, the world domain's order beside its difference from the world it was made as:
/// core::SaveDomainOrder()), and a load gives it back, so a game that saves runs on exactly as one that never did,
/// whatever was created, destroyed or moved between saves.
struct WorldState {
    /// A world of `worldState`'s entities, with no relationships, no travel, the calendar at its start, not paused.
    /// A constructor rather than an aggregate: core::State has no default, and MSVC warns (C4623) where an aggregate's
    /// default constructor is implicitly deleted.
    explicit WorldState(core::State worldState)
      : state(std::move(worldState)) {
    }

    /// The entities: the scene's in the active domain, the NPCs in the world domain.
    core::State state;
    /// What the NPCs think of each other.
    Relationships relationships;
    /// The NPCs' journeys under way and their travel journals.
    Travel travel;
    /// The world clock: the ticks that ran while the world map showed, unpaused, and no month was due, plus those an
    /// early month step skipped.
    u64 worldTicks = 0;
    /// How many month steps have run.
    u64 months = 0;
    /// Whether the player has paused the world.
    bool paused = false;
    /// Where the player's cultivator comes back to on the world map, kept from when it left the map until it is back
    /// there; none while it stands on the map, or in a game that hasn't left it.
    std::optional<core::Vec2> mapPlace;
};

/// Makes the game's world in `world`, which must be fresh (no entities, its calendar at the start), from its state's seed
/// and `data`: each city of the world map's `people` as world NPCs homed in it, city by city in place order, seven tenths
/// on the city's tile and each of the rest on a tile of its region drawn for its running index, each with StartingPower();
/// then the travel of the month before the calendar's start and of its first month (Travel::Seed(), over `paths`, which
/// must route over that map), so the roads are busy from the first frame. Its state is left in canonical order
/// (core::State::SortCanonical()). Nothing else feeds it, so the same
/// seed, data and GENERATOR_VERSION make the same world, entity for entity, in the same order. Without a world map it makes
/// nothing and `paths` may be null.
void GenerateWorld(WorldState& world, const core::GameData& data, core::Paths* paths);

/// A scene's way into one world (SceneContext): what the scene spawns goes into `world`'s active domain, its creatures
/// as `data` has them, and the cultivator it spawns as the player is kept in `player`. It draws with `manifest`. All four
/// must outlive it.
class WorldContext final : public SceneContext {
public:
    /// The way into `world`, keeping the player in `player`.
    WorldContext(WorldState& world, const core::Manifest& manifest, const core::GameData& data, core::StableId& player)
      : m_world{ &world }
      , m_manifest{ &manifest }
      , m_data{ &data }
      , m_player{ &player } {
    }
    WorldContext(const WorldContext&) = delete;
    WorldContext& operator=(const WorldContext&) = delete;
    WorldContext(WorldContext&&) = delete;
    WorldContext& operator=(WorldContext&&) = delete;
    ~WorldContext() override;

    /// The world's state.
    [[nodiscard]] core::State& State() override {
        return m_world->state;
    }
    /// The world's state, read-only.
    [[nodiscard]] const core::State& State() const override {
        return m_world->state;
    }
    /// The manifest it was given.
    [[nodiscard]] const core::Manifest& Manifest() const override {
        return *m_manifest;
    }
    /// The game data it was given.
    [[nodiscard]] const core::GameData& Data() const override {
        return *m_data;
    }
    /// An entity of the scene's own ID range in the world's active domain, standing at `foot` and drawn as `look`.
    core::Entity Spawn(core::Vec2 foot, const Look& look) override;
    /// A fox as the game data has it: its sprite with a shadow, `orbit`, and full health.
    core::Entity SpawnFox(const Orbit& orbit, core::Vec2 foot) override;
    /// The game data's cultivator with a shadow, full health and its starting Qi, kept as the player.
    core::Entity SpawnPlayer(core::Vec2 foot) override;
    /// The player it keeps.
    [[nodiscard]] core::StableId Player() const override {
        return *m_player;
    }
    /// Combat::Damage() in the world's state.
    void Damage(core::StableId target, i64 amount, u64 tick) override;
    /// The world's place on the map (WorldState::mapPlace).
    [[nodiscard]] std::optional<core::Vec2> MapPlace() const override {
        return m_world->mapPlace;
    }

private:
    WorldState* m_world;
    const core::Manifest* m_manifest;
    const core::GameData* m_data;
    core::StableId* m_player;
};

/// Builds dev scene `scene` into `world`, fresh, which `context` spawns into: the scene's own entities
/// (Scene::Build()), then its `worldNpcs` crowd (SceneTraits::worldNpcs) in the world domain, homeless, the 64-column
/// grid's cells in turn. Its world domain is the same every time, whatever the scene counter and the game data's creatures,
/// so a load makes again (or a host keeps) the world a save of a dev scene's scratch world was written against.
void BuildScratch(WorldState& world, const Scene& scene, SceneContext& context);

} // namespace game
