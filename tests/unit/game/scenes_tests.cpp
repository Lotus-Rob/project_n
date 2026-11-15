#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/jobs.hpp"
#include "core/view_limits.hpp"
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

// The tile centre of the shipped map's start city.
core::Vec2 StartOf(const core::WorldMap& map) {
    return map.TileCentre(map.TileIndex(map.Places()[map.Start()].tile));
}

// The tile `point` lies on.
u32 TileAt(const core::WorldMap& map, core::Vec2 point) {
    return map.TileIndex(
        { .column = static_cast<u32>(point.x / map.TileSize()), .row = static_cast<u32>(point.y / map.TileSize()) }
    );
}

constexpr f32 PORTAL_LEFT = -560.0F;
constexpr f32 PORTAL_UP = -200.0F;

TEST_CASE("the cultivator starts on the start city, the portal a short walk from it") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    const core::Domain& active = host.State()[game::ACTIVE];
    const core::Vec2 foot = active.Entities().get<game::Body>(active.Find(host.Player())).current;
    const core::Vec2 start = StartOf(*map);
    CHECK(foot.x == start.x);
    CHECK(foot.y == start.y);
    const std::optional<core::Vec2> portal = host.Portal();
    REQUIRE(portal.has_value());
    const core::Vec2 point = portal.value_or(core::Vec2{});
    CHECK(point.x == start.x + game::WORLD_PORTAL_OFFSET.x);
    CHECK(point.y == start.y + game::WORLD_PORTAL_OFFSET.y);
    CHECK(game::WORLD_PORTAL_OFFSET.x == PORTAL_LEFT); // the offset the 5a portal had, left and up from the start
    CHECK(game::WORLD_PORTAL_OFFSET.y == PORTAL_UP);
}

// How far above its tile's centre a place's name is anchored.
constexpr f32 PLACE_NAME_RISE = 60.0F;

TEST_CASE("each place in view publishes its name, in its own kind, and none outside the view") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    core::Jobs jobs{ 1 };
    const core::Vec2 start = StartOf(*map);
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
    CHECK(places.front().current.y == start.y - PLACE_NAME_RISE);
    // The whole map in view: every place, each with an entity and a key of its own.
    core::Snapshot wide;
    host.Publish(wide, jobs, game::WORLD_MAP);
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
    // None of them is a creature's: the creatures' IDs count up from 1, as far as the walkers go.
    CHECK(std::to_underlying(entities.front()) > 1'000'000);
}

TEST_CASE("the stand-in gatherings sit on their cities, clear of the start and apart") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    const std::vector<game::Gathering> gatherings = game::WorldGatherings(*map);
    REQUIRE(gatherings.size() == 4);
    const core::Vec2 start = StartOf(*map);
    const core::Rect widest{ .min = start - core::WIDEST_HALF_VIEW, .max = start + core::WIDEST_HALF_VIEW };
    const auto apart = [](const core::Rect& one, const core::Rect& other) {
        return one.max.x < other.min.x || one.min.x > other.max.x || one.max.y < other.min.y || one.min.y > other.max.y;
    };
    for (std::size_t i = 0; i < gatherings.size(); ++i) {
        const game::Gathering& gathering = gatherings[i];
        const core::Rect reach = game::GatheringReach(gathering);
        CAPTURE(i);
        // On a city's tile, never the start's; on the map; clear of the start; apart from the others.
        const u32 tile = TileAt(*map, gathering.point);
        const u16 place = map->PlaceAt(tile);
        REQUIRE(place != core::NO_PLACE);
        CHECK(map->Places()[place].kind == core::PlaceKind::City);
        CHECK(place != map->Start());
        CHECK(reach.min.x >= game::WORLD_MAP.min.x);
        CHECK(reach.min.y >= game::WORLD_MAP.min.y);
        CHECK(reach.max.x <= game::WORLD_MAP.max.x);
        CHECK(reach.max.y <= game::WORLD_MAP.max.y);
        CHECK(apart(reach, widest));
        for (std::size_t other = i + 1; other < gatherings.size(); ++other) {
            CHECK(apart(reach, game::GatheringReach(gatherings[other])));
        }
    }
    // The busiest is the port's, the towns the next three cities in place order: sect, oasis, marsh town.
    const auto key = [&](const game::Gathering& gathering) {
        const u32 tile = TileAt(*map, gathering.point);
        return map->Places()[map->PlaceAt(tile)].key;
    };
    CHECK(key(gatherings[0]) == "port");
    CHECK(key(gatherings[1]) == "sect");
    CHECK(key(gatherings[2]) == "oasis");
    CHECK(key(gatherings[3]) == "marsh_town");
}

// The tile of the `walker`th walker's first draw is impassable: its centre had to be redrawn.
bool FirstDrawIsSea(const core::WorldMap& map, u64 seed, u32 walker) {
    return map.CostAt(TileAt(map, game::SpreadDraw(seed, walker, 0))) == core::IMPASSABLE_COST;
}

constexpr u64 WALKER_SEED = 1;

