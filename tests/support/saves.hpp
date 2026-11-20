#pragma once

/// @file
/// Host saves built by hand for tests, laid out as game::Host::Save writes one, so a test can load a state no run reaches
/// cheaply (a world clock at a month's end, a refused mix of components). The format and versions must follow
/// game/host.cpp's, or every load of them is refused for the version alone. The game's world is written as its
/// difference from the world the loading game generates, so each save made for a game with the world map generates
/// that world once: by default the small test map's (SmallData()), made in a moment; a dev scene's scratch world as its
/// difference from the world the scene's entry build makes, so each such save builds that once.

#include "core/abilities.hpp"
#include "core/archive.hpp"
#include "core/delta.hpp"
#include "core/frame.hpp"
#include "core/game_data.hpp"
#include "core/hit.hpp"
#include "core/journey.hpp"
#include "core/manifest.hpp"
#include "core/paths.hpp"
#include "core/seal.hpp"
#include "core/stable_id.hpp"
#include "core/state.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "core/volley.hpp"
#include "game/combat.hpp"
#include "game/components.hpp"
#include "game/npcs.hpp"
#include "game/scene.hpp"
#include "game/scenes.hpp"
#include "game/travel.hpp"
#include "game/world_state.hpp"
#include "support/manifest.hpp"
#include "support/world.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace test {

/// The host's save format (game/host.cpp's ARENA_FORMAT).
inline constexpr u32 HOST_FORMAT = 10;
/// The active domain's version in a host save (5: the NPCs' homes).
inline constexpr u32 ACTIVE_VERSION = 5;
/// The world domain's version in a host save (7: the explored tiles and the sight radius).
inline constexpr u32 WORLD_VERSION = 7;
/// The active domain's saved components, in the host's order.
using ActiveComponents = core::ComponentList<
    game::Body,
    game::Look,
    game::Orbit,
    game::Spin,
    game::Creature,
    game::Npc,
    game::Needs,
    game::Met,
    game::Wander,
    game::Health,
    game::Dying,
    game::Arriving,
    game::Caster,
    game::Dashing,
    game::Afflictions>;
/// The world domain's saved components, in the host's order.
using WorldComponents = core::ComponentList<game::Npc, game::Needs, game::Met, game::Group>;

/// The seed of the empty state WorldSaveAt() saves, and of the empty game world ArenaSave() writes beside a dev scene's.
inline constexpr u64 EMPTY_SAVE_SEED = 7;

/// A volley as game::Combat saves one: the cast, then its targets.
struct SavedVolley {
    /// The cast.
    core::Volley volley{};
    /// Its targets.
    std::vector<core::VolleyTarget> targets;
};

/// The world clock, the month steps run, the world pause, the sight radius and the explored tiles, as a save holds them.
struct SavedClock {
    /// The world clock (game::Host::WorldTicks()).
    u64 ticks = 0;
    /// The month steps run (game::Host::Months()).
    u64 months = 0;
    /// The world pause (game::Host::WorldPaused()).
    bool paused = false;
    /// The sight radius (game::Host::SightRadius()); none for the world's base's: the game data's in the game's world, 0 in
    /// a scratch world.
    std::optional<u32> sight = std::nullopt;
    /// The explored tiles' words (game::Host::ExploredTiles()); none for the world's base's: none explored, sized to the map
    /// in the game's world, no words in a scratch world.
    std::optional<std::vector<u64>> explored = std::nullopt;
};

/// Writes the clock, the world pause, `place` as the place kept on the world map (none if empty), `sight` and `explored`,
/// as both world sections of a host save hold them.
inline void WriteClock(
    core::Writer& writer,
    const SavedClock& clock,
    std::optional<core::Vec2> place,
    u32 sight,
    const std::vector<u64>& explored
) {
    writer(clock.ticks, clock.months, clock.paused, place.has_value());
    if (place) {
        writer(*place);
    }
    writer(sight, explored);
}

