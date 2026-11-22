#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/world_map.hpp"

#include <doctest/doctest.h>

namespace {

constexpr f32 TILE = 256.0F;
constexpr f32 HALF_TILE = 128.0F;
constexpr u16 ROAD_COST = 4;
constexpr u16 FORD_COST = 30;
constexpr u16 MEADOW_COST = 10;
constexpr u16 SAND_COST = 14;
constexpr u8 MEADOW = 0;
constexpr u8 DEEP = 1;
constexpr u8 PEAKS = 2;
constexpr u8 SAND = 3;
constexpr u32 PEOPLE = 100;
constexpr u32 SAND_TILE = 5;
constexpr u32 OFF_MAP = 9;
constexpr u32 PAINT_TILES = 15;
constexpr f32 CENTRE_X = 640.0F;
constexpr f32 CENTRE_Y = 384.0F;

// A map from rows of symbols: '.' meadow, ',' sand, '~' deep water, '^' peaks, and a digit for a city on meadow, whose
// place index is the digit; city 0 is the start.
core::WorldMapSource Source(const std::vector<std::string>& rows) {
    core::WorldMapSource source;
    source.columns = static_cast<u32>(rows.front().size());
    source.rows = static_cast<u32>(rows.size());
    source.tileSize = TILE;
    source.roadCost = ROAD_COST;
    source.fordCost = FORD_COST;
    source.kinds = { { .name = "meadow", .symbol = '.', .cost = MEADOW_COST },
                     { .name = "deep water", .symbol = '~', .cost = core::IMPASSABLE_COST },
                     { .name = "peaks", .symbol = '^', .cost = core::IMPASSABLE_COST },
                     { .name = "sand", .symbol = ',', .cost = SAND_COST } };
    for (u32 row = 0; row < source.rows; ++row) {
        for (u32 column = 0; column < source.columns; ++column) {
            const char symbol = rows[row][column];
            u8 kind = MEADOW;
            if (symbol == '~') {
                kind = DEEP;
            }
            else if (symbol == '^') {
                kind = PEAKS;
            }
            else if (symbol == ',') {
                kind = SAND;
            }
            source.grid.push_back(kind);
            if (symbol >= '0' && symbol <= '9') {
                const auto index = static_cast<std::size_t>(symbol - '0');
                source.places.resize(std::max(source.places.size(), index + 1));
                source.places[index] = { .key = std::format("city{}", index),
                                         .kind = core::PlaceKind::City,
                                         .sort = "",
                                         .tile = { .column = column, .row = row },
                                         .nameKey = std::format("city_{}", index),
                                         .name = {},
                                         .people = PEOPLE,
                                         .start = index == 0 };
            }
        }
    }
    return source;
}

core::WorldMap MustBuild(core::WorldMapSource source) {
    auto built = core::WorldMap::Build(std::move(source));
    REQUIRE(built.has_value());
    return *std::move(built);
}

std::string Refusal(core::WorldMapSource source) {
    const auto built = core::WorldMap::Build(std::move(source));
    REQUIRE_FALSE(built.has_value());
    return built.error().message;
}

TEST_CASE("a road caps a tile's cost and a river blocks it") {
    // Columns: 0 the city, 1 road, 2 river, 3 road and river, 4 river with a ford, 5 road over sand.
    core::WorldMapSource source = Source({ "0.....", "......" });
    source.grid[SAND_TILE] = SAND;
    source.roads = { { { .column = 1, .row = 0 } }, { { .column = 3, .row = 0 } }, { { .column = SAND_TILE, .row = 0 } } };
    source.rivers = { { { .column = 2, .row = 0 } }, { { .column = 3, .row = 0 } }, { { .column = 4, .row = 0 } } };
    source.fords = { { .column = 4, .row = 0 } };
    const core::WorldMap map = MustBuild(std::move(source));
    CHECK(map.CostAt(0) == MEADOW_COST);
    CHECK(map.CostAt(1) == ROAD_COST);
    CHECK(map.FlagsAt(1) == std::to_underlying(core::TileFlag::Road));
    CHECK(map.CostAt(2) == core::IMPASSABLE_COST);
    CHECK(map.FlagsAt(2) == std::to_underlying(core::TileFlag::River));
    CHECK(map.CostAt(3) == ROAD_COST);
    CHECK(
        map.FlagsAt(3) == (std::to_underlying(core::TileFlag::Road) | std::to_underlying(core::TileFlag::River) |
                           std::to_underlying(core::TileFlag::Bridge))
    );
    CHECK(map.CostAt(4) == FORD_COST);
    CHECK(map.FlagsAt(4) == (std::to_underlying(core::TileFlag::River) | std::to_underlying(core::TileFlag::Ford)));
    CHECK(map.CostAt(5) == ROAD_COST);
    CHECK(map.CostAt(6) == MEADOW_COST);
}

TEST_CASE("a polyline marks every tile its segments pass through, edge to edge") {
    core::WorldMapSource source = Source({ "0....", ".....", "....." });
    source.roads = { { { .column = 0, .row = 0 }, { .column = 3, .row = 2 } },
                     { { .column = 4, .row = 0 } },
                     { { .column = 0, .row = 2 }, { .column = 0, .row = 2 } } };
    const core::WorldMap map = MustBuild(std::move(source));
    std::vector<u32> marked;
    for (u32 tile = 0; tile < PAINT_TILES; ++tile) {
        if ((map.FlagsAt(tile) & std::to_underlying(core::TileFlag::Road)) != 0) {
            marked.push_back(tile);
        }
    }
    CHECK(marked == std::vector<u32>{ 0, 1, 4, 6, 7, 10, 12, 13 });
}

// The Road-flagged tiles of the map, in tile order.
std::vector<u32> RoadTiles(const core::WorldMap& map) {
    std::vector<u32> marked;
    for (u32 tile = 0; tile < map.Columns() * map.Rows(); ++tile) {
        if ((map.FlagsAt(tile) & std::to_underlying(core::TileFlag::Road)) != 0) {
            marked.push_back(tile);
        }
    }
    return marked;
}

TEST_CASE("a steep segment running up and to the right marks its run, the same whichever end it starts from") {
    constexpr core::TilePoint LOW{ .column = 3, .row = 0 };
    constexpr core::TilePoint HIGH{ .column = 4, .row = 2 };
    core::WorldMapSource forward = Source({ "0....", ".....", "....." });
    forward.roads = { { HIGH, LOW } };
    core::WorldMapSource backward = Source({ "0....", ".....", "....." });
    backward.roads = { { LOW, HIGH } };
    // (3,0), (3,1), (4,1), (4,2)
    const std::vector<u32> forwardTiles = RoadTiles(MustBuild(std::move(forward)));
    const std::vector<u32> backwardTiles = RoadTiles(MustBuild(std::move(backward)));
    CHECK(forwardTiles == std::vector<u32>{ 3, 8, 9, 14 });
    CHECK(backwardTiles == std::vector<u32>{ 3, 8, 9, 14 });
}

TEST_CASE("a diagonal segment marks the same tiles in either direction") {
    constexpr core::TilePoint ORIGIN{ .column = 0, .row = 0 };
    constexpr core::TilePoint CORNER{ .column = 1, .row = 1 };
    core::WorldMapSource forward = Source({ "0....", ".....", "....." });
    forward.roads = { { ORIGIN, CORNER } };
    core::WorldMapSource backward = Source({ "0....", ".....", "....." });
    backward.roads = { { CORNER, ORIGIN } };
    // (0,0), (1,0), (1,1): the tie goes to the column from the lower end.
    const std::vector<u32> forwardTiles = RoadTiles(MustBuild(std::move(forward)));
    const std::vector<u32> backwardTiles = RoadTiles(MustBuild(std::move(backward)));
    CHECK(forwardTiles == std::vector<u32>{ 0, 1, 6 });
    CHECK(backwardTiles == std::vector<u32>{ 0, 1, 6 });
}

TEST_CASE("a diagonal step costs seven fifths and never cuts a corner") {
    const core::WorldMap open = MustBuild(Source({ "0..", "...", "..." }));
    CHECK(open.StepCost(0, 4) == 14);
    CHECK(open.StepCost(0, 1) == MEADOW_COST);
    CHECK(open.StepCost(0, 2) == core::IMPASSABLE_COST);
    CHECK(open.StepCost(4, 4) == core::IMPASSABLE_COST);
    CHECK(open.StepCost(0, 99) == core::IMPASSABLE_COST);
    const core::WorldMap cut = MustBuild(Source({ "0~.", "...", "..." }));
    CHECK(cut.StepCost(0, 4) == core::IMPASSABLE_COST);
    CHECK(cut.StepCost(3, 1) == core::IMPASSABLE_COST);
    CHECK(cut.StepCost(0, 3) == MEADOW_COST);
    // From (1,1) to (0,0), blocked only by the water at (1,0).
    CHECK(cut.StepCost(4, 0) == core::IMPASSABLE_COST);
    // From (0,0) to (1,1), blocked only by the water at (0,1).
    const core::WorldMap side = MustBuild(Source({ "0..", "~..", "..." }));
    CHECK(side.StepCost(0, 4) == core::IMPASSABLE_COST);
    CHECK(side.StepCost(0, 1) == MEADOW_COST);
}

TEST_CASE("each tile's region is its nearest city by walking cost") {
    const core::WorldMap map = MustBuild(Source({ "..0^.....1", "...^......", "...^......", ".........." }));
    // Tile 4: two tiles from city 0 as the crow flies, but an 80-cost walk round the ridge; city 1 is five steps (50) away.
    CHECK(map.RegionAt(4) == 1);
    CHECK(map.RegionAt(2) == 0);
    CHECK(map.RegionAt(10) == 0);
    CHECK(map.RegionAt(3) == core::NO_PLACE);
    const std::span<const u32> first = map.RegionTiles(0);
    const std::span<const u32> second = map.RegionTiles(1);
    CHECK(std::ranges::is_sorted(first));
    CHECK(first.size() + second.size() == 37);
    CHECK(std::ranges::find(first, 3U) == first.end());
    CHECK(map.RegionTiles(7).empty());
}

TEST_CASE("a tie between two cities goes to the lower index, the same every build") {
    const core::WorldMap map = MustBuild(Source({ "0.1" }));
    const core::WorldMap again = MustBuild(Source({ "0.1" }));
    CHECK(map.RegionAt(1) == 0);
    CHECK(map.Hash() == again.Hash());
    CHECK(again.RegionAt(1) == 0);
    // The cities written the other way round: place 0 is still the lower index.
    const core::WorldMap swapped = MustBuild(Source({ "1.0" }));
    CHECK(swapped.RegionAt(1) == 0);
    CHECK(swapped.RegionAt(0) == 1);
}

TEST_CASE("Build refuses a map it can't use") {
    core::WorldMapSource overWater = Source({ "0~." });
    overWater.roads = { { { .column = 0, .row = 0 }, { .column = 2, .row = 0 } } };
    const std::string overWaterRefusal = Refusal(overWater);
    CHECK(overWaterRefusal.contains("road 0"));

    core::WorldMapSource overPeaks = Source({ "0.^", "..." });
    overPeaks.roads = { { { .column = 0, .row = 1 }, { .column = 1, .row = 1 } }, { { .column = 2, .row = 0 } } };
    const std::string overPeaksRefusal = Refusal(overPeaks);
    CHECK(overPeaksRefusal.contains("road 1"));

    core::WorldMapSource onWater = Source({ "0.~" });
    onWater.places.push_back(
        { .key = "pit", .kind = core::PlaceKind::Danger, .sort = "pit", .tile = { .column = 2, .row = 0 }, .nameKey = "pit" }
    );
    const std::string onWaterRefusal = Refusal(onWater);
    CHECK(onWaterRefusal.contains("place 1"));

    core::WorldMapSource crowded = Source({ "0.." });
    crowded.places.push_back(
        { .key = "twin", .kind = core::PlaceKind::City, .sort = "", .tile = { .column = 0, .row = 0 }, .nameKey = "twin" }
    );
    const std::string crowdedRefusal = Refusal(crowded);
    CHECK(crowdedRefusal.contains("place 1"));

    core::WorldMapSource noStart = Source({ "0.1" });
    noStart.places[0].start = false;
    const std::string noStartRefusal = Refusal(noStart);
    CHECK(noStartRefusal.contains("no place starts"));

    core::WorldMapSource twoStarts = Source({ "0.1" });
    twoStarts.places[1].start = true;
    const std::string twoStartsRefusal = Refusal(twoStarts);
    CHECK(twoStartsRefusal.contains("places 0 and 1"));

    const std::string walledRefusal = Refusal(Source({ "0^1" }));
    CHECK(walledRefusal.contains("place 1"));

    core::WorldMapSource offMap = Source({ "0..", "..." });
    offMap.roads = { { { .column = 0, .row = 0 } }, { { .column = 1, .row = 1 }, { .column = 3, .row = 1 } } };
    const std::string offMapRefusal = Refusal(offMap);
    CHECK(offMapRefusal.contains("road 1"));

    core::WorldMapSource riverOff = Source({ "0.." });
    riverOff.rivers = { { { .column = 0, .row = OFF_MAP } } };
    const std::string riverOffRefusal = Refusal(riverOff);
    CHECK(riverOffRefusal.contains("river 0"));

    core::WorldMapSource dryFord = Source({ "0.." });
    dryFord.fords = { { .column = 1, .row = 0 } };
    const std::string dryFordRefusal = Refusal(dryFord);
    CHECK(dryFordRefusal.contains("ford 0"));

    core::WorldMapSource fordOff = Source({ "0.." });
    fordOff.fords = { { .column = OFF_MAP, .row = 0 } };
    const std::string fordOffRefusal = Refusal(fordOff);
    CHECK(fordOffRefusal.contains("ford 0"));

    core::WorldMapSource sameKey = Source({ "0.1" });
    sameKey.places[1].key = sameKey.places[0].key;
    const std::string sameKeyRefusal = Refusal(sameKey);
    CHECK(sameKeyRefusal.contains("'city0'"));
    CHECK(sameKeyRefusal.contains("places 0 and 1"));

    core::WorldMapSource impassableFord = Source({ "0~." });
    impassableFord.rivers = { { { .column = 1, .row = 0 } } };
    impassableFord.fords = { { .column = 1, .row = 0 } };
    const std::string impassableFordRefusal = Refusal(impassableFord);
    CHECK(impassableFordRefusal.contains("ford 0"));
    CHECK(impassableFordRefusal.contains("deep water"));

    core::WorldMapSource bridgedFord = Source({ "0.." });
    bridgedFord.rivers = { { { .column = 1, .row = 0 } } };
    bridgedFord.roads = { { { .column = 1, .row = 0 } } };
    bridgedFord.fords = { { .column = 1, .row = 0 } };
    const std::string bridgedFordRefusal = Refusal(bridgedFord);
    CHECK(bridgedFordRefusal.contains("ford 0"));
    CHECK(bridgedFordRefusal.contains("bridged"));

    core::WorldMapSource dearKind = Source({ "0.." });
    dearKind.kinds[MEADOW].cost = core::MAX_WALK_COST + 1;
    const std::string dearKindRefusal = Refusal(dearKind);
    CHECK(dearKindRefusal.contains("meadow"));
    core::WorldMapSource freeKind = Source({ "0.." });
    freeKind.kinds[MEADOW].cost = 0;
    const std::string freeKindRefusal = Refusal(freeKind);
    CHECK(freeKindRefusal.contains("costs 0"));
    core::WorldMapSource freeRoad = Source({ "0.." });
    freeRoad.roadCost = 0;
    const std::string freeRoadRefusal = Refusal(freeRoad);
    CHECK(freeRoadRefusal.contains("the road cost is 0"));
    core::WorldMapSource freeFord = Source({ "0.." });
    freeFord.fordCost = 0;
    const std::string freeFordRefusal = Refusal(freeFord);
    CHECK(freeFordRefusal.contains("the ford cost is 0"));
    core::WorldMapSource dearRoad = Source({ "0.." });
    dearRoad.roadCost = core::MAX_WALK_COST + 1;
    const std::string dearRoadRefusal = Refusal(dearRoad);
    CHECK(dearRoadRefusal.contains("road cost"));
    core::WorldMapSource dearFord = Source({ "0.." });
    dearFord.fordCost = core::MAX_WALK_COST + 1;
    const std::string dearFordRefusal = Refusal(dearFord);
    CHECK(dearFordRefusal.contains("ford cost"));

    core::WorldMapSource placeOff = Source({ "0.." });
    placeOff.places[0].tile.column = OFF_MAP;
    const std::string placeOffRefusal = Refusal(placeOff);
    CHECK(placeOffRefusal.contains("place 0"));

    core::WorldMapSource dangerStart = Source({ "0.1" });
    dangerStart.places[0].kind = core::PlaceKind::Danger;
    dangerStart.places[0].sort = "pit";
    const std::string dangerStartRefusal = Refusal(dangerStart);
    CHECK(dangerStartRefusal.contains("place 0"));
    CHECK(dangerStartRefusal.contains("isn't a city"));

    core::WorldMapSource bareDanger = Source({ "0.." });
    bareDanger.places.push_back(
        { .key = "pit", .kind = core::PlaceKind::Danger, .sort = "", .tile = { .column = 1, .row = 0 }, .nameKey = "pit" }
    );
    const std::string bareDangerRefusal = Refusal(bareDanger);
    CHECK(bareDangerRefusal.contains("place 1"));

    core::WorldMapSource sortedCity = Source({ "0.1" });
    sortedCity.places[1].sort = "cave";
    const std::string sortedCityRefusal = Refusal(sortedCity);
    CHECK(sortedCityRefusal.contains("place 1"));

    core::WorldMapSource shortGrid = Source({ "0.." });
    shortGrid.grid.pop_back();
    const std::string shortGridRefusal = Refusal(shortGrid);
    CHECK(shortGridRefusal.contains("grid"));
}

TEST_CASE("a refusal names the line of the entry it refuses when the source knows its lines") {
    constexpr u32 ROAD_LINE = 12;
    constexpr u32 PLACE_LINE = 40;
    core::WorldMapSource road = Source({ "0~." });
    road.roads = { { { .column = 1, .row = 0 } } };
    const std::string unlined = Refusal(road);
    CHECK(unlined == "road 0: the tile (1, 0) is deep water, which no road can cross");
    road.lines.roads = { ROAD_LINE };
    const std::string lined = Refusal(road);
    CHECK(lined == "line 12: road 0: the tile (1, 0) is deep water, which no road can cross");
    core::WorldMapSource place = Source({ "0.1" });
    place.places[1].key = place.places[0].key;
    place.lines.places = { PLACE_LINE - 1, PLACE_LINE };
    const std::string placeRefusal = Refusal(place);
    CHECK(placeRefusal.contains("line 40: places 0 and 1"));
    core::WorldMapSource crowded = Source({ "0.." });
    crowded.places.push_back(
        { .key = "twin", .kind = core::PlaceKind::City, .sort = "", .tile = { .column = 0, .row = 0 }, .nameKey = "twin" }
    );
    crowded.lines.places = { PLACE_LINE - 1, PLACE_LINE };
    const std::string crowdedRefusal = Refusal(crowded);
    CHECK(crowdedRefusal.contains("line 40: place 1"));
}

TEST_CASE("a danger site homing people is refused, naming its line") {
    constexpr u32 PLACE_LINE = 30;
    core::WorldMapSource source = Source({ "0.." });
    source.places.push_back(
        { .key = "pit", .kind = core::PlaceKind::Danger, .sort = "pit", .tile = { .column = 2, .row = 0 }, .nameKey = "pit" }
    );
    source.lines.places = { PLACE_LINE - 1, PLACE_LINE };
    core::WorldMapSource empty = source;
    const bool built = core::WorldMap::Build(std::move(empty)).has_value();
    CHECK(built);
    source.places[1].people = 1;
    const std::string refusal = Refusal(source);
    CHECK(refusal == "line 30: place 1 ('pit') is a danger with people");
}

TEST_CASE("two terrain kinds with one name are refused, naming the second's line") {
    constexpr u32 KIND_LINE = 7;
    core::WorldMapSource source = Source({ "0.." });
    source.kinds[SAND].name = "meadow";
    source.lines.kinds = { KIND_LINE - 3, KIND_LINE - 2, KIND_LINE - 1, KIND_LINE };
    const std::string refusal = Refusal(source);
    CHECK(refusal == "line 7: terrain kinds 0 and 3 share the name 'meadow'");
}

TEST_CASE("cities homing more than the most a world holds are refused, naming the city that crosses it") {
    constexpr u32 PLACE_LINE = 12;
    core::WorldMapSource source = Source({ "0.1" });
    source.places[0].people = core::MOST_WORLD_PEOPLE - PEOPLE;
    source.lines.places = { PLACE_LINE - 1, PLACE_LINE };
    core::WorldMapSource full = source;
    const bool built = core::WorldMap::Build(std::move(full)).has_value(); // exactly the most
    CHECK(built);
    source.places[1].people = PEOPLE + 1;
    const std::string refusal = Refusal(source);
    CHECK(refusal == "line 12: place 1 ('city1') brings the cities' people to 100001, over the most a world holds, 100000");
}

TEST_CASE("TileAt finds the tile a point lies on, clamped to the map") {
    // Three columns and two rows of 256-unit tiles.
    const core::WorldMap map = MustBuild(Source({ "0..", "..." }));
    constexpr core::Vec2 INSIDE{ .x = 300.0F, .y = 300.0F };
    constexpr core::Vec2 ON_EDGE{ .x = 512.0F, .y = 0.0F };
    constexpr core::Vec2 BEFORE{ .x = -10.0F, .y = -10.0F };
    constexpr core::Vec2 PAST{ .x = 10'000.0F, .y = 10'000.0F };
    constexpr core::Vec2 UNBOUNDED{ .x = std::numeric_limits<f32>::infinity(), .y = std::numeric_limits<f32>::quiet_NaN() };
    const u32 inside = map.TileAt(INSIDE);
    const u32 onEdge = map.TileAt(ON_EDGE);
    const u32 before = map.TileAt(BEFORE);
    const u32 past = map.TileAt(PAST);
    const u32 unbounded = map.TileAt(UNBOUNDED);
    CHECK(inside == 4);
    CHECK(onEdge == 2); // a tile's left edge is its own
    CHECK(before == 0);
    CHECK(past == 5);
    CHECK(unbounded == 5); // a coordinate that isn't a number counts as past the map
}

TEST_CASE("StartCentre is the middle of the start city's tile") {
    core::WorldMapSource source = Source({ "1..", "..0" });
    const core::WorldMap map = MustBuild(std::move(source));
    const core::Vec2 centre = map.StartCentre();
    CHECK(centre.x == CENTRE_X);
    CHECK(centre.y == CENTRE_Y);
}

TEST_CASE("TilesIn finds the tiles a view overlaps, clamped to the map") {
    constexpr u32 COLUMNS = 150;
    constexpr u32 ROWS = 85;
    std::vector<std::string> rows(ROWS, std::string(COLUMNS, '.'));
    rows[0][0] = '0';
    const core::WorldMap map = MustBuild(Source(rows));
    const auto area = [](const core::TileSpan& span) {
        return (span.lastColumn + 1 - span.firstColumn) * (span.lastRow + 1 - span.firstRow);
    };
    // Inside the tile in column 1, row 1 (256 to 512 on both axes).
    constexpr core::Rect INSIDE_ONE{ .min = { .x = 300.0F, .y = 300.0F }, .max = { .x = 400.0F, .y = 400.0F } };
    const core::TileSpan one = map.TilesIn(INSIDE_ONE);
    CHECK(one.firstColumn == 1);
    CHECK(one.lastColumn == 1);
    CHECK(one.firstRow == 1);
    CHECK(one.lastRow == 1);
    // Across the corner where columns and rows 1 and 2 meet, at 512.
    constexpr core::Rect STRADDLING{ .min = { .x = 500.0F, .y = 500.0F }, .max = { .x = 600.0F, .y = 600.0F } };
    const core::TileSpan four = map.TilesIn(STRADDLING);
    CHECK(four.firstColumn == 1);
    CHECK(four.lastColumn == 2);
    CHECK(four.firstRow == 1);
    CHECK(four.lastRow == 2);
    CHECK(area(four) == 4);
    // Past the map's far corner: only its last columns and rows.
    constexpr core::Rect PAST_CORNER{ .min = { .x = 38'000.0F, .y = 21'300.0F }, .max = { .x = 40'000.0F, .y = 23'000.0F } };
    const core::TileSpan corner = map.TilesIn(PAST_CORNER);
    CHECK(corner.firstColumn == 148);
    CHECK(corner.lastColumn == 149);
    CHECK(corner.firstRow == 83);
    CHECK(corner.lastRow == 84);
    // The whole map, the last row reaching a little past the map's edge.
    constexpr core::Rect WHOLE{ .min = { .x = 0.0F, .y = 0.0F }, .max = { .x = 38'400.0F, .y = 21'760.0F } };
    const core::TileSpan whole = map.TilesIn(WHOLE);
    CHECK(whole.firstColumn == 0);
    CHECK(whole.lastColumn == 149);
    CHECK(whole.firstRow == 0);
    CHECK(whole.lastRow == 84);
    CHECK(area(whole) == COLUMNS * ROWS);
    // A view that misses the map, before it or past its last row: no tiles at all, not the nearest edge's.
    constexpr core::Rect BEFORE{ .min = { .x = -2'000.0F, .y = 100.0F }, .max = { .x = -1'000.0F, .y = 200.0F } };
    constexpr core::Rect PAST{ .min = { .x = 100.0F, .y = 22'000.0F }, .max = { .x = 200.0F, .y = 23'000.0F } };
    CHECK(area(map.TilesIn(BEFORE)) == 0);
    CHECK(area(map.TilesIn(PAST)) == 0);
}

TEST_CASE("tiles are indexed row by row and centred in their square") {
    const core::WorldMap map = MustBuild(Source({ "0...", "...." }));
    CHECK(map.TileIndex({ .column = 2, .row = 1 }) == 6);
    CHECK(map.PointOf(6) == core::TilePoint{ .column = 2, .row = 1 });
    CHECK(map.TileCentre(6).x == CENTRE_X);
    CHECK(map.TileCentre(6).y == CENTRE_Y);
    CHECK(map.PlaceAt(0) == 0);
    CHECK(map.PlaceAt(1) == core::NO_PLACE);
    CHECK(map.Start() == 0);
}

TEST_CASE("the hash changes when any authored value does, not with resolved names") {
    core::WorldMapSource base = Source({ "0.....", "......", "..,..1" });
    base.rivers = { { { .column = 3, .row = 0 }, { .column = 3, .row = 2 } } };
    base.roads = { { { .column = 0, .row = 2 }, { .column = 1, .row = 2 } } };
    base.fords = { { .column = 3, .row = 1 }, { .column = 3, .row = 0 } };
    base.places.push_back(
        { .key = "cave",
          .kind = core::PlaceKind::Danger,
          .sort = "cave",
          .tile = { .column = 4, .row = 0 },
          .nameKey = "cave_name" }
    );
    const u64 reference = MustBuild(base).Hash();
    const auto hashOf = [&](auto change) {
        core::WorldMapSource source = base;
        change(source);
        return MustBuild(std::move(source)).Hash();
    };
    CHECK(hashOf([](core::WorldMapSource& data) { data.kinds[MEADOW].cost = 11; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.kinds[MEADOW].name = "grass"; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.grid[4] = SAND; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.tileSize = HALF_TILE; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.roadCost = 5; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.fordCost = 31; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.roads[0].push_back({ .column = 1, .row = 2 }); }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.rivers[0][1].row = 1; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.fords[1].row = 2; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.kinds[MEADOW].symbol = 'm'; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) {
              data.places[0].start = false;
              data.places[1].start = true;
          }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.places[2].sort = "cliff"; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.places[2].tile.row = 1; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.places[1].people = 101; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.places[1].nameKey = "other"; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.places[1].key = "other"; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) {
              data.places[2].kind = core::PlaceKind::City;
              data.places[2].sort = "";
          }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.places[2].name = core::StringId{ 1 }; }) == reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.lines.places = { 3, 4, 5 }; }) == reference);

    // The same 16 tiles as 8 by 2 and as 4 by 4.
    const core::WorldMapSource wide = Source({ "0..1....", "........" });
    core::WorldMapSource square = wide;
    square.columns = 4;
    square.rows = 4;
    const u64 wideHash = MustBuild(wide).Hash();
    const u64 squareHash = MustBuild(square).Hash();
    CHECK(wideHash != squareHash);
}