TEST_CASE("no spread walker stands on an impassable tile, those whose first draw is sea redrawn on to land") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    struct Sample {
        std::string_view why;
        u32 walker;
        bool seaFirst; // its first draw is known to land on an impassable tile
    };
    constexpr std::array<Sample, 8> SAMPLES{ {
        { .why = "the first", .walker = 0, .seaFirst = false },
        { .why = "the end of the first row", .walker = 49, .seaFirst = false },
        { .why = "the start of the second row", .walker = 50, .seaFirst = false },
        { .why = "the last", .walker = 1'599, .seaFirst = false },
        { .why = "a middle one", .walker = 800, .seaFirst = false },
        { .why = "first draw in the sea (4)", .walker = 4, .seaFirst = true },
        { .why = "first draw in the sea (54)", .walker = 54, .seaFirst = true },
        { .why = "first draw in the sea (63)", .walker = 63, .seaFirst = true },
    } };
    u32 placed = 0;
    for (const Sample& sample : SAMPLES) {
        CAPTURE(sample.why);
        if (sample.seaFirst) {
            REQUIRE(FirstDrawIsSea(*map, WALKER_SEED, sample.walker));
        }
        const std::optional<core::Vec2> centre = game::SpreadCentre(*map, WALKER_SEED, sample.walker);
        if (sample.seaFirst) {
            REQUIRE(centre.has_value()); // the redraw found land
        }
        if (centre) {
            ++placed;
            CHECK(map->CostAt(TileAt(*map, centre.value_or(core::Vec2{}))) != core::IMPASSABLE_COST);
        }
    }
    CHECK(placed >= 3); // the sea doesn't take the sampled walkers
}

TEST_CASE("no gathered walker stands on an impassable tile, the port's crowd on its sea side included") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    const std::vector<game::Gathering> gatherings = game::WorldGatherings(*map);
    REQUIRE(gatherings.size() == 4);
    struct Sample {
        std::string_view why;
        std::size_t gathering;
        u32 member;
    };
    // The port's grid is 60 by 52 cells, the towns' 25 by 15; the port's members' walkers follow the spread crowd's.
    constexpr std::array<Sample, 12> SAMPLES{ {
        { .why = "the port's first cell", .gathering = 0, .member = 0 },
        { .why = "the end of the port's first row", .gathering = 0, .member = 59 },
        { .why = "the port's west side, mid-height", .gathering = 0, .member = 1'440 },
        { .why = "the port's last cell", .gathering = 0, .member = 3'119 },
        { .why = "the sea east of the port (cell wholly in one sea tile)", .gathering = 0, .member = 1'415 },
        { .why = "further east in that sea", .gathering = 0, .member = 1'423 },
        { .why = "the sect's first", .gathering = 1, .member = 0 },
        { .why = "the sect's last", .gathering = 1, .member = 374 },
        { .why = "the oasis's first (its cell wholly in one sea tile)", .gathering = 2, .member = 0 },
        { .why = "the oasis's last", .gathering = 2, .member = 374 },
        { .why = "the marsh town's first", .gathering = 3, .member = 0 },
        { .why = "the marsh town's last", .gathering = 3, .member = 374 },
    } };
    u32 placed = 0;
    for (const Sample& sample : SAMPLES) {
        CAPTURE(sample.why);
        const game::Gathering& gathering = gatherings.at(sample.gathering);
        u32 before = game::WORLD_SPREAD_WALKERS;
        for (std::size_t earlier = 0; earlier < sample.gathering; ++earlier) {
            before += gatherings.at(earlier).columns * gatherings.at(earlier).rows;
        }
        // A cell lying wholly within one impassable tile can only be placed by a redraw.
        const f32 width = 2.0F * gathering.half.x / static_cast<f32>(gathering.columns);
        const f32 height = 2.0F * gathering.half.y / static_cast<f32>(gathering.rows);
        const u32 column = sample.member % gathering.columns;
        const u32 row = sample.member / gathering.columns;
        const core::Vec2 low{
            .x = gathering.point.x - gathering.half.x + (width * static_cast<f32>(column)),
            .y = gathering.point.y - gathering.half.y + (height * static_cast<f32>(row)),
        };
        const u32 tile = TileAt(*map, low);
        const bool cellInOneSeaTile =
            tile == TileAt(*map, low + core::Vec2{ .x = width, .y = height }) && map->CostAt(tile) == core::IMPASSABLE_COST;
        const bool seaCell = sample.member == 1'415 || sample.member == 1'423 || (sample.gathering == 2 && sample.member == 0);
        if (seaCell) {
            REQUIRE(cellInOneSeaTile);
        }
        const std::optional<core::Vec2> centre =
            game::GatheredCentre(*map, WALKER_SEED, before + sample.member, sample.member, gathering);
        if (seaCell) {
            REQUIRE(centre.has_value());
        }
        if (centre) {
            ++placed;
            CHECK(map->CostAt(TileAt(*map, centre.value_or(core::Vec2{}))) != core::IMPASSABLE_COST);
        }
    }
    CHECK(placed >= 6);
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
        u32 untouched = 0;
        for (const auto [entity, person] : host.State()[game::WORLD].Entities().view<const game::Npc>().each()) {
            untouched += person.action == game::Action::Rest && person.power == 0.0F ? 1U : 0U;
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