/// The world dev scene `scene`'s entry build makes on `data` (game::BuildScratch()), which a save of its scratch world
/// is written against, its scene's own entities drawing IDs from `sceneIds`.
[[nodiscard]] inline game::WorldState ScratchBase(
    core::SceneKey scene,
    const core::GameData& data,
    core::StableIdCounter sceneIds = { .next = core::SCENE_ID_BASE }
) {
    game::WorldState built{ core::State{ game::DOMAINS, EMPTY_SAVE_SEED, core::StableIdCounter{ .next = 1 }, sceneIds } };
    core::StableId player{};
    const std::shared_ptr<const core::Manifest> manifest = TestManifest();
    game::WorldContext context{ built, *manifest, data, player };
    game::BuildScratch(built, game::SceneOf(scene), context);
    return built;
}

/// Writes dev scene `scene`'s scratch world's section of a host save into `writer`, as its difference from the world the
/// scene's entry build makes on `data`: the world stable-ID counter (`state`'s, or the built world's where that is further
/// on, as IDs are never handed out twice), `clock`, no place on the world map, then `state`'s world domain after
/// `worldVersion` and in its own order, `relationships`, then `travel` (each journey whole, as no base has any).
inline void WriteScratchSection(
    core::Writer& writer,
    core::SceneKey scene,
    const core::GameData& data,
    const core::State& state,
    u32 worldVersion,
    const SavedClock& clock,
    const game::Travel& travel,
    const game::Relationships& relationships = game::Relationships{}
) {
    const game::WorldState base = ScratchBase(scene, data);
    writer(std::max(state.Ids().next, base.state.Ids().next));
    WriteClock(writer, clock, std::nullopt, clock.sight.value_or(base.sightRadius), clock.explored.value_or(base.explored));
    writer(worldVersion);
    core::SaveDomainDelta(writer, base.state[game::WORLD], state[game::WORLD], WorldComponents{});
    core::SaveDomainOrder(writer, base.state[game::WORLD], state[game::WORLD], WorldComponents{});
    relationships.Save(writer);
    travel.SaveDelta(writer, game::Travel{}, nullptr, nullptr);
}

/// The world a new game on `data` makes from `seed`, its journeys routed over `paths` (null without a map).
[[nodiscard]] inline game::WorldState GeneratedWorld(const core::GameData& data, u64 seed, core::Paths* paths) {
    game::WorldState world{ core::State{ game::DOMAINS, seed } };
    game::GenerateWorld(world, data, paths);
    return world;
}

/// Writes the game's world's section of a host save into `writer`, as its difference from the world a game on `data`
/// generates from `state`'s seed: the seed, game::GENERATOR_VERSION and `data`'s base hash, the world stable-ID counter
/// (`state`'s, or the generated world's where that is further on, as IDs are never handed out twice), `clock` (by default
/// the generated world's sight and explored tiles: none), `place` as the place kept on the world map (none if empty), then
/// `state`'s world domain after `worldVersion` (in its own order), `relationships` and `travel`, each against the generated
/// world's.
inline void WriteGameWorldSection(
    core::Writer& writer,
    const core::State& state,
    const core::GameData& data,
    u32 worldVersion,
    const SavedClock& clock,
    const game::Travel& travel,
    const game::Relationships& relationships = game::Relationships{},
    std::optional<core::Vec2> place = std::nullopt
) {
    std::optional<core::Paths> paths = std::nullopt;
    if (data.world != nullptr) {
        paths.emplace(data.world);
    }
    core::Paths* const routes = paths ? &*paths : nullptr;
    const game::WorldState base = GeneratedWorld(data, state.Seed(), routes);
    writer(state.Seed(), game::GENERATOR_VERSION, data.BaseHash(), std::max(state.Ids().next, base.state.Ids().next));
    WriteClock(writer, clock, place, clock.sight.value_or(base.sightRadius), clock.explored.value_or(base.explored));
    writer(worldVersion);
    core::SaveDomainDelta(writer, base.state[game::WORLD], state[game::WORLD], WorldComponents{});
    core::SaveDomainOrder(writer, base.state[game::WORLD], state[game::WORLD], WorldComponents{});
    relationships.Save(writer);
    travel.SaveDelta(writer, base.travel, data.world.get(), routes);
}

/// What the game's world, set aside and otherwise empty, holds in a dev scene's save that ArenaSave() writes.
struct SetAside {
    /// The world pause.
    bool paused = false;
    /// The place kept on the world map, if any.
    std::optional<core::Vec2> place = std::nullopt;
};

