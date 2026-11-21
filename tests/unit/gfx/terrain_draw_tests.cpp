#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/manifest.hpp"
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
constexpr u32 PAGE_MASK = 0xFFFFU;

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
    stamps: { sprites: [stamp_water_0], density: 0.08, size: [0.7, 0.9], jitter: 0.15, thin_below: 0.5, mist: false }
  meadow:
    wash: [196, 204, 146]
    strength: 0.1
    stamps: { sprites: [stamp_meadow_0], density: 0.03, size: [0.6, 0.9], jitter: 0.2, thin_below: 0, mist: true }
roads:
  [road_0, road_1, road_2, road_3, road_4, road_5, road_6, road_7, road_8, road_9, road_10, road_11, road_12, road_13, road_14, road_15]
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

/// The instance that draws `sprite` with its foot at `foot`, standing still, with the fog flags `flags`.
core::Instance Placed(
    const core::Manifest& manifest,
    SpriteId sprite,
    core::Vec2 foot,
    u32 flags = core::INSTANCE_FOGGED_LAND
) {
    return core::MakeInstance(manifest, { .sprite = sprite, .previous = foot, .current = foot, .flags = flags });
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

/// Whether `instance` is `sprite` at `foot`, on the land the fog covers.
bool Is(const core::Manifest& manifest, const core::Instance& instance, SpriteId sprite, core::Vec2 foot) {
    return Same(instance, Placed(manifest, sprite, foot));
}

/// Whether `instance` shows `sprite`, wherever and however it stands: the same region of the same atlas page.
bool Shows(const core::Manifest& manifest, const core::Instance& instance, SpriteId sprite) {
    const core::Instance made = core::MakeInstance(manifest, { .sprite = sprite });
    return instance.regionMin == made.regionMin && (instance.layerFlags & PAGE_MASK) == (made.layerFlags & PAGE_MASK);
}

/// Every sprite `look` names for the terrain to draw: road overlays, crossings, stamps, mist, paintings and markers.
std::vector<SpriteId> LookSprites(const core::WorldLook& look) {
    std::vector<SpriteId> sprites{ look.roads.begin(), look.roads.end() };
    sprites.insert(sprites.end(), { look.bridgeAcross, look.bridgeDown, look.ford, look.cityMarker, look.places.city });
    for (const core::KindLook& kind : look.kinds) {
        sprites.insert(sprites.end(), kind.stamps.sprites.begin(), kind.stamps.sprites.end());
    }
    sprites.insert(sprites.end(), look.mist.sprites.begin(), look.mist.sprites.end());
    for (const auto& named : { look.dangerMarkers, look.places.dangers, look.places.byKey }) {
        for (const auto& [name, sprite] : named) {
            sprites.push_back(sprite);
        }
    }
    return sprites;
}

/// The instances of `drawing`'s batch `batch`.
std::span<const core::Instance> BatchOf(const gfx::TerrainDrawing& drawing, std::size_t batch) {
    const core::Batch& run = drawing.batches.at(batch);
    return std::span{ drawing.instances }.subspan(run.first, run.count);
}

/// The instances of `drawing`'s marker batches, the last ones.
std::span<const core::Instance> MarkersOf(const gfx::TerrainDrawing& drawing) {
    if (drawing.markersFrom >= drawing.batches.size()) {
        return {};
    }
    return std::span{ drawing.instances }.subspan(drawing.batches[drawing.markersFrom].first);
}

/// Whether `drawing`'s batches cover its instances in order, one after another, every batch normal blend.
bool Covers(const gfx::TerrainDrawing& drawing) {
    u32 next = 0;
    for (const core::Batch& batch : drawing.batches) {
        if (batch.first != next || batch.count == 0 || batch.pipeline != core::Pipeline::Normal) {
            return false;
        }
        next += batch.count;
    }
    return next == drawing.instances.size();
}

gfx::TerrainDrawing Drawn(
    const core::WorldMap& map,
    const core::WorldLook& look,
    const core::Manifest& manifest,
    core::Rect view,
    f32 zoom = 1.0F
) {
    gfx::TerrainDrawing drawing;
    gfx::DrawTerrain(map, look, manifest, view, zoom, drawing);
    return drawing;
}

TEST_CASE("the terrain draws no tiles and no river overlays") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = SmallMap();
    const core::WorldLook look = SmallLook(*manifest, map);
    // The whole map: the ground and river pipelines paint every tile and the river, so each instance is one of the look's
    // road overlays, crossings, stamps, puffs, paintings or markers.
    const gfx::TerrainDrawing drawing = Drawn(map, look, *manifest, TilesRect(0, 4, 0, 2));
    const std::vector<SpriteId> named = LookSprites(look);
    REQUIRE_FALSE(drawing.instances.empty());
    CHECK(std::ranges::all_of(drawing.instances, [&](const core::Instance& instance) {
        return std::ranges::any_of(named, [&](SpriteId sprite) { return Shows(*manifest, instance, sprite); });
    }));
    CHECK(Covers(drawing));
}