constexpr u32 ROAD_TILE = 2;
constexpr u32 BRIDGE_TILE = 3;
constexpr u32 FORD_TILE = 4;
constexpr u32 RIVER_TILE = 5;
constexpr u32 DEEP_TILE = 8;

// Columns: 0 the city, 1 meadow, 2 road, 3 road and river (a bridge), 4 river with a ford, 5 river; row 1 has deep water
// under column 2.
core::WorldMapSource ProfileSource() {
    core::WorldMapSource source = Source({ "0.....", "..~..." });
    source.roads = { { { .column = 2, .row = 0 } }, { { .column = 3, .row = 0 } } };
    source.rivers = { { { .column = 3, .row = 0 }, { .column = RIVER_TILE, .row = 0 } } };
    source.fords = { { .column = 4, .row = 0 } };
    return source;
}

constexpr u16 WATER_RIVER_COST = 5;
constexpr u16 WATER_FORD_COST = 6;
constexpr u16 SAND_SHALLOWS_COST = 8; // the water profile reads the sand kind as shallows
constexpr u16 ROADS_ONLY_COST = 3;
constexpr u16 ROADS_ONLY_PACE = 12;

core::MovementProfile Profile(std::string key, u16 kindCost) {
    return { .key = std::move(key),
             .ticksPerCost = core::DEFAULT_TICKS_PER_COST,
             .kindCosts = std::vector<u16>(Source({ "0" }).kinds.size(), kindCost),
             .features = { .road = ROAD_COST, .river = core::IMPASSABLE_COST, .bridge = ROAD_COST, .ford = FORD_COST } };
}

