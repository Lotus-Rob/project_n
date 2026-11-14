#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/jobs.hpp"
#include "game/host.hpp"
#include "game/scenes.hpp"
#include "game/scenes/world.hpp"
#include "support/manifest.hpp"

#include <doctest/doctest.h>

namespace {

TEST_CASE("names round-trip") {
    for (const game::Scene* const scene : game::Scenes()) {
        const std::string_view name = scene->Traits().name;
        const std::optional<core::SceneKey> key = game::SceneNamed(name);
        REQUIRE(key.has_value());
        CHECK(game::SceneName(key.value_or(core::SceneKey{})) == name);
    }
    const std::optional<core::SceneKey> unknown = game::SceneNamed("nowhere");
    CHECK_FALSE(unknown.has_value());
}

TEST_CASE("the list is F2's order and wraps") {
    REQUIRE(game::Scenes().size() == 5);
    CHECK(game::SceneName(game::ARENA_SCENE) == "arena");
    CHECK(game::SceneName(game::STRESS_SCENE) == "stress");
    CHECK(game::SceneName(game::PAINTED_SCENE) == "painted");
    CHECK(game::SceneName(game::CELLS_SCENE) == "cells");
    CHECK(game::SceneName(game::WORLD_SCENE) == "world");
    CHECK(game::NextScene(game::ARENA_SCENE) == game::STRESS_SCENE);
    CHECK(game::NextScene(game::STRESS_SCENE) == game::PAINTED_SCENE);
    CHECK(game::NextScene(game::PAINTED_SCENE) == game::CELLS_SCENE);
    CHECK(game::NextScene(game::CELLS_SCENE) == game::WORLD_SCENE);
    CHECK(game::NextScene(game::WORLD_SCENE) == game::ARENA_SCENE); // and round again
    CHECK(&game::SceneOf(game::PAINTED_SCENE) == game::Scenes()[2]);
}

TEST_CASE("every listed scene passes CheckScenes against the shipped manifest") {
    const std::expected<void, core::Error> shipped = game::CheckScenes(*test::TestManifest(), game::BuiltInData());
    CHECK(shipped.has_value());
    // Each listed scene is checked: a manifest without it is refused, naming it.
    for (const game::Scene* const scene : game::Scenes()) {
        const std::string_view name = scene->Traits().name;
        core::Manifest manifest = *test::TestManifest();
        std::erase_if(manifest.scenes, [name](const core::SceneInfo& info) { return info.name == name; });
        const std::expected<void, core::Error> checked = game::CheckScenes(manifest, game::BuiltInData());
        REQUIRE_FALSE(checked.has_value());
        CHECK(checked.error().message.contains(name));
    }
}

TEST_CASE("each scene's camera reaches the snapshot") {
    constexpr std::array<std::pair<core::SceneKey, core::CameraKind>, 5> CAMERAS{ {
        { game::ARENA_SCENE, core::CameraKind::Follow },
        { game::STRESS_SCENE, core::CameraKind::Follow },
        { game::PAINTED_SCENE, core::CameraKind::Pan },
        { game::CELLS_SCENE, core::CameraKind::Pan },
        { game::WORLD_SCENE, core::CameraKind::Roam },
    } };
    core::Jobs jobs{ 1 };
    for (const auto& [scene, camera] : CAMERAS) {
        game::Host host(test::TestManifest(), scene);
        core::Snapshot snapshot;
        host.Publish(snapshot, jobs, std::nullopt);
        CHECK(snapshot.camera == camera);
    }
}

// How many tiles `span` covers: none for an empty one, whose first column or row lies past its last.
u32 Area(const game::TileSpan& span) {
    return (span.lastColumn + 1 - span.firstColumn) * (span.lastRow + 1 - span.firstRow);
}

TEST_CASE("TilesIn finds the tiles a view overlaps, clamped to the map") {
    // Inside the tile in column 1, row 1 (256 to 512 on both axes).
    constexpr core::Rect INSIDE_ONE{ .min = { .x = 300.0F, .y = 300.0F }, .max = { .x = 400.0F, .y = 400.0F } };
    const game::TileSpan one = game::TilesIn(INSIDE_ONE);
    CHECK(one.firstColumn == 1);
    CHECK(one.lastColumn == 1);
    CHECK(one.firstRow == 1);
    CHECK(one.lastRow == 1);
    // Across the corner where columns and rows 1 and 2 meet, at 512.
    constexpr core::Rect STRADDLING{ .min = { .x = 500.0F, .y = 500.0F }, .max = { .x = 600.0F, .y = 600.0F } };
    const game::TileSpan four = game::TilesIn(STRADDLING);
    CHECK(four.firstColumn == 1);
    CHECK(four.lastColumn == 2);
    CHECK(four.firstRow == 1);
    CHECK(four.lastRow == 2);
    CHECK(Area(four) == 4);
    // Past the map's far corner: only its last columns and rows.
    constexpr core::Rect PAST_CORNER{ .min = { .x = 38'000.0F, .y = 21'300.0F }, .max = { .x = 40'000.0F, .y = 23'000.0F } };
    const game::TileSpan corner = game::TilesIn(PAST_CORNER);
    CHECK(corner.firstColumn == 148);
    CHECK(corner.lastColumn == 149);
    CHECK(corner.firstRow == 83);
    CHECK(corner.lastRow == 84);
    // The whole map: 150 by 85 tiles of 256, the last row reaching a little past the map's edge.
    const game::TileSpan whole = game::TilesIn(game::WORLD_MAP);
    CHECK(whole.firstColumn == 0);
    CHECK(whole.lastColumn == 149);
    CHECK(whole.firstRow == 0);
    CHECK(whole.lastRow == 84);
    CHECK(Area(whole) == 150 * 85);
    // A view that misses the map, before it or past its last row: no tiles at all, not the nearest edge's.
    constexpr core::Rect BEFORE{ .min = { .x = -2'000.0F, .y = 100.0F }, .max = { .x = -1'000.0F, .y = 200.0F } };
    constexpr core::Rect PAST{ .min = { .x = 100.0F, .y = 22'000.0F }, .max = { .x = 200.0F, .y = 23'000.0F } };
    CHECK(Area(game::TilesIn(BEFORE)) == 0);
    CHECK(Area(game::TilesIn(PAST)) == 0);
}

TEST_CASE("every terrain kind appears somewhere on the map, and chosen tiles keep their kind and variant") {
    std::array<bool, game::TERRAINS> kinds{};
    std::array<bool, core::atlas::SPRITE_NAMES.size()> sprites{};
    for (u32 row = 0; row < game::WORLD_ROWS; ++row) {
        for (u32 column = 0; column < game::WORLD_COLUMNS; ++column) {
            kinds.at(std::to_underlying(game::TerrainAt(column, row))) = true;
            sprites.at(std::to_underlying(game::TileSprite(column, row))) = true;
        }
    }
    CHECK(std::ranges::all_of(kinds, std::identity{}));
    // Both variants of every kind, and no other sprite.
    CHECK(std::ranges::count(sprites, true) == 2 * game::TERRAINS);
    // The first tile, the last, one in the middle and one near an edge, as the map has drawn them since it was made:
    // a change to the noise or the variants shows here (and in the world's reference pictures).
    struct Tile {
        u32 column = 0;
        u32 row = 0;
        game::Terrain terrain = game::Terrain::DeepWater;
        core::atlas::SpriteId sprite = core::atlas::SpriteId{};
    };
    constexpr std::array<Tile, 4> TILES{ {
        { .column = 0, .row = 0, .terrain = game::Terrain::Hills, .sprite = core::atlas::SpriteId::TerrainHills1 },
        { .column = 149, .row = 84, .terrain = game::Terrain::Meadow, .sprite = core::atlas::SpriteId::TerrainMeadow1 },
        { .column = 75, .row = 42, .terrain = game::Terrain::Forest, .sprite = core::atlas::SpriteId::TerrainForest0 },
        { .column = 13, .row = 71, .terrain = game::Terrain::Rock, .sprite = core::atlas::SpriteId::TerrainRock0 },
    } };
    for (const Tile& tile : TILES) {
        CAPTURE(tile.column);
        CAPTURE(tile.row);
        CHECK(game::TerrainAt(tile.column, tile.row) == tile.terrain);
        CHECK(game::TileSprite(tile.column, tile.row) == tile.sprite);
    }
}

} // namespace
