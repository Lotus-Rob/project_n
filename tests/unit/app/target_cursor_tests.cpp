#include "app/target_cursor.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/world_map.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 COLUMNS = 5;
constexpr u32 ROWS = 4;
constexpr u32 MIDDLE = (1 * COLUMNS) + 2; // column 2, row 1
constexpr f32 TILE = 256.0F;
constexpr core::Vec2 RIGHT{ .x = 1.0F, .y = 0.0F };
constexpr core::Vec2 NORTH{ .x = 0.0F, .y = -1.0F };
constexpr core::Vec2 LEFT{ .x = -1.0F, .y = 0.0F };
constexpr core::Vec2 REST{};
constexpr f32 FRAME = 1.0F / 60.0F;
constexpr f32 COLUMN_CENTRE = 2.5F; // the middle tile's column, from the map's edge, in tiles
constexpr f32 ROW_CENTRE = 1.5F;
constexpr f32 HALF_PRESSED = 0.4F;        // a stick axis short of the cursor's threshold
constexpr f32 PAST_THRESHOLD = 0.7F;      // and past it
constexpr f32 JUST_BEFORE_REPEAT = 0.29F; // of the 0.3 s pause
constexpr f32 JUST_AFTER_REPEAT = 0.02F;
constexpr f32 PART_OF_INTERVAL = 0.10F; // of the 0.15 s between repeats
constexpr f32 REST_OF_INTERVAL = 0.06F;
constexpr f32 TWO_REPEATS = 0.46F; // the pause, one interval and a little over
constexpr f32 SHORT_OF_PAUSE = 0.2F;
constexpr f32 PAUSE = 0.31F;       // a little over the first repeat's 0.3 s
constexpr f32 FOOT_OFFSET = 10.0F; // below the tile's middle, still on it
constexpr u16 ROAD_COST = 4;
constexpr u16 FORD_COST = 30;
constexpr u16 MEADOW_COST = 10;
constexpr u32 PEOPLE = 100;

app::TargetCursor ShownAt(u32 tile) {
    app::TargetCursor cursor;
    cursor.Show(tile, COLUMNS, ROWS);
    return cursor;
}

core::WorldMap MapOfFiveByFour() {
    core::WorldMapSource source;
    source.columns = COLUMNS;
    source.rows = ROWS;
    source.tileSize = TILE;
    source.roadCost = ROAD_COST;
    source.fordCost = FORD_COST;
    source.kinds = { { .name = "meadow", .symbol = '.', .cost = MEADOW_COST } };
    source.grid.assign(static_cast<std::size_t>(COLUMNS) * ROWS, 0);
    source.places.push_back(
        { .key = "city0",
          .kind = core::PlaceKind::City,
          .sort = "",
          .tile = { .column = 0, .row = 0 },
          .nameKey = "city_0",
          .name = {},
          .people = PEOPLE,
          .start = true }
    );
    auto built = core::WorldMap::Build(std::move(source));
    REQUIRE(built.has_value());
    return *std::move(built);
}

TEST_CASE("the cursor starts on the cultivator's tile") {
    const core::WorldMap map = MapOfFiveByFour();
    const core::Vec2 foot{ .x = COLUMN_CENTRE * TILE, .y = (ROW_CENTRE * TILE) + FOOT_OFFSET };
    const std::optional<u32> start = app::CursorStart(true, foot, map);
    CHECK(start == MIDDLE);
    app::TargetCursor cursor;
    CHECK_FALSE(cursor.Shown());
    cursor.Show(MIDDLE, COLUMNS, ROWS);
    CHECK(cursor.Tile() == MIDDLE);
}

TEST_CASE("the cursor doesn't show off the world map or without a cultivator") {
    const core::WorldMap map = MapOfFiveByFour();
    const std::optional<u32> offMap = app::CursorStart(false, core::Vec2{}, map);
    const std::optional<u32> noCultivator = app::CursorStart(true, std::nullopt, map);
    CHECK_FALSE(offMap.has_value());
    CHECK_FALSE(noCultivator.has_value());
}

TEST_CASE("the cultivator doesn't walk while the cursor shows") {
    app::TargetCursor cursor;
    CHECK(cursor.Walk(RIGHT).x == doctest::Approx(1.0F));
    cursor.Show(MIDDLE, COLUMNS, ROWS);
    CHECK(cursor.Walk(RIGHT).x == 0.0F);
    cursor.Hide();
    CHECK(cursor.Walk(RIGHT).x == doctest::Approx(1.0F));
}

TEST_CASE("after confirming, a held direction doesn't walk until released once") {
    const core::WorldMap map = MapOfFiveByFour();
    app::TargetCursor cursor = ShownAt(MIDDLE);
    static_cast<void>(cursor.Confirm(map));
    CHECK(cursor.Walk(RIGHT).x == 0.0F);
    CHECK(cursor.Walk(RIGHT).x == 0.0F);
    CHECK(cursor.Walk(REST).x == 0.0F);
    CHECK(cursor.Walk(RIGHT).x == doctest::Approx(1.0F));
}

