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

bool Contains(const std::string& text, std::string_view part) {
    return text.contains(part);
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
}

TEST_CASE("each tile's region is its nearest city by walking cost") {
    const core::WorldMap map = MustBuild(Source({ "..0^.....1", "...^......", "...^......", ".........." }));
    // Four tiles from city 0 in a straight line, but a long walk round the ridge; city 1 is five tiles away.
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
    CHECK(Contains(Refusal(overWater), "road 0"));

    core::WorldMapSource overPeaks = Source({ "0.^", "..." });
    overPeaks.roads = { { { .column = 0, .row = 1 }, { .column = 1, .row = 1 } }, { { .column = 2, .row = 0 } } };
    CHECK(Contains(Refusal(overPeaks), "road 1"));

    core::WorldMapSource onWater = Source({ "0.~" });
    onWater.places.push_back(
        { .key = "pit", .kind = core::PlaceKind::Danger, .sort = "pit", .tile = { .column = 2, .row = 0 }, .nameKey = "pit" }
    );
    CHECK(Contains(Refusal(onWater), "place 1"));

    core::WorldMapSource crowded = Source({ "0.." });
    crowded.places.push_back(
        { .key = "twin", .kind = core::PlaceKind::City, .sort = "", .tile = { .column = 0, .row = 0 }, .nameKey = "twin" }
    );
    CHECK(Contains(Refusal(crowded), "place 1"));

    core::WorldMapSource noStart = Source({ "0.1" });
    noStart.places[0].start = false;
    CHECK(Contains(Refusal(noStart), "no place starts"));

    core::WorldMapSource twoStarts = Source({ "0.1" });
    twoStarts.places[1].start = true;
    CHECK(Contains(Refusal(twoStarts), "places 0 and 1"));

    CHECK(Contains(Refusal(Source({ "0^1" })), "place 1"));

    core::WorldMapSource offMap = Source({ "0..", "..." });
    offMap.roads = { { { .column = 0, .row = 0 } }, { { .column = 1, .row = 1 }, { .column = 3, .row = 1 } } };
    CHECK(Contains(Refusal(offMap), "road 1"));

    core::WorldMapSource riverOff = Source({ "0.." });
    riverOff.rivers = { { { .column = 0, .row = OFF_MAP } } };
    CHECK(Contains(Refusal(riverOff), "river 0"));

    core::WorldMapSource dryFord = Source({ "0.." });
    dryFord.fords = { { .column = 1, .row = 0 } };
    CHECK(Contains(Refusal(dryFord), "ford 0"));

    core::WorldMapSource fordOff = Source({ "0.." });
    fordOff.fords = { { .column = OFF_MAP, .row = 0 } };
    CHECK(Contains(Refusal(fordOff), "ford 0"));

    core::WorldMapSource sameKey = Source({ "0.1" });
    sameKey.places[1].key = sameKey.places[0].key;
    const std::string sameKeyRefusal = Refusal(sameKey);
    CHECK(Contains(sameKeyRefusal, "'city0'"));
    CHECK(Contains(sameKeyRefusal, "places 0 and 1"));

    core::WorldMapSource shortGrid = Source({ "0.." });
    shortGrid.grid.pop_back();
    CHECK(Contains(Refusal(shortGrid), "grid"));
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
    core::WorldMapSource base = Source({ "0..~..", "..~...", "..,..1" });
    base.rivers = { { { .column = 3, .row = 0 }, { .column = 2, .row = 1 } } };
    base.roads = { { { .column = 0, .row = 2 }, { .column = 1, .row = 2 } } };
    base.fords = { { .column = 2, .row = 1 } };
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
    CHECK(hashOf([](core::WorldMapSource& data) {
              data.rivers[0][1].column = 3;
              data.fords[0].column = 3;
          }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.places[2].sort = "cliff"; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.places[2].tile.row = 1; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.places[1].people = 101; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.places[1].nameKey = "other"; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.places[1].key = "other"; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.places[2].kind = core::PlaceKind::City; }) != reference);
    CHECK(hashOf([](core::WorldMapSource& data) { data.places[2].name = core::StringId{ 1 }; }) == reference);
}

} // namespace
