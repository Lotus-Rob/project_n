#pragma once

/// @file
/// The shipped world map for tests: assets/data/world.yaml read against the shipped strings, as main reads it, the shipped
/// names, the shipped animations, and the game data a world scene runs on in the game (game::BuiltInData() with that map and
/// those names), since game::BuiltInData() carries neither; and a small map, names and data for tests that need a world map but
/// not the shipped one, whose world is made in a moment where the shipped one takes a large part of a second. Their target
/// copies assets/ next to it (the `assets` rule) and links data and platform, and game for ShippedData().

#include "core/animations.hpp"
#include "core/game_data.hpp"
#include "core/manifest.hpp"
#include "core/names.hpp"
#include "core/world_look.hpp"
#include "core/world_map.hpp"
#include "data/animations.hpp"
#include "data/names.hpp"
#include "data/world_look.hpp"
#include "game/host.hpp"
#include "platform/paths.hpp"
#include "support/shipped_world.hpp"

#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <utility>

namespace test {

/// assets/data/world_look.yaml, read against `manifest` and `map` (ShippedWorld()); a test binary that can't read it stops at
/// once, as ShippedWorld() does.
[[nodiscard]] inline core::WorldLook ShippedWorldLook(const core::Manifest& manifest, const core::WorldMap& map) {
    std::expected<core::WorldLook, core::Error> look =
        platform::AssetsDirectory().and_then([&](const std::filesystem::path& assets) {
            return data::LoadWorldLook(assets / "data", manifest, map);
        });
    if (!look) {
        std::abort(); // every test after would fail for the same reason
    }
    return std::move(*look);
}

/// assets/data/names.yaml; a test binary that can't read it stops at once, as ShippedWorld() does.
[[nodiscard]] inline std::shared_ptr<const core::Names> ShippedNames() {
    std::expected<core::Names, core::Error> names =
        platform::AssetsDirectory().and_then([](const std::filesystem::path& assets) {
            return data::LoadNames(assets / "data" / "names.yaml");
        });
    if (!names) {
        std::abort(); // every test after would fail for the same reason
    }
    return std::make_shared<const core::Names>(std::move(*names));
}

/// assets/data/animations.yaml, read against `manifest`; a test binary that can't read it stops at once, as ShippedWorld()
/// does.
[[nodiscard]] inline std::shared_ptr<const core::Animations> ShippedAnimations(const core::Manifest& manifest) {
    std::expected<core::Animations, core::Error> animations =
        platform::AssetsDirectory().and_then([&](const std::filesystem::path& assets) {
            return data::LoadAnimations(assets / "data", manifest);
        });
    if (!animations) {
        std::abort(); // every test after would fail for the same reason
    }
    return std::make_shared<const core::Animations>(std::move(*animations));
}

/// game::BuiltInData() with the shipped world map and names: the data every host that builds or cycles into the world
/// scene needs, its travellers named as in the game.
[[nodiscard]] inline core::GameData ShippedData() {
    core::GameData data = game::BuiltInData();
    data.world = ShippedWorld();
    data.names = ShippedNames();
    return data;
}

/// The small test map's layout (SmallWorld()).
namespace small {
/// A tile's size, in world units.
inline constexpr f32 TILE = 256.0F;
/// What a road step costs.
inline constexpr u16 ROAD_COST = 4;
/// What a ford step costs.
inline constexpr u16 FORD_COST = 30;
/// What a meadow step costs.
inline constexpr u16 MEADOW_COST = 10;
/// The meadow, every tile's terrain but one.
inline constexpr u8 MEADOW = 0;
/// The peaks, which no one can walk onto: the bottom row's last tile's terrain.
inline constexpr u8 PEAKS = 1;
/// Each city's people.
inline constexpr u32 PEOPLE = 100;
/// Its columns.
inline constexpr u32 COLUMNS = 7;
/// Its rows.
inline constexpr u32 ROWS = 3;
/// Its tiles.
inline constexpr u32 TILES = COLUMNS * ROWS;
/// The home city's place index, on tile 0, where a new game starts.
inline constexpr u16 HOME = 0;
/// The destination city's place index, on tile 6, the top row's right end.
inline constexpr u16 DESTINATION = 1;
/// The danger's place index, in the bottom row's middle.
inline constexpr u16 DANGER = 2;
/// The last column of the road along the top row, from column 1.
inline constexpr u32 ROAD_END = 5;
/// The ticks per cost of the second profile SmallWorld(true) adds.
inline constexpr u16 RIDDEN_TICKS_PER_COST = 8;
/// Travel rules near the shipped map's, without its bands (the small map has one profile), which TravellingSmallData()
/// gives the small map: its two cities in each other's reach, so a month step sends visitors between them.
inline constexpr core::TravelRules TRAVEL{ .sharePerMille = 60,
                                           .stayMinDays = 2,
                                           .stayMaxDays = 5,
                                           .reachBase = 300,
                                           .reachPerPower = 20,
                                           .cultivatorPower = 5.0F,
                                           .bands = {} };
} // namespace small

/// The small test map: 7 by 3 tiles of meadow but for impassable peaks on the last tile (the bottom row's right end),
/// city small::HOME (the start) at tile 0, a road over tiles 1 to 5 of the top row and city small::DESTINATION at tile 6,
/// its right end, each of small::PEOPLE, and a danger, small::DANGER, in the bottom row's middle. Its one profile is the
/// default on_foot; with `secondProfile` a `ridden` one twice as fast follows it. Its travellers follow `travel`: by
/// default they reach nowhere, so nobody travels. A test binary that can't build it stops at once, as ShippedWorld() does.
[[nodiscard]] inline core::WorldMap SmallWorldMap(bool secondProfile = false, const core::TravelRules& travel = {}) {
    core::WorldMapSource source;
    source.travel = travel;
    source.columns = small::COLUMNS;
    source.rows = small::ROWS;
    source.tileSize = small::TILE;
    source.roadCost = small::ROAD_COST;
    source.fordCost = small::FORD_COST;
    source.kinds = { { .name = "meadow", .symbol = '.', .cost = small::MEADOW_COST },
                     { .name = "peaks", .symbol = '^', .cost = core::IMPASSABLE_COST } };
    source.grid.assign(static_cast<std::size_t>(small::TILES), small::MEADOW);
    source.grid.back() = small::PEAKS;
    source.roads = { { { .column = 1, .row = 0 }, { .column = small::ROAD_END, .row = 0 } } };
    for (const u16 city : { small::HOME, small::DESTINATION }) {
        source.places.push_back(
            { .key = std::format("city{}", city),
              .kind = core::PlaceKind::City,
              .sort = "",
              .tile = { .column = city == small::HOME ? 0U : small::COLUMNS - 1, .row = 0 },
              .nameKey = std::format("city_{}", city),
              .name = {},
              .people = small::PEOPLE,
              .start = city == small::HOME }
        );
    }
    source.places.push_back(
        { .key = "danger",
          .kind = core::PlaceKind::Danger,
          .sort = "bandits",
          .tile = { .column = small::COLUMNS / 2, .row = small::ROWS - 1 },
          .nameKey = "danger",
          .name = {},
          .people = 0,
          .start = false,
          .draws = { { 0, 0 } } }
    );
    if (secondProfile) {
        const core::FeatureCosts features{
            .road = small::ROAD_COST, .river = core::IMPASSABLE_COST, .bridge = small::ROAD_COST, .ford = small::FORD_COST
        };
        source.profiles = {
            { .key = "on_foot",
              .ticksPerCost = core::DEFAULT_TICKS_PER_COST,
              .kindCosts = { small::MEADOW_COST, core::IMPASSABLE_COST },
              .features = features },
            { .key = "ridden",
              .ticksPerCost = small::RIDDEN_TICKS_PER_COST,
              .kindCosts = { small::MEADOW_COST, core::IMPASSABLE_COST },
              .features = features },
        };
    }
    std::expected<core::WorldMap, core::Error> built = core::WorldMap::Build(std::move(source));
    if (!built) {
        std::abort(); // every test after would fail for the same reason
    }
    return *std::move(built);
}

/// SmallWorldMap(`secondProfile`, `travel`), shared as game data holds a map.
[[nodiscard]] inline std::shared_ptr<const core::WorldMap> SmallWorld(
    bool secondProfile = false,
    const core::TravelRules& travel = {}
) {
    return std::make_shared<const core::WorldMap>(SmallWorldMap(secondProfile, travel));
}

/// A names table of one surname and one given character.
[[nodiscard]] inline std::shared_ptr<const core::Names> SmallNames() {
    core::Names names;
    names.surnames = { { .characters = "王", .pinyin = "wang", .weight = 1 } };
    names.given = { { .characters = "小", .pinyin = "xiao", .weight = 1 } };
    return std::make_shared<const core::Names>(std::move(names));
}

/// game::BuiltInData() with the small map (SmallWorld(`secondProfile`)) and names (SmallNames()): enough for a world scene,
/// whose world, of 200 NPCs, is made in a moment.
[[nodiscard]] inline core::GameData SmallData(bool secondProfile = false) {
    core::GameData data = game::BuiltInData();
    data.world = SmallWorld(secondProfile);
    data.names = SmallNames();
    return data;
}

/// SmallData() whose map has the shipped travel rules (small::TRAVEL): a month step there sends visitors between its
/// cities, so its world's journeys end and begin.
[[nodiscard]] inline core::GameData TravellingSmallData() {
    core::GameData data = game::BuiltInData();
    data.world = SmallWorld(false, small::TRAVEL);
    data.names = SmallNames();
    return data;
}

} // namespace test