TEST_CASE("a map built with no profiles walks on foot by exactly its base costs") {
    const core::WorldMap map = MustBuild(ProfileSource());
    REQUIRE(map.Profiles().size() == 1);
    CHECK(map.Profiles()[0].key == "on_foot");
    CHECK(map.Profiles()[0].ticksPerCost == 16);
    CHECK(core::WALKING == 0);
    for (const u32 tile : { 1U, ROAD_TILE, BRIDGE_TILE, FORD_TILE, RIVER_TILE, DEEP_TILE }) {
        CAPTURE(tile); // a meadow, a road, a bridge, a ford, a river, deep water
        CHECK(map.CostAt(tile, core::WALKING) == map.CostAt(tile));
    }
    // Orthogonal and diagonal steps, onto each kind of tile and past an impassable corner.
    const std::vector<std::pair<u32, u32>> steps = { { 0, 1 },
                                                     { 1, ROAD_TILE },
                                                     { ROAD_TILE, BRIDGE_TILE },
                                                     { BRIDGE_TILE, FORD_TILE },
                                                     { FORD_TILE, RIVER_TILE },
                                                     { 7, DEEP_TILE },
                                                     { 6, 1 },
                                                     { 0, 7 },
                                                     { 1, 8 },
                                                     { 7, ROAD_TILE },
                                                     { 9, FORD_TILE },
                                                     { 1, 0 } };
    for (const auto& [from, target] : steps) {
        CAPTURE(from); // orthogonal steps onto each tile kind, then diagonals past meadow, deep water and a ford, and one back
        CAPTURE(target);
        CHECK(map.StepCost(from, target, core::WALKING) == map.StepCost(from, target));
    }
    CHECK(map.StepCost(0, 7, core::WALKING) == MEADOW_COST * 7 / 5);
}

