#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/manifest.hpp"
#include "core/random.hpp"
#include "core/view_limits.hpp"
#include "core/world_look.hpp"
#include "core/world_map.hpp"
#include "data/world_look.hpp"
#include "data/world_map.hpp"
#include "gfx/terrain_draw.hpp"
#include "platform/paths.hpp"
#include "support/manifest.hpp"
#include "support/shipped_world.hpp"

#include <doctest/doctest.h>

namespace {

using core::atlas::SpriteId;

constexpr f32 TILE = 256.0F;
constexpr f32 HALF = 0.5F;
constexpr u32 SMALL_COLUMNS = 5;
// A tile of the shipped map the variant case follows; the neighbouring tiles it draws beside.
// The wide view's first and last columns and rows (TilesRect(WIDE_FIRST, WIDE_LAST_COLUMN, WIDE_FIRST, WIDE_LAST_ROW)).
constexpr u32 WIDE_FIRST = 10;
constexpr u32 WIDE_LAST_COLUMN = 30;
constexpr u32 WIDE_LAST_ROW = 20;
constexpr std::size_t WIDE_COLUMNS = WIDE_LAST_COLUMN - WIDE_FIRST + 1;
constexpr core::TilePoint SAMPLE{ .column = 12, .row = 12 };

// Deep water over a meadow with a river down column 3, a road along row 2 (bridging the river at (3, 2)), a ford at (3, 1)
// and two places: the capital on the road's west end and a mountain pass in the east.
constexpr std::string_view SMALL_MAP = R"yaml(road_cost: 4
ford_cost: 30
tile_size: 256
kinds:
  - { symbol: "~", name: deep_water, cost: impassable }
  - { symbol: ".", name: meadow, cost: 10 }
grid: |
  ~~~~~
  ..~..
  .....
roads: [ [[0, 2], [4, 2]] ]
rivers: [ [[3, 1], [3, 2]] ]
fords: [ [3, 1] ]
profiles:
  - { key: on_foot, ticks_per_cost: 16 }
places:
  - { key: capital, kind: city, tile: [0, 2], name: place_capital, people: 100, start: true }
  - { key: wolf_pass, kind: danger, sort: mountain_pass, tile: [4, 1], name: place_wolf_pass }
)yaml";

constexpr std::string_view SMALL_LOOK = R"yaml(kinds:
  deep_water:
    wash: [88, 124, 148]
    strength: 0.68
    variants: [terrain_water_0]
    stamps: { sprites: [stamp_water_0], density: 0.08, size: [0.7, 0.9], jitter: 0.15, thin_below: 0.5, mist: false }
  meadow:
    wash: [196, 204, 146]
    strength: 0.1
    variants: [terrain_meadow_0, terrain_meadow_1]
    stamps: { sprites: [stamp_meadow_0], density: 0.03, size: [0.6, 0.9], jitter: 0.2, thin_below: 0, mist: true }
roads:
  [road_0, road_1, road_2, road_3, road_4, road_5, road_6, road_7, road_8, road_9, road_10, road_11, road_12, road_13, road_14, road_15]
rivers:
  [river_0, river_1, river_2, river_3, river_4, river_5, river_6, river_7, river_8, river_9, river_10, river_11, river_12, river_13, river_14, river_15]
bridge: { across: bridge_h, down: bridge_v }
ford: ford
markers:
  city: marker_city
  mountain_pass: marker_mountain_pass
route: { dot: route_dot, spacing: 48, marker: route_marker, cursor: target_tile }
fog:
  clouds: [cloud_0]
  cloud_width: 1.15
  bob: { amplitude: 0.03, period: 4.0 }
  drift: 0.3
  fade_seconds: 0.5
  drain: { strength: 0.85, sepia: [160, 138, 106] }
  mist: { colour: [239, 233, 220], strength: 0.25 }
  shade: [214, 206, 192]
ground:
  paper: [240, 235, 222]
  grain: 0.04
  edge_warp: 0.45
  edge_soft: 0.5
  blotch: 0.5
  water: { shallow: meadow, deep: deep_water, depth_tiles: 2.5 }
  shore: { colour: [70, 104, 112], strength: 0.55, width: 0.12 }
  grid: { colour: [90, 80, 70], strength: 0.09, fade: [0.6, 0.35] }