TEST_CASE("road overlays lead and markers close the terrain's batches") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = SmallMap();
    const core::WorldLook look = SmallLook(*manifest, map);
    const gfx::TerrainDrawing drawing = Drawn(map, look, *manifest, TilesRect(0, 4, 0, 2));
    REQUIRE(Covers(drawing));
    // Roads, crossings, the stamps and paintings (the two places' paintings at least), then the markers.
    REQUIRE(drawing.batches.size() >= 4);
    CHECK(drawing.roadBatches == 1);
    CHECK(BatchOf(drawing, 0).size() == 5);
    const std::span<const core::Instance> crossings = BatchOf(drawing, 1);
    REQUIRE(crossings.size() == 2);
    CHECK(Is(*manifest, crossings[0], SpriteId::Ford, Corner(3, 1)));
    CHECK(Is(*manifest, crossings[1], SpriteId::BridgeH, Corner(3, 2)));
    CHECK(drawing.markersFrom == drawing.batches.size() - 1);
    const std::span<const core::Instance> markers = MarkersOf(drawing);
    REQUIRE(markers.size() == 2);
    CHECK(Shows(*manifest, markers[0], SpriteId::MarkerCity));
    CHECK(Shows(*manifest, markers[1], SpriteId::MarkerMountainPass));
    // Row 0 alone, open water: no road leads and no marker closes, though the places' paintings reach in from below.
    const gfx::TerrainDrawing water = Drawn(map, look, *manifest, TilesRect(0, 4, 0, 0));
    CHECK(Covers(water));
    CHECK(water.roadBatches == 0);
    CHECK(water.markersFrom == water.batches.size());
    CHECK(MarkersOf(water).empty());
}

TEST_CASE("markers are hidden under cloud but not drained") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = SmallMap();
    const core::WorldLook look = SmallLook(*manifest, map);
    const gfx::TerrainDrawing drawing = Drawn(map, look, *manifest, TilesRect(0, 4, 0, 2));
    const std::span<const core::Instance> markers = MarkersOf(drawing);
    REQUIRE(markers.size() == 2);
    CHECK(Same(markers[0], Placed(*manifest, SpriteId::MarkerCity, Middle(0, 2), core::INSTANCE_FOG_SHADE)));
    CHECK(Same(markers[1], Placed(*manifest, SpriteId::MarkerMountainPass, Middle(4, 1), core::INSTANCE_FOG_SHADE)));
    // Everything under the markers is land: drained, misted and shaded together.
    const std::span<const core::Instance> land =
        std::span{ drawing.instances }.first(drawing.batches.at(drawing.markersFrom).first);
    REQUIRE_FALSE(land.empty());
    CHECK(std::ranges::all_of(land, [](const core::Instance& instance) {
        return instance.flags == core::INSTANCE_FOGGED_LAND;
    }));
}

TEST_CASE("a road tile draws the overlay its neighbours choose") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = SmallMap();
    const core::WorldLook look = SmallLook(*manifest, map);
    // Row 2, all five tiles: the road along it, the bridge, the capital's marker.
    const gfx::TerrainDrawing drawing = Drawn(map, look, *manifest, TilesRect(0, 4, 2, 2));
    REQUIRE(drawing.roadBatches == 1);
    const std::span<const core::Instance> roads = BatchOf(drawing, 0);
    REQUIRE(roads.size() == 5);
    // The road runs east to west: the west end has its east neighbour (2), the middle both (2 | 8), the east end its west (8).
    CHECK(Is(*manifest, roads[0], SpriteId::Road2, Corner(0, 2)));
    for (u32 column = 1; column < 4; ++column) {
        CHECK(Is(*manifest, roads[column], SpriteId::Road10, Corner(column, 2)));
    }
    CHECK(Is(*manifest, roads[4], SpriteId::Road8, Corner(4, 2)));
    // The bridge where the road crosses the river, east to west, over the road.
    const std::span<const core::Instance> crossings = BatchOf(drawing, 1);
    REQUIRE(crossings.size() == 1);
    CHECK(Is(*manifest, crossings[0], SpriteId::BridgeH, Corner(3, 2)));
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
    const gfx::TerrainDrawing drawing = Drawn(*map, look, *manifest, TilesRect(1, 1, 1, 1));
    // The tile's road (north and south: 1 | 4), then the downward bridge over it.
    REQUIRE(drawing.roadBatches == 1);
    REQUIRE(BatchOf(drawing, 0).size() == 1);
    CHECK(Is(*manifest, BatchOf(drawing, 0)[0], SpriteId::Road5, Corner(1, 1)));
    REQUIRE(BatchOf(drawing, 1).size() == 1);
    CHECK(Is(*manifest, BatchOf(drawing, 1)[0], SpriteId::BridgeV, Corner(1, 1)));
}