TEST_CASE("a road-only profile walks roads and bridges and nothing else") {
    core::WorldMapSource source = ProfileSource();
    core::MovementProfile roads = Profile("roads", core::IMPASSABLE_COST);
    roads.ticksPerCost = ROADS_ONLY_PACE;
    roads.features = {
        .road = ROADS_ONLY_COST, .river = core::IMPASSABLE_COST, .bridge = ROADS_ONLY_COST, .ford = core::IMPASSABLE_COST
    };
    source.profiles = { std::move(roads) };
    const core::WorldMap map = MustBuild(std::move(source));
    REQUIRE(map.Profiles().size() == 1);
    CHECK(map.Profiles()[0].ticksPerCost == ROADS_ONLY_PACE);
    CHECK(map.CostAt(ROAD_TILE, core::WALKING) == ROADS_ONLY_COST);
    CHECK(map.CostAt(BRIDGE_TILE, core::WALKING) == ROADS_ONLY_COST);
    CHECK(map.CostAt(1, core::WALKING) == core::IMPASSABLE_COST);
    CHECK(map.CostAt(FORD_TILE, core::WALKING) == core::IMPASSABLE_COST);
    CHECK(map.StepCost(ROAD_TILE, BRIDGE_TILE, core::WALKING) == ROADS_ONLY_COST);
    CHECK(map.StepCost(1, ROAD_TILE, core::WALKING) == ROADS_ONLY_COST);
    CHECK(map.StepCost(ROAD_TILE, 1, core::WALKING) == core::IMPASSABLE_COST);
}