/// A save of `state` in `scene` with `player`: the format, the scene and the scene stable-ID counter; then `state`'s world
/// domain, `clock`, `travel` and `relationships` as the game's world (WriteGameWorldSection(), after `worldVersion`) in a
/// game scene, or as the scratch world (WriteScratchSection(), against the scene's entry build on the same data) in a dev
/// scene after the game's world, empty but for
/// `setAside`; then the player, the selection (none), the active domain after `activeVersion`, the player's `route` if
/// any, no respawns waiting, `volley` if any (the flying swords' hits of 20, none resolved yet), `flash` if any and `seal`
/// if any. The game's world is written against the world a game on `data` generates, or, when null, on the small test data
/// (SmallData()) for a scene showing the world map and on game::BuiltInData() otherwise: a game on other data refuses the
/// save for its base.
[[nodiscard]] inline std::vector<std::byte> ArenaSave(
    const core::State& state,
    core::StableId player = {},
    const std::optional<SavedVolley>& volley = std::nullopt,
    const std::optional<core::Flash>& flash = std::nullopt,
    const std::optional<core::Seal>& seal = std::nullopt,
    core::SceneKey scene = game::ARENA_SCENE,
    u32 worldVersion = WORLD_VERSION,
    u32 activeVersion = ACTIVE_VERSION,
    const SavedClock& clock = {},
    const game::Travel& travel = game::Travel{},
    const std::optional<core::Journey>& route = std::nullopt,
    const game::Relationships& relationships = game::Relationships{},
    const SetAside& setAside = {},
    const core::GameData* data = nullptr
) {
    constexpr u64 RESPAWNS = 0;
    const game::SceneTraits& traits = game::SceneOf(scene).Traits();
    const core::GameData baseData = [&]() -> core::GameData {
        if (data != nullptr) {
            return *data;
        }
        return traits.worldMap ? SmallData() : game::BuiltInData();
    }();
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    writer(HOST_FORMAT, static_cast<u8>(std::to_underlying(scene)), state.SceneIds().next);
    if (traits.devScene) {
        const core::State empty{ game::DOMAINS, EMPTY_SAVE_SEED };
        const SavedClock asideClock{ .paused = setAside.paused };
        WriteGameWorldSection(
            writer, empty, baseData, WORLD_VERSION, asideClock, game::Travel{}, game::Relationships{}, setAside.place
        );
        WriteScratchSection(writer, scene, baseData, state, worldVersion, clock, travel, relationships);
    }
    else {
        WriteGameWorldSection(writer, state, baseData, worldVersion, clock, travel, relationships);
    }
    writer(player, core::StableId{}, activeVersion);
    core::SaveDomain(writer, state[game::ACTIVE], ActiveComponents{});
    writer(route.has_value());
    if (route) {
        writer(*route);
    }
    writer(RESPAWNS);
    writer(static_cast<u64>(volley ? 1 : 0));
    if (volley) {
        constexpr i64 DAMAGE = 20;
        constexpr u32 RESOLVED = 0;
        writer(volley->volley, core::AbilityKind::FlyingSwords, core::HitKind::Damage, DAMAGE, core::Affliction{}, RESOLVED);
        writer(static_cast<u64>(volley->targets.size()));
        for (const core::VolleyTarget& target : volley->targets) {
            writer(target);
        }
    }
    writer(static_cast<u64>(flash ? 1 : 0));
    if (flash) {
        writer(*flash);
    }
    writer(static_cast<u64>(seal ? 1 : 0));
    if (seal) {
        writer(*seal);
    }
    return bytes;
}

/// A save of the world scene with nothing in it, at `clock`, written against the world a game on `data` makes (the small
/// test data when null, as ArenaSave()): a host on that data loads it and runs its clock from there without a month of
/// ticks first.
[[nodiscard]] inline std::vector<std::byte> WorldSaveAt(const SavedClock& clock, const core::GameData* data = nullptr) {
    const core::State state{ game::DOMAINS, EMPTY_SAVE_SEED };
    return ArenaSave(
        state,
        {},
        std::nullopt,
        std::nullopt,
        std::nullopt,
        game::WORLD_SCENE,
        WORLD_VERSION,
        ACTIVE_VERSION,
        clock,
        game::Travel{},
        std::nullopt,
        game::Relationships{},
        {},
        data
    );
}

} // namespace test