TEST_CASE("a direction held when the cursor shows doesn't step it until pressed afresh or held") {
    app::TargetCursor cursor;
    cursor.Show(MIDDLE, COLUMNS, ROWS, RIGHT);
    cursor.Steer(RIGHT, FRAME);
    CHECK(cursor.Tile() == MIDDLE);
    cursor.Steer(RIGHT, PAUSE);
    CHECK(cursor.Tile() == MIDDLE + 1);
    app::TargetCursor released;
    released.Show(MIDDLE, COLUMNS, ROWS, RIGHT);
    released.Steer(REST, FRAME);
    released.Steer(RIGHT, FRAME);
    CHECK(released.Tile() == MIDDLE + 1);
}

TEST_CASE("a press steps the cursor one tile") {
    app::TargetCursor cursor = ShownAt(MIDDLE);
    cursor.Steer(RIGHT, FRAME);
    CHECK(cursor.Tile() == MIDDLE + 1);
    cursor.Steer(REST, FRAME);
    cursor.Steer(NORTH, FRAME);
    CHECK(cursor.Tile() == MIDDLE + 1 - COLUMNS);
}

TEST_CASE("a stick axis steps once past half its travel, and a diagonal steps both ways") {
    app::TargetCursor cursor = ShownAt(MIDDLE);
    cursor.Steer({ .x = HALF_PRESSED, .y = 0.0F }, FRAME);
    CHECK(cursor.Tile() == MIDDLE);
    cursor.Steer({ .x = PAST_THRESHOLD, .y = -PAST_THRESHOLD }, FRAME);
    CHECK(cursor.Tile() == MIDDLE + 1 - COLUMNS);
}

TEST_CASE("held, it repeats after a pause") {
    app::TargetCursor cursor = ShownAt(0);
    cursor.Steer(RIGHT, FRAME);
    CHECK(cursor.Tile() == 1);
    cursor.Steer(RIGHT, JUST_BEFORE_REPEAT);
    CHECK(cursor.Tile() == 1);
    cursor.Steer(RIGHT, JUST_AFTER_REPEAT);
    CHECK(cursor.Tile() == 2);
    cursor.Steer(RIGHT, PART_OF_INTERVAL);
    CHECK(cursor.Tile() == 2);
    cursor.Steer(RIGHT, REST_OF_INTERVAL);
    CHECK(cursor.Tile() == 3);
}

TEST_CASE("a long frame steps once, and a release starts the pause afresh") {
    app::TargetCursor cursor = ShownAt(0);
    cursor.Steer(RIGHT, FRAME);
    cursor.Steer(RIGHT, TWO_REPEATS);
    CHECK(cursor.Tile() == 2);
    cursor.Steer(REST, FRAME);
    cursor.Steer(RIGHT, FRAME);
    CHECK(cursor.Tile() == 3);
    cursor.Steer(RIGHT, SHORT_OF_PAUSE);
    CHECK(cursor.Tile() == 3);
}

TEST_CASE("the cursor stops at the map's edge") {
    app::TargetCursor cursor = ShownAt(0);
    cursor.Steer(LEFT, FRAME);
    CHECK(cursor.Tile() == 0);
    cursor.Steer(REST, FRAME);
    cursor.Steer(NORTH, FRAME);
    CHECK(cursor.Tile() == 0);
    app::TargetCursor corner = ShownAt((ROWS * COLUMNS) - 1);
    corner.Steer({ .x = 1.0F, .y = 1.0F }, FRAME);
    CHECK(corner.Tile() == (ROWS * COLUMNS) - 1);
    app::TargetCursor edge = ShownAt(COLUMNS - 1);
    edge.Steer({ .x = 1.0F, .y = 1.0F }, FRAME);
    CHECK(edge.Tile() == (2 * COLUMNS) - 1); // the row still moved, the column held
}

TEST_CASE("confirming walks to the tile and hides the cursor") {
    const core::WorldMap map = MapOfFiveByFour();
    app::TargetCursor cursor = ShownAt(MIDDLE);
    const std::optional<core::Action> action = cursor.Confirm(map);
    REQUIRE(action.has_value());
    CHECK(action.value_or(core::Action{}).kind == core::ActionKind::WalkTo);
    CHECK(action.value_or(core::Action{}).point.x == doctest::Approx(COLUMN_CENTRE * TILE));
    CHECK(action.value_or(core::Action{}).point.y == doctest::Approx(ROW_CENTRE * TILE));
    CHECK_FALSE(cursor.Shown());
}

TEST_CASE("a hidden cursor neither steers nor confirms") {
    const core::WorldMap map = MapOfFiveByFour();
    app::TargetCursor cursor = ShownAt(MIDDLE);
    cursor.Hide();
    cursor.Steer(RIGHT, FRAME);
    CHECK_FALSE(cursor.Tile().has_value());
    CHECK_FALSE(cursor.Confirm(map).has_value());
}

} // namespace