TEST_CASE("a water profile crosses rivers and shallows and not land") {
    core::WorldMapSource source = ProfileSource();
    core::MovementProfile water = Profile("water", core::IMPASSABLE_COST);
    water.kindCosts[SAND] = SAND_SHALLOWS_COST;
    water.features = {
        .road = core::IMPASSABLE_COST, .river = WATER_RIVER_COST, .bridge = core::IMPASSABLE_COST, .ford = WATER_FORD_COST
    };
    source.profiles = { Profile("on_foot", MEADOW_COST), std::move(water) };
    source.grid[1] = SAND;
    const core::WorldMap map = MustBuild(std::move(source));
    REQUIRE(map.Profiles().size() == 2);
    constexpr core::ProfileIndex WATER = 1;
    CHECK(map.CostAt(RIVER_TILE, WATER) == WATER_RIVER_COST);
    CHECK(map.CostAt(FORD_TILE, WATER) == WATER_FORD_COST);
    CHECK(map.CostAt(1, WATER) == SAND_SHALLOWS_COST);
    CHECK(map.CostAt(0, WATER) == core::IMPASSABLE_COST);
    CHECK(map.CostAt(ROAD_TILE, WATER) == core::IMPASSABLE_COST);
    CHECK(map.CostAt(BRIDGE_TILE, WATER) == core::IMPASSABLE_COST);
    CHECK(map.StepCost(RIVER_TILE, FORD_TILE, WATER) == WATER_FORD_COST);
}

