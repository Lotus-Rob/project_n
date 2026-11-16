#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/jobs.hpp"
#include "game/host.hpp"
#include "game/scenes.hpp"
#include "game/scenes/world.hpp"
#include "support/manifest.hpp"
#include "support/world.hpp"

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

TEST_CASE("the walkable scenes lead round arena, stress and world") {
    CHECK(game::NextWalkableScene(game::ARENA_SCENE) == game::STRESS_SCENE);
    CHECK(game::NextWalkableScene(game::STRESS_SCENE) == game::WORLD_SCENE);
    CHECK(game::NextWalkableScene(game::WORLD_SCENE) == game::ARENA_SCENE); // and round again, past painted and cells
    // The still scenes have no cultivator to walk, so no portal.
    CHECK_FALSE(game::SceneOf(game::PAINTED_SCENE).Traits().portal.has_value());
    CHECK_FALSE(game::SceneOf(game::CELLS_SCENE).Traits().portal.has_value());
}

TEST_CASE("every listed scene passes CheckScenes against the shipped manifest") {
    const std::expected<void, core::Error> shipped = game::CheckScenes(*test::TestManifest(), test::ShippedData());
    CHECK(shipped.has_value());
    // Each listed scene is checked: a manifest without it is refused, naming it.
    for (const game::Scene* const scene : game::Scenes()) {
        const std::string_view name = scene->Traits().name;
        core::Manifest manifest = *test::TestManifest();
        std::erase_if(manifest.scenes, [name](const core::SceneInfo& info) { return info.name == name; });
        const std::expected<void, core::Error> checked = game::CheckScenes(manifest, test::ShippedData());
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
        game::Host host(test::TestManifest(), scene, test::ShippedData());
        core::Snapshot snapshot;
        host.Publish(snapshot, jobs, std::nullopt);
        CHECK(snapshot.camera == camera);
    }
}

TEST_CASE("only the world scene shows the map") {
    for (const game::Scene* const scene : game::Scenes()) {
        CAPTURE(scene->Traits().name);
        CHECK(scene->Traits().worldMap == (scene->Traits().name == "world"));
    }
    // Its publish holds none of the map: the renderer draws it from the map and the look, with no painted ground under.
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    core::Jobs jobs;
    game::Host host(manifest, game::WORLD_SCENE, test::ShippedData());
    core::Snapshot snapshot;
    host.Publish(snapshot, jobs, std::nullopt);
    CHECK(snapshot.worldMap);
    const core::Instance ground = core::MakeInstance(*manifest, { .sprite = core::atlas::SpriteId::Ground });
    const core::Instance tile = core::MakeInstance(*manifest, { .sprite = core::atlas::SpriteId::TerrainMeadow0 });
    CHECK(std::ranges::none_of(snapshot.instances, [&](const core::Instance& instance) {
        return instance.regionMin == ground.regionMin || instance.regionMin == tile.regionMin;
    }));
    core::Snapshot arena;
    game::Host arenaHost(manifest, game::ARENA_SCENE);
    arenaHost.Publish(arena, jobs, std::nullopt);
    CHECK_FALSE(arena.worldMap);
}

TEST_CASE("the world scene needs the world map") {
    const std::expected<void, core::Error> refused = game::CheckScenes(*test::TestManifest(), game::BuiltInData());
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.contains("scene world"));
}

TEST_CASE("the world's view is the map's size") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    game::Host world(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    const core::Rect view = world.View();
    CHECK(view.min.x == 0.0F);
    CHECK(view.min.y == 0.0F);
    CHECK(view.max.x == static_cast<f32>(map->Columns()) * map->TileSize());
    CHECK(view.max.y == static_cast<f32>(map->Rows()) * map->TileSize());
    // A scene without a map keeps the view its traits name.
    game::Host painted(test::TestManifest(), game::PAINTED_SCENE);
    const core::Rect still = painted.View();
    const core::Rect named = game::SceneOf(game::PAINTED_SCENE).Traits().view;
    CHECK(still.min.x == named.min.x);
    CHECK(still.min.y == named.min.y);
    CHECK(still.max.x == named.max.x);
    CHECK(still.max.y == named.max.y);
}

constexpr f32 PORTAL_LEFT = -560.0F;
constexpr f32 PORTAL_UP = -200.0F;

TEST_CASE("the cultivator starts on the start city, the portal a short walk from it") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    const core::Domain& active = host.State()[game::ACTIVE];
    const core::Vec2 foot = active.Entities().get<game::Body>(active.Find(host.Player())).current;
    const core::Vec2 start = map->StartCentre();
    CHECK(foot.x == start.x);
    CHECK(foot.y == start.y);
    const std::optional<core::Vec2> portal = host.Portal();
    REQUIRE(portal.has_value());
    const core::Vec2 point = portal.value_or(core::Vec2{});
    CHECK(point.x == start.x + game::WORLD_PORTAL_OFFSET.x);
    CHECK(point.y == start.y + game::WORLD_PORTAL_OFFSET.y);
    CHECK(game::WORLD_PORTAL_OFFSET.x == PORTAL_LEFT); // as before the world map: left and up from the start
    CHECK(game::WORLD_PORTAL_OFFSET.y == PORTAL_UP);
}

TEST_CASE("every place marker sprite reaches as far below the tile's centre as the city marker") {
    constexpr auto MARKERS = std::to_array(
        { core::atlas::SpriteId::MarkerHauntedForest,
          core::atlas::SpriteId::MarkerMountainPass,
          core::atlas::SpriteId::MarkerShrine,
          core::atlas::SpriteId::MarkerCave,
          core::atlas::SpriteId::MarkerCliff,
          core::atlas::SpriteId::MarkerCorruptVillage }
    );
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::SpriteInfo& city = manifest->Sprite(core::atlas::SpriteId::MarkerCity);
    for (const core::atlas::SpriteId sprite : MARKERS) {
        const core::SpriteInfo& marker = manifest->Sprite(sprite);
        INFO("sprite ", std::to_underlying(sprite));
        CHECK(marker.height - marker.footY == city.height - city.footY);
    }
}