veil: { colour: [246, 244, 238], scale: 3.0, strength: 0.55 }
mist: { sprites: [mist_0], colour: [246, 244, 238], strength: 0.9, chance: 0.7, width: [1.0, 1.4] }
river: { width: 0.38, edge: 0.5, colour: [128, 172, 176], edge_colour: [70, 104, 112], edge_strength: 0.67 }
places:
  city: painting_city
  dangers: { mountain_pass: painting_mountain_pass }
  by_key: { capital: painting_capital }
)yaml";

core::WorldMap SmallMap() {
    core::Strings strings;
    strings.Add("place_capital", { { "Capital", "都城" } });
    strings.Add("place_wolf_pass", { { "Wolf Pass", "狼关" } });
    std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(SMALL_MAP, strings);
    REQUIRE(map.has_value());
    return std::move(*map);
}

core::WorldLook SmallLook(const core::Manifest& manifest, const core::WorldMap& map) {
    std::expected<core::WorldLook, core::Error> look = data::ParseWorldLook(SMALL_LOOK, manifest, map);
    REQUIRE(look.has_value());
    return std::move(*look);
}

// The world rectangle of `columns` and `rows`, inside their tiles: the first column and row to the last, both included.
core::Rect TilesRect(u32 firstColumn, u32 lastColumn, u32 firstRow, u32 lastRow) {
    const f32 inset = 10.0F;
    return { .min = { .x = (static_cast<f32>(firstColumn) * TILE) + inset, .y = (static_cast<f32>(firstRow) * TILE) + inset },
             .max = { .x = (static_cast<f32>(lastColumn) * TILE) + inset, .y = (static_cast<f32>(lastRow) * TILE) + inset } };
}

// The instance that draws `sprite` with its foot at `foot`, as the terrain places everything: standing still, on the land
// the fog covers.
core::Instance Placed(const core::Manifest& manifest, SpriteId sprite, core::Vec2 foot) {
    return core::MakeInstance(
        manifest, { .sprite = sprite, .previous = foot, .current = foot, .flags = core::INSTANCE_FOGGED_LAND }
    );
}

core::Vec2 Corner(u32 column, u32 row) {
    return { .x = static_cast<f32>(column) * TILE, .y = static_cast<f32>(row) * TILE };
}

core::Vec2 Middle(u32 column, u32 row) {
    return { .x = (static_cast<f32>(column) + HALF) * TILE, .y = (static_cast<f32>(row) + HALF) * TILE };
}

bool Same(const core::Instance& lhs, const core::Instance& rhs) {
    return std::ranges::equal(std::as_bytes(std::span{ &lhs, 1 }), std::as_bytes(std::span{ &rhs, 1 }));
}

// Whether `instance` is `sprite` at `foot`.
bool Is(const core::Manifest& manifest, const core::Instance& instance, SpriteId sprite, core::Vec2 foot) {
    return Same(instance, Placed(manifest, sprite, foot));
}

// Whether `instance` is one of the two meadow variants at `foot`.
bool IsMeadow(const core::Manifest& manifest, const core::Instance& instance, core::Vec2 foot) {
    return Is(manifest, instance, SpriteId::TerrainMeadow0, foot) || Is(manifest, instance, SpriteId::TerrainMeadow1, foot);
}

gfx::TerrainDrawing Drawn(
    const core::WorldMap& map,
    const core::WorldLook& look,
    const core::Manifest& manifest,
    core::Rect view
) {
    gfx::TerrainDrawing drawing;
    gfx::DrawTerrain(map, look, manifest, view, drawing);
    return drawing;
}

TEST_CASE("the terrain draws the tiles in view by the map's kinds and the look's variants") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = SmallMap();
    const core::WorldLook look = SmallLook(*manifest, map);
    // Columns 0 and 1, rows 0 and 1: deep water above, meadow below.
    const gfx::TerrainDrawing drawing = Drawn(map, look, *manifest, TilesRect(0, 1, 0, 1));
    REQUIRE(drawing.instances.size() == 4);
    CHECK(Is(*manifest, drawing.instances[0], SpriteId::TerrainWater0, Corner(0, 0)));
    CHECK(Is(*manifest, drawing.instances[1], SpriteId::TerrainWater0, Corner(1, 0)));
    CHECK(IsMeadow(*manifest, drawing.instances[2], Corner(0, 1)));
    CHECK(IsMeadow(*manifest, drawing.instances[3], Corner(1, 1)));
    REQUIRE(drawing.batches.size() == 1);
    CHECK(drawing.batches[0].first == 0);
    CHECK(drawing.batches[0].count == 4);
    CHECK(drawing.batches[0].pipeline == core::Pipeline::Normal);
}