TEST_CASE("Build refuses a profile it can't use") {
    const auto refusal = [](const std::function<void(std::vector<core::MovementProfile>&)>& change) {
        core::WorldMapSource source = ProfileSource();
        source.profiles = { Profile("walk", MEADOW_COST) };
        change(source.profiles);
        return Refusal(std::move(source));
    };
    CHECK(refusal([](auto& profiles) { profiles[0].ticksPerCost = 0; }).contains("no ticks per cost"));
    CHECK(refusal([](auto& profiles) { profiles[0].kindCosts.pop_back(); }).contains("costs 3 terrain kinds"));
    CHECK(refusal([](auto& profiles) { profiles[0].kindCosts[0] = core::MAX_WALK_COST + 1; }).contains("a terrain kind over"));
    CHECK(refusal([](auto& profiles) { profiles[0].features.ford = core::MAX_WALK_COST + 1; }).contains("a feature over"));
    CHECK(refusal([](auto& profiles) { profiles[0].kindCosts[0] = 0; }).contains("a terrain kind 0"));
    CHECK(refusal([](auto& profiles) { profiles[0].features.road = 0; }).contains("a feature 0"));
    CHECK(refusal([](auto& profiles) { profiles.push_back(Profile("walk", 1)); }).contains("shares its key"));
    CHECK(refusal([](auto& profiles) { profiles[0].key = ""; }).contains("has no key"));
    CHECK(refusal([](auto& profiles) { profiles[0].features.danger = 1; }).contains("everyone's walk"));
    CHECK(refusal([](auto& profiles) {
              profiles.push_back(Profile("fear", MEADOW_COST));
              profiles[1].features.danger = core::MAX_WALK_COST + 1;
          }).contains("danger cost over"));
    CHECK(refusal([](auto& profiles) {
              profiles.assign(core::MOST_PROFILES + 1, Profile("walk", 1));
              for (std::size_t index = 0; index < profiles.size(); ++index) {
                  profiles[index].key = std::format("p{}", index);
              }
          }).contains("17 profiles"));
}