TEST_CASE("a ford draws on its tile, with no road before it") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = SmallMap();
    const core::WorldLook look = SmallLook(*manifest, map);
    // Row 1: no road, so the ford's crossing batch comes first.
    const gfx::TerrainDrawing drawing = Drawn(map, look, *manifest, TilesRect(0, 4, 1, 1));
    CHECK(drawing.roadBatches == 0);
    REQUIRE_FALSE(drawing.batches.empty());
    REQUIRE(BatchOf(drawing, 0).size() == 1);
    CHECK(Is(*manifest, BatchOf(drawing, 0)[0], SpriteId::Ford, Corner(3, 1)));
}

TEST_CASE("a place's marker draws at its tile's centre") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = SmallMap();
    const core::WorldLook look = SmallLook(*manifest, map);
    const gfx::TerrainDrawing drawing = Drawn(map, look, *manifest, TilesRect(0, 4, 1, 1));
    // A place off the view's tiles draws no marker: the capital stands in row 2.
    const std::span<const core::Instance> markers = MarkersOf(drawing);
    REQUIRE(markers.size() == 1);
    CHECK(Same(markers[0], Placed(*manifest, SpriteId::MarkerMountainPass, Middle(4, 1), core::INSTANCE_FOG_SHADE)));
}

TEST_CASE("a view past the map's edge draws only what stands on the map, and one wholly off it draws nothing") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = SmallMap();
    const core::WorldLook look = SmallLook(*manifest, map);
    // Past the east edge: only columns 3 and 4 of rows 0 and 1, the ford and the pass but no road.
    constexpr core::Rect PAST_EAST{ .min = { .x = 900.0F, .y = 10.0F }, .max = { .x = 5'000.0F, .y = 300.0F } };
    const gfx::TerrainDrawing east = Drawn(map, look, *manifest, PAST_EAST);
    CHECK(Covers(east));
    CHECK(east.roadBatches == 0);
    REQUIRE(BatchOf(east, 0).size() == 1);
    CHECK(Is(*manifest, BatchOf(east, 0)[0], SpriteId::Ford, Corner(3, 1)));
    REQUIRE(MarkersOf(east).size() == 1);
    CHECK(Shows(*manifest, MarkersOf(east)[0], SpriteId::MarkerMountainPass));
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
        CHECK(drawing.roadBatches == 0);
        CHECK(drawing.markersFrom == 0);
    }
    // A drawing is replaced by the next, never added to.
    gfx::TerrainDrawing reused = Drawn(map, look, *manifest, TilesRect(0, 4, 0, 2));
    REQUIRE_FALSE(reused.instances.empty());
    gfx::DrawTerrain(map, look, *manifest, OFF[0], 1.0F, reused);
    CHECK(reused.instances.empty());
    CHECK(reused.batches.empty());
    CHECK(reused.roadBatches == 0);
    CHECK(reused.markersFrom == 0);
}

TEST_CASE("a look short of the map's kinds draws no stamp there, never past a table") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = SmallMap();
    core::WorldLook look = SmallLook(*manifest, map);
    std::vector<SpriteId> stamps;
    for (const core::KindLook& kind : look.kinds) {
        stamps.insert(stamps.end(), kind.stamps.sprites.begin(), kind.stamps.sprites.end());
    }
    look.kinds.clear();
    const gfx::TerrainDrawing drawing = Drawn(map, look, *manifest, TilesRect(0, 4, 0, 2));
    CHECK(Covers(drawing));
    CHECK(std::ranges::none_of(drawing.instances, [&](const core::Instance& instance) {
        return std::ranges::any_of(stamps, [&](SpriteId sprite) { return Shows(*manifest, instance, sprite); });
    }));
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
    bool covered = true;
    for (const core::Vec2 centre : centres) {
        // The farthest zoom thins the small stamps out; the nearest keeps every one.
        for (const f32 zoom : { 0.25F, 1.0F }) {
            const gfx::TerrainDrawing drawing = Drawn(
                *map, *look, *manifest, { .min = centre - core::WIDEST_HALF_VIEW, .max = centre + core::WIDEST_HALF_VIEW }, zoom
            );
            most = std::max(most, drawing.instances.size());
            covered = covered && Covers(drawing);
        }
    }
    CAPTURE(most);
    CHECK(most <= gfx::MAX_TERRAIN_INSTANCES);
    CHECK(most > 0);
    CHECK(covered);
}

} // namespace