TEST_CASE("a tile's variant depends on its place only, so a view never changes it") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    std::expected<core::WorldLook, core::Error> look =
        platform::AssetsDirectory().and_then([&](const std::filesystem::path& assets) {
            return data::LoadWorldLook(assets / "data", *manifest, *map);
        });
    REQUIRE(look.has_value());
    const gfx::TerrainDrawing wide =
        Drawn(*map, *look, *manifest, TilesRect(WIDE_FIRST, WIDE_LAST_COLUMN, WIDE_FIRST, WIDE_LAST_ROW));
    const gfx::TerrainDrawing narrow =
        Drawn(*map, *look, *manifest, TilesRect(SAMPLE.column, SAMPLE.column + 2, SAMPLE.row, SAMPLE.row + 2));
    // The narrow view's first tile, (12, 12), is the wide view's tile at row 2, column 2 of its span.
    const std::size_t wideIndex = (std::size_t{ 2 } * WIDE_COLUMNS) + 2;
    CHECK(Same(narrow.instances.front(), wide.instances.at(wideIndex)));
    // The pick two-variant kinds keep: the top bit of the hash of column and row.
    constexpr u64 SEED = 0x5eed;
    constexpr u32 PURPOSE = 9;
    constexpr u32 DRAW = 2;
    constexpr u32 TOP_BIT = 63;
    constexpr u32 COLUMN_SHIFT = 32;
    std::size_t checked = 0;
    std::size_t mismatches = 0;
    std::array<bool, 2> seen{};
    for (u32 row = WIDE_FIRST; row <= WIDE_LAST_ROW; ++row) {
        for (u32 column = WIDE_FIRST; column <= WIDE_LAST_COLUMN; ++column) {
            const std::vector<SpriteId>& kind =
                look->kinds.at(map->KindAt(map->TileIndex({ .column = column, .row = row }))).variants;
            if (kind.size() != 2) {
                continue;
            }
            const u64 key = (u64{ column } << COLUMN_SHIFT) | row;
            const u64 bit = core::Random(SEED, 0, key, PURPOSE, DRAW) >> TOP_BIT;
            const std::size_t index = (std::size_t{ row - WIDE_FIRST } * WIDE_COLUMNS) + (column - WIDE_FIRST);
            if (!Is(*manifest, wide.instances.at(index), kind.at(bit), Corner(column, row))) {
                ++mismatches;
            }
            seen.at(bit) = true;
            ++checked;
        }
    }
    // Each tile of a two-variant kind wears that pick (asserted once, for all), and both variants occur among them.
    CHECK(checked > 0);
    CHECK(mismatches == 0);
    CHECK(seen.at(0));
    CHECK(seen.at(1));
}

TEST_CASE("a road tile draws the overlay its neighbours choose") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = SmallMap();
    const core::WorldLook look = SmallLook(*manifest, map);
    // Row 2, all five tiles: meadow; the river's overlay on (3, 2); the road along it; the bridge; the capital's marker.
    const gfx::TerrainDrawing drawing = Drawn(map, look, *manifest, TilesRect(0, 4, 2, 2));
    REQUIRE(drawing.instances.size() == 13);
    for (u32 column = 0; column < SMALL_COLUMNS; ++column) {
        CHECK(IsMeadow(*manifest, drawing.instances[column], Corner(column, 2)));
    }
    // The river runs north from (3, 2) to the ford at (3, 1): its north neighbour only (1).
    CHECK(Is(*manifest, drawing.instances[5], SpriteId::River1, Corner(3, 2)));
    // The road runs east to west: the west end has its east neighbour (2), the middle both (2 | 8), the east end its west (8).
    CHECK(Is(*manifest, drawing.instances[6], SpriteId::Road2, Corner(0, 2)));
    for (u32 column = 1; column < 4; ++column) {
        CHECK(Is(*manifest, drawing.instances[6 + column], SpriteId::Road10, Corner(column, 2)));
    }
    CHECK(Is(*manifest, drawing.instances[10], SpriteId::Road8, Corner(4, 2)));
    // The bridge where the road crosses the river, east to west: after the river and the road, over both.
    CHECK(Is(*manifest, drawing.instances[11], SpriteId::BridgeH, Corner(3, 2)));
    CHECK(Is(*manifest, drawing.instances[12], SpriteId::MarkerCity, Middle(0, 2)));
}