// The gap between the city marker's lower edge and the name hanging from it, in world units.
constexpr f32 PLACE_NAME_GAP = 8.0F;

TEST_CASE("each place in view publishes its name, in its own kind, and none outside the view") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    core::Jobs jobs{ 1 };
    const core::Vec2 start = map->StartCentre();
    const core::Rect view{ .min = start - core::Vec2{ .x = 960.0F, .y = 540.0F },
                           .max = start + core::Vec2{ .x = 960.0F, .y = 540.0F } };
    core::Snapshot snapshot;
    host.Publish(snapshot, jobs, view);
    std::vector<core::TextItem> places;
    for (const core::TextItem& text : snapshot.texts) {
        if (text.kind == core::TextKind::Place) {
            places.push_back(text);
        }
    }
    // Only the capital lies in a screen round itself.
    REQUIRE(places.size() == 1);
    const core::Place& capital = map->Places()[map->Start()];
    CHECK(places.front().string == capital.name);
    CHECK(places.front().priority == core::LabelPriority::Place);
    CHECK(places.front().nameKey == std::to_underlying(capital.name));
    CHECK(places.front().entity != core::StableId{});
    CHECK(places.front().current.x == start.x);
    // The name hangs under the city marker, which is drawn round the tile's centre where the cultivator stands: clear
    // of the cultivator's label, which rides over its head.
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest(); // held: marker points into it
    const core::SpriteInfo& marker = manifest->Sprite(core::atlas::SpriteId::MarkerCity);
    CHECK(places.front().align == core::TextAlign::TopCentre);
    CHECK(places.front().current.y == start.y + static_cast<f32>(marker.height - marker.footY) + PLACE_NAME_GAP);
    CHECK(places.front().current.y > start.y); // whatever the offset, the name hangs below the tile's centre
    // The whole map in view: every place, each with an entity and a key of its own.
    core::Snapshot wide;
    host.Publish(wide, jobs, host.View());
    std::vector<u32> keys;
    std::vector<core::StableId> entities;
    for (const core::TextItem& text : wide.texts) {
        if (text.kind == core::TextKind::Place) {
            keys.push_back(text.nameKey);
            entities.push_back(text.entity);
        }
    }
    CHECK(keys.size() == map->Places().size());
    std::ranges::sort(keys);
    std::ranges::sort(entities);
    CHECK(std::ranges::adjacent_find(keys) == keys.end());
    CHECK(std::ranges::adjacent_find(entities) == entities.end());
    // None of them is a creature's: the creatures' IDs count up from 1, as far as the NPCs go.
    CHECK(std::to_underlying(entities.front()) > 1'000'000);
}

TEST_CASE("the world scene's NPCs live in their cities and regions") {
    constexpr u32 PEOPLE = 50'000;
    constexpr u32 PERCENT_ON_CITY_TILE = 70;
    constexpr u32 PERCENT_TOLERANCE = 2;
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    game::Host const host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    const core::Registry& entities = host.State()[game::WORLD].Entities();
    std::vector<u32> perCity(map->Places().size(), 0);
    u32 onCityTile = 0;
    u32 total = 0;
    bool homed = true;
    for (const auto [entity, person] : entities.view<const game::Npc>().each()) {
        ++total;
        const bool isCity = person.home < map->Places().size() && map->Places()[person.home].kind == core::PlaceKind::City;
        homed = homed && isCity;
        if (!isCity) {
            continue;
        }
        ++perCity[person.home];
        const u32 cityTile = map->TileIndex(map->Places()[person.home].tile);
        const std::span<const u32> region = map->RegionTiles(person.home);
        onCityTile += person.cell == cityTile ? 1U : 0U;
        homed = homed && (person.cell == cityTile || std::ranges::binary_search(region, person.cell));
    }
    CHECK(homed);
    CHECK(total == PEOPLE);
    CHECK(onCityTile >= PEOPLE * (PERCENT_ON_CITY_TILE - PERCENT_TOLERANCE) / 100);
    CHECK(onCityTile <= PEOPLE * (PERCENT_ON_CITY_TILE + PERCENT_TOLERANCE) / 100);
    // Each city has exactly its share.
    bool shares = true;
    for (std::size_t place = 0; place < map->Places().size(); ++place) {
        const core::Place& city = map->Places()[place];
        shares = shares && perCity[place] == (city.kind == core::PlaceKind::City ? city.people : 0U);
    }
    CHECK(shares);
}

TEST_CASE("a month passes on the world map, the same twice, over the world NPCs") {
    core::Jobs jobs{ 1 };
    game::Host first(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    game::Host second(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    const auto fresh = [](const game::Host& host) {
        // Every NPC spawns resting, and a month's action replaces Rest.
        u32 untouched = 0;
        for (const auto [entity, person] : host.State()[game::WORLD].Entities().view<const game::Npc>().each()) {
            untouched += person.action == game::Action::Rest ? 1U : 0U;
        }
        return untouched;
    };
    const u32 before = fresh(first);
    first.AdvanceCalendar({ .wall = 1, .simulation = 1, .state = 1 }, jobs, {});
    second.AdvanceCalendar({ .wall = 1, .simulation = 1, .state = 1 }, jobs, {});
    CHECK(first.Months() == 1);
    CHECK(fresh(first) < before); // NPCs acted: some cultivated or socialised
    CHECK(first.StateHash() == second.StateHash());
}

} // namespace