constexpr u32 DANGER_REACH = 2;
constexpr u32 DANGER_MAP_SIDE = 9;
// Wide enough to flag every tile of the cost test's map from its one danger.
constexpr u8 WHOLE_MAP_REACH = 9;
constexpr u16 DANGER_COST = 30;
constexpr u16 DANGER_KIND_COST = 990;
constexpr core::ProfileIndex FEAR = 1;

// `source` with a danger place on `tile`.
void AddDanger(core::WorldMapSource& source, core::TilePoint tile) {
    source.places.push_back(
        { .key = "danger",
          .kind = core::PlaceKind::Danger,
          .sort = "cave",
          .tile = tile,
          .nameKey = "place_danger",
          .name = {},
          .people = 0,
          .start = false }
    );
}

bool Flagged(const core::WorldMap& map, u32 column, u32 row) {
    return (map.FlagsAt(map.TileIndex({ .column = column, .row = row })) & std::to_underlying(core::TileFlag::Danger)) != 0;
}

TEST_CASE("a danger's reach flags the tiles within it") {
    std::vector<std::string> rows(DANGER_MAP_SIDE, ".........");
    rows.front()[0] = '0';
    core::WorldMapSource source = Source(rows);
    AddDanger(source, { .column = 4, .row = 4 });
    source.dangerReach = DANGER_REACH;
    const core::WorldMap map = MustBuild(source);
    CHECK(Flagged(map, 4, 4));
    CHECK(Flagged(map, 4, 2));
    CHECK(Flagged(map, 6, 4));
    CHECK(Flagged(map, 5, 5));
    CHECK_FALSE(Flagged(map, 6, 6));
    CHECK_FALSE(Flagged(map, 4, 1));
    CHECK_FALSE(Flagged(map, 0, 0));
    source.dangerReach = 0;
    const core::WorldMap none = MustBuild(source);
    for (u32 tile = 0; tile < DANGER_MAP_SIDE * DANGER_MAP_SIDE; ++tile) {
        CHECK((none.FlagsAt(tile) & std::to_underlying(core::TileFlag::Danger)) == 0);
    }
}

TEST_CASE("Build refuses a danger reach past the most") {
    core::WorldMapSource source = Source({ "0.." });
    source.dangerReach = core::MAX_DANGER_REACH + 1;
    CHECK(Refusal(std::move(source)).contains("danger reach"));
}

TEST_CASE("a profile's danger cost adds after the road and caps") {
    // Columns: 0 the city, 1 meadow, 2 road, 3 sand, 4 the danger, 6 deep water.
    core::WorldMapSource source = Source({ "0..,..~", "......." });
    source.roads = { { { .column = 2, .row = 0 } } };
    AddDanger(source, { .column = 4, .row = 0 });
    source.dangerReach = WHOLE_MAP_REACH;
    core::MovementProfile walk = Profile("walk", MEADOW_COST);
    walk.kindCosts[DEEP] = core::IMPASSABLE_COST;
    core::MovementProfile fear = walk;
    fear.key = "fear";
    fear.features.danger = DANGER_COST;
    fear.kindCosts[SAND] = DANGER_KIND_COST;
    source.profiles = { walk, fear };
    const core::WorldMap map = MustBuild(std::move(source));
    CHECK(map.CostAt(1, FEAR) == MEADOW_COST + DANGER_COST);
    CHECK(map.CostAt(2, FEAR) == ROAD_COST + DANGER_COST);
    CHECK(map.CostAt(6, FEAR) == core::IMPASSABLE_COST);
    CHECK(map.CostAt(3, FEAR) == core::MAX_WALK_COST);
    CHECK(map.CostAt(1, core::WALKING) == MEADOW_COST);
    CHECK(map.CostAt(2, core::WALKING) == ROAD_COST);
}