TEST_CASE("every terrain instance is flagged as fogged land") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = SmallMap();
    const core::WorldLook look = SmallLook(*manifest, map);
    // The whole map: tiles, both overlays, the bridge, the ford and both markers.
    const gfx::TerrainDrawing drawing = Drawn(map, look, *manifest, TilesRect(0, 4, 0, 2));
    REQUIRE(drawing.instances.size() == 26);
    CHECK(std::ranges::all_of(drawing.instances, [](const core::Instance& instance) {
        return instance.flags == core::INSTANCE_FOGGED_LAND;
    }));
}

TEST_CASE("a bridge whose road runs north to south draws the downward bridge") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    constexpr std::string_view MAP = R"yaml(road_cost: 4
ford_cost: 30
tile_size: 256
kinds:
  - { symbol: ".", name: meadow, cost: 10 }
grid: |
  ...
  ...
  ...
roads: [ [[1, 0], [1, 2]] ]
rivers: [ [[0, 1], [2, 1]] ]
profiles:
  - { key: on_foot, ticks_per_cost: 16 }
places:
  - { key: capital, kind: city, tile: [1, 0], name: place_capital, people: 100, start: true }
)yaml";
    core::Strings strings;
    strings.Add("place_capital", { { "Capital", "都城" } });
    std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(MAP, strings);
    REQUIRE(map.has_value());
    core::WorldLook look = SmallLook(*manifest, SmallMap());
    look.kinds.resize(1);
    look.kinds[0].variants = { SpriteId::TerrainMeadow0 };
    const gfx::TerrainDrawing drawing = Drawn(*map, look, *manifest, TilesRect(1, 1, 1, 1));
    // The tile, its river overlay (east and west: 2 | 8), its road (north and south: 1 | 4), the downward bridge.
    REQUIRE(drawing.instances.size() >= 4);
    CHECK(Is(*manifest, drawing.instances[1], SpriteId::River10, Corner(1, 1)));
    CHECK(Is(*manifest, drawing.instances[2], SpriteId::Road5, Corner(1, 1)));
    CHECK(Is(*manifest, drawing.instances[3], SpriteId::BridgeV, Corner(1, 1)));
}

TEST_CASE("a ford draws the river, then the ford") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = SmallMap();
    const core::WorldLook look = SmallLook(*manifest, map);
    // Row 1: the tiles, the river's overlay on (3, 1) (south neighbour only: 4), its ford, the pass's marker.
    const gfx::TerrainDrawing drawing = Drawn(map, look, *manifest, TilesRect(0, 4, 1, 1));
    REQUIRE(drawing.instances.size() == 8);
    CHECK(Is(*manifest, drawing.instances[5], SpriteId::River4, Corner(3, 1)));
    CHECK(Is(*manifest, drawing.instances[6], SpriteId::Ford, Corner(3, 1)));
}

TEST_CASE("a place's marker draws at its tile's centre") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = SmallMap();
    const core::WorldLook look = SmallLook(*manifest, map);
    const gfx::TerrainDrawing drawing = Drawn(map, look, *manifest, TilesRect(0, 4, 1, 1));
    REQUIRE(drawing.instances.size() == 8);
    CHECK(Is(*manifest, drawing.instances[7], SpriteId::MarkerMountainPass, Middle(4, 1)));
    // A place off the view's tiles draws no marker: the capital stands in row 2.
    CHECK(std::ranges::none_of(drawing.instances, [&](const core::Instance& instance) {
        return Is(*manifest, instance, SpriteId::MarkerCity, Middle(0, 2));
    }));
}

