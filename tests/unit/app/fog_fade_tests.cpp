#include "app/fog_fade.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/fog.hpp"
#include "core/frame.hpp"
#include "core/world_map.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 SIDE = 5;
constexpr std::size_t TILES = std::size_t{ SIDE } * SIDE;
constexpr f32 FADE = 0.5F;
constexpr f32 QUARTER = 0.25F;
constexpr u32 CENTRE_TILE = 12; // column 2, row 2
constexpr u32 FIRST_TILE = 0;
constexpr u32 RADIUS = 1;

// A meadow of SIDE x SIDE tiles with one city, which is all a map needs to build.
core::WorldMap SmallMap() {
    core::WorldMapSource source;
    source.columns = SIDE;
    source.rows = SIDE;
    source.tileSize = 256.0F;
    source.kinds = { { .name = "meadow", .symbol = '.', .cost = 10 } };
    source.grid.assign(TILES, 0);
    source.places.push_back(
        { .key = "city",
          .kind = core::PlaceKind::City,
          .sort = "",
          .tile = { .column = 0, .row = 0 },
          .nameKey = "city",
          .name = {},
          .people = 100,
          .start = true }
    );
    auto built = core::WorldMap::Build(std::move(source));
    REQUIRE(built.has_value());
    return *std::move(built);
}

core::Snapshot Shows(u64 version, std::optional<u32> player, u32 explored) {
    core::Snapshot snapshot;
    snapshot.epoch = 1;
    snapshot.sceneChange.change = 1;
    snapshot.fog.on = true;
    snapshot.fog.version = version;
    snapshot.fog.explored.assign(core::FogWords(TILES), 0);
    snapshot.fog.explored.front() = u64{ 1 } << explored;
    snapshot.fog.playerTile = player;
    snapshot.fog.sightRadius = RADIUS;
    return snapshot;
}

u8 Value(const app::FogFade& fade, u32 tile, u32 channel) {
    return fade.Frame().values[(std::size_t{ tile } * 2) + channel];
}

TEST_CASE("a newly explored tile eases to explored over the fade time and holds") {
    const core::WorldMap map = SmallMap();
    app::FogFade fade;
    fade.Update(Shows(1, std::nullopt, FIRST_TILE), map, 0.0F, FADE);
    CHECK(Value(fade, CENTRE_TILE, 0) == 0);
    const core::Snapshot next = Shows(2, std::nullopt, CENTRE_TILE);
    fade.Update(next, map, QUARTER, FADE);
    CHECK(Value(fade, CENTRE_TILE, 0) == doctest::Approx(127.5).epsilon(0.02));
    fade.Update(next, map, QUARTER, FADE);
    CHECK(Value(fade, CENTRE_TILE, 0) == 255);
    fade.Update(next, map, 1.0F, FADE);
    CHECK(Value(fade, CENTRE_TILE, 0) == 255);
}

TEST_CASE("the first fog, a new epoch, a scene change and a debug sight switch snap the values") {
    const core::WorldMap map = SmallMap();
    app::FogFade fade;
    fade.Update(Shows(1, CENTRE_TILE, FIRST_TILE), map, 0.0F, FADE);
    CHECK(Value(fade, CENTRE_TILE, 1) == 255);
    CHECK(Value(fade, FIRST_TILE, 0) == 255);

    core::Snapshot epoch = Shows(2, std::nullopt, CENTRE_TILE);
    epoch.epoch = 2;
    fade.Update(epoch, map, 0.0F, FADE);
    CHECK(Value(fade, CENTRE_TILE, 1) == 0);
    CHECK(Value(fade, CENTRE_TILE, 0) == 255);

    core::Snapshot scene = Shows(3, CENTRE_TILE, FIRST_TILE);
    scene.epoch = 2;
    scene.sceneChange.change = 2;
    fade.Update(scene, map, 0.0F, FADE);
    CHECK(Value(fade, CENTRE_TILE, 1) == 255);
    CHECK(Value(fade, CENTRE_TILE, 0) == 0);

    core::Snapshot debug = scene;
    debug.fog.debugSight = true;
    fade.Update(debug, map, 0.0F, FADE);
    CHECK(Value(fade, FIRST_TILE, 1) == 255);
}

TEST_CASE("debug sight puts every tile in sight and explores none") {
    const core::WorldMap map = SmallMap();
    core::Snapshot snapshot = Shows(1, std::nullopt, CENTRE_TILE);
    snapshot.fog.debugSight = true;
    app::FogFade fade;
    fade.Update(snapshot, map, 0.0F, FADE);
    for (u32 tile = 0; tile < TILES; ++tile) {
        CHECK(Value(fade, tile, 1) == 255);
        CHECK(Value(fade, tile, 0) == (tile == CENTRE_TILE ? 255 : 0));
    }
}

TEST_CASE("leaving sight eases out while explored stays") {
    const core::WorldMap map = SmallMap();
    app::FogFade fade;
    fade.Update(Shows(1, CENTRE_TILE, CENTRE_TILE), map, 0.0F, FADE);
    CHECK(Value(fade, CENTRE_TILE, 1) == 255);
    const core::Snapshot gone = Shows(1, FIRST_TILE, CENTRE_TILE);
    fade.Update(gone, map, QUARTER, FADE);
    CHECK(Value(fade, CENTRE_TILE, 1) == doctest::Approx(127.5).epsilon(0.02));
    CHECK(Value(fade, CENTRE_TILE, 0) == 255);
    fade.Update(gone, map, QUARTER, FADE);
    CHECK(Value(fade, CENTRE_TILE, 1) == 0);
    CHECK(Value(fade, CENTRE_TILE, 0) == 255);
}

TEST_CASE("a settled field keeps its revision") {
    const core::WorldMap map = SmallMap();
    app::FogFade fade;
    const core::Snapshot snapshot = Shows(1, CENTRE_TILE, CENTRE_TILE);
    fade.Update(snapshot, map, 0.0F, FADE);
    const u64 revision = fade.Frame().revision;
    fade.Update(snapshot, map, QUARTER, FADE);
    CHECK(fade.Frame().revision == revision);
}

TEST_CASE("no fog in the snapshot gives a frame with no columns") {
    const core::WorldMap map = SmallMap();
    app::FogFade fade;
    fade.Update(Shows(1, CENTRE_TILE, CENTRE_TILE), map, 0.0F, FADE);
    CHECK(fade.Frame().columns == SIDE);
    const core::Snapshot off;
    fade.Update(off, map, QUARTER, FADE);
    CHECK(fade.Frame().columns == 0);
}

} // namespace