TEST_CASE("the base costs ignore danger") {
    // City 0 starts on a flagged stretch (columns 0 to 4); a base layer that priced it would hand tile 4 to city 1.
    core::WorldMapSource source = Source({ "0........1" });
    AddDanger(source, { .column = 2, .row = 0 });
    source.dangerReach = 2;
    const core::MovementProfile walk = Profile("walk", MEADOW_COST);
    core::MovementProfile fear = walk;
    fear.key = "fear";
    fear.features.danger = DANGER_KIND_COST;
    source.profiles = { walk, fear };
    const core::WorldMap map = MustBuild(std::move(source));
    CHECK(Flagged(map, 4, 0));
    CHECK_FALSE(Flagged(map, 5, 0));
    CHECK(map.CostAt(4) == MEADOW_COST);
    CHECK(map.RegionAt(4) == 0);
    CHECK(map.RegionAt(5) == 1);
    CHECK(map.CostAt(4, FEAR) == core::MAX_WALK_COST);
}

TEST_CASE("the hash changes with danger reach and danger costs") {
    const auto hashOf = [](const std::function<void(core::WorldMapSource&)>& change) {
        core::WorldMapSource source = ProfileSource();
        source.profiles = { Profile("walk", MEADOW_COST), Profile("fear", MEADOW_COST) };
        AddDanger(source, { .column = 5, .row = 1 });
        change(source);
        return MustBuild(std::move(source)).Hash();
    };
    const u64 reference = hashOf([](core::WorldMapSource&) {});
    CHECK(hashOf([](core::WorldMapSource& source) { source.dangerReach = 1; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& source) { source.profiles[1].features.danger = 1; }) != reference);
}

TEST_CASE("the hash changes with a profile's pace or cost") {
    const auto hashOf = [](const std::function<void(core::MovementProfile&)>& change) {
        core::WorldMapSource source = ProfileSource();
        source.profiles = { Profile("walk", MEADOW_COST) };
        change(source.profiles[0]);
        return MustBuild(std::move(source)).Hash();
    };
    const u64 reference = hashOf([](core::MovementProfile&) {});
    CHECK(hashOf([](core::MovementProfile& profile) { profile.ticksPerCost = 17; }) != reference);
    CHECK(hashOf([](core::MovementProfile& profile) { profile.kindCosts[SAND] = 11; }) != reference);
    CHECK(hashOf([](core::MovementProfile& profile) { profile.features.road = 2; }) != reference);
    CHECK(hashOf([](core::MovementProfile& profile) { profile.key = "stroll"; }) != reference);
}

constexpr u16 PAST_PER_MILLE = 1'001;
constexpr u8 STAY_AFTER_MAX = 6;
constexpr f32 OTHER_POWER = 6.0F;

TEST_CASE("a city draws one of each visitor by default, and Build zeroes a danger's draws") {
    core::WorldMapSource source = Source({ "0.1", "..." });
    source.places.push_back(
        { .key = "cave", .kind = core::PlaceKind::Danger, .sort = "cave", .tile = { .column = 1, .row = 1 } }
    );
    const core::WorldMap map = MustBuild(std::move(source));
    CHECK(map.Places()[0].draws == std::array<u16, core::VISITOR_KINDS>{ { 1, 1 } });
    CHECK(map.Places()[2].draws == std::array<u16, core::VISITOR_KINDS>{ { 0, 0 } });
}

TEST_CASE("Build refuses a draw past the most and a city with no draw") {
    core::WorldMapSource tooMuch = Source({ "0.1" });
    tooMuch.places[1].draws = { { 0, core::MOST_DRAW + 1 } };
    const std::string tooMuchRefusal = Refusal(std::move(tooMuch));
    CHECK(tooMuchRefusal.contains("past the most, 100"));
    core::WorldMapSource none = Source({ "0.1" });
    none.places[1].draws = { { 0, 0 } };
    const std::string noneRefusal = Refusal(std::move(none));
    CHECK(noneRefusal.contains("draws no visitors"));
}

TEST_CASE("Build refuses travel rules out of order or past a thousand") {
    core::WorldMapSource share = Source({ "0.1" });
    share.travel.sharePerMille = PAST_PER_MILLE;
    const std::string shareRefusal = Refusal(std::move(share));
    CHECK(shareRefusal.contains("over a thousand"));
    core::WorldMapSource backwards = Source({ "0.1" });
    backwards.travel.stayMinDays = STAY_AFTER_MAX;
    const std::string backwardsRefusal = Refusal(std::move(backwards));
    CHECK(backwardsRefusal.contains("in order"));
    core::WorldMapSource zero = Source({ "0.1" });
    zero.travel.stayMinDays = 0;
    const std::string zeroRefusal = Refusal(std::move(zero));
    CHECK(zeroRefusal.contains("in order"));
}

TEST_CASE("a map with other travel rules keeps all else and hashes apart; past a thousand it is refused") {
    constexpr u16 RAISED_SHARE = 200;
    const core::WorldMap map = MustBuild(Source({ "0.1" }));
    core::TravelRules raised = map.Travel();
    raised.sharePerMille = RAISED_SHARE;
    const std::expected<core::WorldMap, core::Error> other = map.WithTravel(raised);
    REQUIRE(other.has_value());
    CHECK(other->Travel().sharePerMille == RAISED_SHARE);
    CHECK(other->Travel().stayMaxDays == map.Travel().stayMaxDays);
    CHECK(other->Columns() == map.Columns());
    CHECK(other->Places().size() == map.Places().size());
    CHECK(other->Hash() != map.Hash());
    raised.sharePerMille = PAST_PER_MILLE;
    const std::expected<core::WorldMap, core::Error> refused = map.WithTravel(raised);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.contains("over a thousand"));
}

TEST_CASE("the hash changes with a draw or a travel rule") {
    const u64 reference = MustBuild(Source({ "0.1" })).Hash();
    const auto hashOf = [](const std::function<void(core::WorldMapSource&)>& change) {
        core::WorldMapSource source = Source({ "0.1" });
        change(source);
        return MustBuild(std::move(source)).Hash();
    };
    CHECK(hashOf([](core::WorldMapSource& data) { data.places[1].draws = { { 1, 2 } }; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.travel.sharePerMille = 41; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.travel.stayMaxDays = 6; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.travel.reachBase = 1; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.travel.reachPerPower = 1; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.travel.cultivatorPower = OTHER_POWER; }) != reference);
}

} // namespace