TEST_CASE("a view past the map's edge draws only tiles on the map, and one wholly off it draws nothing") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = SmallMap();
    const core::WorldLook look = SmallLook(*manifest, map);
    // Past the east edge: only columns 3 and 4 of row 0, water.
    constexpr core::Rect PAST_EAST{ .min = { .x = 900.0F, .y = 10.0F }, .max = { .x = 5'000.0F, .y = 100.0F } };
    const gfx::TerrainDrawing east = Drawn(map, look, *manifest, PAST_EAST);
    REQUIRE(east.instances.size() == 2);
    CHECK(Is(*manifest, east.instances[0], SpriteId::TerrainWater0, Corner(3, 0)));
    CHECK(Is(*manifest, east.instances[1], SpriteId::TerrainWater0, Corner(4, 0)));
    // Wholly off the map in each direction: nothing, not even a batch.
    constexpr std::array<core::Rect, 4> OFF{ {
        { .min = { .x = -3'000.0F, .y = 0.0F }, .max = { .x = -1'000.0F, .y = 600.0F } },
        { .min = { .x = 6'000.0F, .y = 0.0F }, .max = { .x = 8'000.0F, .y = 600.0F } },
        { .min = { .x = 0.0F, .y = -3'000.0F }, .max = { .x = 600.0F, .y = -1'000.0F } },
        { .min = { .x = 0.0F, .y = 5'000.0F }, .max = { .x = 600.0F, .y = 7'000.0F } },
    } };
    for (const core::Rect& view : OFF) {
        const gfx::TerrainDrawing drawing = Drawn(map, look, *manifest, view);
        CHECK(drawing.instances.empty());
        CHECK(drawing.batches.empty());
    }
    // A drawing is replaced by the next, never added to.
    gfx::TerrainDrawing reused = Drawn(map, look, *manifest, TilesRect(0, 4, 0, 2));
    REQUIRE_FALSE(reused.instances.empty());
    gfx::DrawTerrain(map, look, *manifest, OFF[0], reused);
    CHECK(reused.instances.empty());
    CHECK(reused.batches.empty());
}

TEST_CASE("a look short of the map's kinds draws the tile bare, never past a table") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = SmallMap();
    core::WorldLook look = SmallLook(*manifest, map);
    look.kinds.resize(1);
    look.kinds[0].variants.clear();
    const gfx::TerrainDrawing drawing = Drawn(map, look, *manifest, TilesRect(0, 1, 0, 1));
    CHECK(drawing.instances.empty());
}

TEST_CASE("the widest view of the shipped map stays within MAX_TERRAIN_INSTANCES") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    std::expected<core::WorldLook, core::Error> look =
        platform::AssetsDirectory().and_then([&](const std::filesystem::path& assets) {
            return data::LoadWorldLook(assets / "data", *manifest, *map);
        });
    REQUIRE(look.has_value());
    // Centred on each place, the map's middle and its four corners, where overlays and markers crowd or the view overhangs.
    std::vector<core::Vec2> centres{
        { .x = static_cast<f32>(map->Columns()) * TILE * HALF, .y = static_cast<f32>(map->Rows()) * TILE * HALF },
        { .x = 0.0F, .y = 0.0F },
        { .x = static_cast<f32>(map->Columns()) * TILE, .y = 0.0F },
        { .x = 0.0F, .y = static_cast<f32>(map->Rows()) * TILE },
        { .x = static_cast<f32>(map->Columns()) * TILE, .y = static_cast<f32>(map->Rows()) * TILE }
    };
    for (const core::Place& place : map->Places()) {
        centres.push_back(map->TileCentre(map->TileIndex(place.tile)));
    }
    std::size_t most = 0;
    for (const core::Vec2 centre : centres) {
        const gfx::TerrainDrawing drawing =
            Drawn(*map, *look, *manifest, { .min = centre - core::WIDEST_HALF_VIEW, .max = centre + core::WIDEST_HALF_VIEW });
        most = std::max(most, drawing.instances.size());
        u32 batched = 0;
        for (const core::Batch& batch : drawing.batches) {
            batched += batch.count;
        }
        CHECK(batched == drawing.instances.size());
    }
    CAPTURE(most);
    CHECK(most <= gfx::MAX_TERRAIN_INSTANCES);
    CHECK(most > 0);
}

} // namespace
