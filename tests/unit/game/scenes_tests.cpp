#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/jobs.hpp"
#include "core/journey.hpp"
#include "core/state.hpp"
#include "game/host.hpp"
#include "game/npcs.hpp"
#include "game/scenes.hpp"
#include "game/scenes/world.hpp"
#include "support/host.hpp"
#include "support/manifest.hpp"
#include "support/saves.hpp"
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

core::TickCounters At(u64 tick) {
    return { .wall = tick, .simulation = tick, .state = tick };
}

/// Ticks `host` once at `tick` with `input`, ignoring its events.
void Tick(game::Host& host, u64 tick, const core::TickInput& input = {}) {
    std::vector<core::Event> events;
    host.Tick(At(tick), input, events);
}

/// Presses F2 at `tick`, then enters the scene it asks for, as the logic does after the tick.
void CycleScene(game::Host& host, u64 tick) {
    const std::array<core::Action, 1> cycle{ { { .kind = core::ActionKind::CycleScene } } };
    Tick(host, tick, { .actions = cycle, .held = {} });
    host.EnterScene();
}

/// Presses F2 from `tick` on, a tick each, until `host` runs `scene`; returns the tick after the last press.
u64 CycleTo(game::Host& host, u64 tick, core::SceneKey scene) {
    CycleScene(host, tick++);
    while (host.Current().key != scene) {
        CycleScene(host, tick++);
    }
    return tick;
}

/// Lets a month pass for `host` at once, as M asks for it.
void StepMonth(game::Host& host, u64 tick) {
    core::Jobs jobs{ 1 };
    host.AdvanceCalendar(At(tick), jobs, {});
}

/// Where the player's cultivator stands in `host`.
core::Vec2 FootOf(const game::Host& host) {
    return test::BodyOf(host, host.Player()).current;
}

/// A hash of `host`'s journeys under way and travel journals.
u64 TravelHash(const game::Host& host) {
    core::Hasher hasher;
    host.NpcTravel().Save(hasher);
    return hasher.Value();
}

/// Each world-domain entity of `host`'s running world with its cell, in the domain's order.
std::vector<std::pair<core::StableId, u32>> Grid(const game::Host& host) {
    std::vector<std::pair<core::StableId, u32>> grid;
    const core::Registry& world = host.State()[game::WORLD].Entities();
    for (const auto [entity, person, stableId] : world.view<const game::Npc, const core::StableId>().each()) {
        grid.emplace_back(stableId, person.cell);
    }
    return grid;
}

/// How many entities the domain `domain` of `host`'s running world holds.
std::size_t CountIn(const game::Host& host, core::DomainIndex domain) {
    return host.State()[domain].Entities().view<const core::StableId>().size();
}

/// The highest stable ID in the world domain of `host`'s running world, or 0 when it is empty.
u64 HighestWorldId(const game::Host& host) {
    u64 highest = 0;
    for (const auto [entity, stableId] : host.State()[game::WORLD].Entities().view<const core::StableId>().each()) {
        highest = std::max(highest, std::to_underlying(stableId));
    }
    return highest;
}

TEST_CASE("leaving the world map and coming back keeps the world") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    const core::Vec2 east = map->StartCentre() + core::Vec2{ .x = 3.0F * map->TileSize(), .y = 0.0F };
    const std::array<core::Action, 1> walk{ { { .kind = core::ActionKind::WalkTo, .point = east } } };
    StepMonth(host, 1);
    Tick(host, 2, { .actions = walk, .held = {} });
    Tick(host, 3);
    REQUIRE(host.PlayerRoute().has_value());
    REQUIRE_FALSE(host.NpcTravel().Journeys().empty());
    const u64 world = host.WorldHash();
    const u64 travel = TravelHash(host);
    const u64 ids = host.State().Ids().next;
    const u64 clock = host.WorldTicks();

    CycleTo(host, 4, game::WORLD_SCENE); // F2 round every scene and back
    CHECK(host.WorldHash() == world);
    CHECK(TravelHash(host) == travel);
    CHECK(host.State().Ids().next == ids);
    CHECK(host.WorldTicks() == clock);
    CHECK(host.Months() == 1);
    CHECK_FALSE(host.PlayerRoute().has_value());
}

TEST_CASE("F2 brings the cultivator back where it stood") {
    constexpr core::HeldInput DOWN_RIGHT{ .move = { .x = 0.6F, .y = 0.8F } };
    game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    for (u64 tick = 1; tick <= 10; ++tick) {
        Tick(host, tick, { .actions = {}, .held = DOWN_RIGHT });
    }
    const core::Vec2 foot = FootOf(host);
    REQUIRE(foot.x != test::ShippedWorld()->StartCentre().x);

    CycleTo(host, 11, game::WORLD_SCENE);
    CHECK(FootOf(host).x == foot.x);
    CHECK(FootOf(host).y == foot.y);
    CHECK(test::BodyOf(host, host.Player()).previous.x == foot.x); // standing, not streaking in from elsewhere
}

TEST_CASE("a portal brings the cultivator back off the portal") {
    core::GameData data = test::ShippedData();
    game::Host host(test::TestManifest(), game::WORLD_SCENE, data);
    const std::optional<core::Vec2> portal = host.Portal();
    REQUIRE(portal.has_value());
    // Two steps of the cultivator's walk, each the whole distance at once: beside the portal, then onto it.
    const auto stepTo = [&](u64 tick, core::Vec2 target) {
        const core::Vec2 away = target - FootOf(host);
        const f32 distance = std::hypot(away.x, away.y);
        data.cultivator.walkSpeed = distance;
        host.SetData(data);
        Tick(host, tick, { .actions = {}, .held = { .move = away * (1.0F / distance) } });
    };
    const core::Vec2 beside = portal.value_or(core::Vec2{}) + core::Vec2{ .x = 3.0F * game::PORTAL_RADIUS, .y = 0.0F };
    stepTo(1, beside);
    REQUIRE_FALSE(host.Requested().has_value());
    stepTo(2, portal.value_or(core::Vec2{}));
    REQUIRE(host.Requested().has_value());
    host.EnterScene();
    REQUIRE(host.Current().key == game::ARENA_SCENE);

    const u64 next = CycleTo(host, 3, game::WORLD_SCENE);
    const core::Vec2 foot = FootOf(host);
    CHECK(foot.x == beside.x);
    CHECK(foot.y == beside.y);
    const core::Vec2 away = foot - portal.value_or(core::Vec2{});
    CHECK(std::hypot(away.x, away.y) > game::PORTAL_RADIUS);
    Tick(host, next);
    CHECK_FALSE(host.Requested().has_value()); // standing still there doesn't send it out again
}

TEST_CASE("a dev scene leaves the game world alone") {
    game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    StepMonth(host, 1);
    for (u64 tick = 2; tick <= 10; ++tick) {
        Tick(host, tick);
    }
    const u64 world = host.WorldHash();
    const u64 clock = host.WorldTicks();

    CycleScene(host, 11);
    REQUIRE(host.Current().key == game::ARENA_SCENE);
    const u64 setAside = host.WorldHash(); // the world, keeping where the cultivator left it
    CHECK(host.Months() == 0);             // the arena's own calendar, from its start
    StepMonth(host, 12);
    StepMonth(host, 13);
    CHECK(host.Months() == 2);
    CHECK(host.WorldTicks() == 2 * game::TICKS_PER_MONTH);
    CHECK(host.WorldHash() == setAside);

    CycleTo(host, 14, game::WORLD_SCENE);
    CHECK(host.WorldHash() == world);
    CHECK(host.Months() == 1);
    CHECK(host.WorldTicks() == clock);
}

TEST_CASE("every dev scene builds its crowd as before") {
    struct Crowd {
        core::SceneKey scene;
        std::size_t worldNpcs;
    };
    constexpr std::array CROWDS{
        Crowd{ .scene = game::ARENA_SCENE, .worldNpcs = 5'000 },
        Crowd{ .scene = game::STRESS_SCENE, .worldNpcs = 50'000 },
        Crowd{ .scene = game::PAINTED_SCENE, .worldNpcs = 0 },
        Crowd{ .scene = game::CELLS_SCENE, .worldNpcs = 0 },
    };
    game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    StepMonth(host, 1); // a game world under way, which none of them sees
    u64 tick = 2;
    for (const Crowd& crowd : CROWDS) {
        CAPTURE(game::SceneName(crowd.scene));
        tick = CycleTo(host, tick, crowd.scene);
        const game::Host fresh(test::TestManifest(), crowd.scene);
        CHECK(CountIn(host, game::WORLD) == crowd.worldNpcs);
        CHECK(Grid(host) == Grid(fresh));
        CHECK(CountIn(host, game::ACTIVE) == CountIn(fresh, game::ACTIVE));
        CHECK(host.State().Ids().next == fresh.State().Ids().next);
        CHECK(host.Months() == 0);
        CHECK(host.WorldTicks() == 0);
        CHECK(host.NpcTravel().Journeys().empty());
    }
}

TEST_CASE("the base world doesn't depend on the scenes run before") {
    const game::Host first(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    game::Host later(test::TestManifest(), game::ARENA_SCENE, test::ShippedData());
    CycleTo(later, 1, game::WORLD_SCENE); // through stress, painted and cells
    CHECK(later.WorldHash() == first.WorldHash());
    CHECK(later.State().Ids().next == first.State().Ids().next);
    CHECK(TravelHash(later) == TravelHash(first));
}

TEST_CASE("F2 through every scene twice keeps the world and never reuses a scene ID") {
    game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    u64 tick = 1;
    u64 sceneIds = host.State().SceneIds().next;
    bool rising = true;
    u64 highest = 0;
    for (u32 round = 0; round < 2; ++round) {
        CAPTURE(round);
        StepMonth(host, tick++); // a month step between the rounds, and one before the first
        const u64 world = host.WorldHash();
        const u64 travel = TravelHash(host);
        const u64 ids = host.State().Ids().next;
        for (bool back = false; !back; back = host.Current().key == game::WORLD_SCENE) {
            CycleScene(host, tick++);
            rising = rising && host.State().SceneIds().next > sceneIds;
            sceneIds = host.State().SceneIds().next;
            highest = std::max(highest, HighestWorldId(host));
        }
        CHECK(host.WorldHash() == world);
        CHECK(TravelHash(host) == travel);
        CHECK(host.State().Ids().next == ids);
    }
    CHECK(rising);
    CHECK(highest < core::SCENE_ID_BASE); // no scene's own entity ever stood in a world domain
}

TEST_CASE("a world NPC brought into the world scene goes back to the world under its ID when the scene is left") {
    constexpr f32 POWER = 7.0F;
    constexpr game::Needs NEEDS{ .rest = 0.25F, .company = 0.5F };
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    const u16 city = map->Start();
    const u32 tile = map->TileIndex(map->Places()[city].tile);
    core::State state{ game::DOMAINS, test::EMPTY_SAVE_SEED };
    const core::StableId npc = game::SpawnNpc(state, tile, city, POWER);
    const core::Entity spawned = state[game::WORLD].Find(npc);
    state[game::WORLD].Entities().replace<game::Needs>(spawned, NEEDS);
    state[game::WORLD].Entities().emplace<game::Met>(spawned);
    // Into the scene as an NPC the scene walks: its body on its tile.
    state.Move(npc, game::ACTIVE, [&](const core::Registry& from, core::Entity old, core::Registry& into, core::Entity moved) {
        into.emplace<game::Npc>(moved, from.get<game::Npc>(old));
        into.emplace<game::Needs>(moved, from.get<game::Needs>(old));
        into.emplace<game::Met>(moved);
        const core::Vec2 foot = map->TileCentre(tile);
        into.emplace<game::Body>(moved, foot, foot);
    });
    const std::vector<std::byte> bytes =
        test::ArenaSave(state, {}, std::nullopt, std::nullopt, std::nullopt, game::WORLD_SCENE);
    game::Host host(test::TestManifest(), game::ARENA_SCENE, test::ShippedData());
    core::Reader reader{ bytes };
    REQUIRE(host.Load(reader, 0));
    host.EnterScene();
    REQUIRE(host.State().DomainOf(npc) == game::ACTIVE);

    CycleTo(host, 1, game::WORLD_SCENE); // left, and back round to the world map
    REQUIRE(host.State().DomainOf(npc) == game::WORLD);
    const core::Registry& world = host.State()[game::WORLD].Entities();
    const core::Entity back = host.State()[game::WORLD].Find(npc);
    const game::Npc& person = world.get<game::Npc>(back);
    CHECK(person.cell == tile);
    CHECK(person.home == city);
    CHECK(person.power == POWER);
    CHECK(world.get<game::Needs>(back).rest == NEEDS.rest);
    CHECK(world.get<game::Needs>(back).company == NEEDS.company);
    CHECK(world.all_of<game::Met>(back));
    CHECK_FALSE(world.all_of<game::Body>(back));
}

/// Whether `point` lies in the step area of the portal at `portal`: the ellipse PORTAL_RADIUS across and
/// PORTAL_DEPTH_RATIO as deep that the host checks the cultivator's foot against.
bool InStepArea(core::Vec2 point, core::Vec2 portal) {
    const core::Vec2 away = point - portal;
    const f32 across = away.x / game::PORTAL_RADIUS;
    const f32 deep = away.y / (game::PORTAL_RADIUS * game::PORTAL_DEPTH_RATIO);
    return (across * across) + (deep * deep) <= 1.0F;
}

/// A host with the shipped data that has loaded and entered a save of the world scene whose one entity is the player's
/// cultivator standing at `foot`, the world clock at `clock` in the first month.
std::unique_ptr<game::Host> CultivatorAt(core::Vec2 foot, u64 clock) {
    core::State state{ game::DOMAINS, test::EMPTY_SAVE_SEED };
    const auto [player, entity] = state.Create(game::ACTIVE, core::IdRange::Scene);
    state[game::ACTIVE].Entities().emplace<game::Body>(entity, foot, foot);
    const std::vector<std::byte> bytes = test::ArenaSave(
        state,
        player,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        game::WORLD_SCENE,
        test::WORLD_VERSION,
        test::ACTIVE_VERSION,
        { .ticks = clock }
    );
    auto host = std::make_unique<game::Host>(test::TestManifest(), game::ARENA_SCENE, test::ShippedData());
    core::Reader reader{ bytes };
    REQUIRE(host->Load(reader, 0));
    host->EnterScene();
    return host;
}

TEST_CASE("a month step that lands the cultivator on the portal never brings it back onto it") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    const f32 tile = map->TileSize();
    const core::Vec2 start = map->StartCentre();
    const core::Vec2 portal = start + game::WORLD_PORTAL_OFFSET;
    // The walk between these two tiles' centres, diagonal, passes a few units from the portal's point.
    const core::Vec2 from = start + core::Vec2{ .x = -2.0F * tile, .y = -tile };
    const core::Vec2 onto = start + core::Vec2{ .x = -3.0F * tile, .y = 0.0F };
    const std::array<core::Action, 1> walk{ { { .kind = core::ActionKind::WalkTo, .point = onto } } };
    // How long after it sets out the walk first stands in the step area.
    i64 inside = -1;
    {
        const std::unique_ptr<game::Host> probe = CultivatorAt(from, 0);
        Tick(*probe, 1, { .actions = walk, .held = {} });
        REQUIRE(probe->PlayerRoute().has_value());
        const core::Journey route = probe->PlayerRoute().value_or(core::Journey{});
        for (i64 tick = 0; tick <= core::EndTick(route) && inside < 0; ++tick) {
            inside = InStepArea(core::Where(*map, route, tick), portal) ? tick : -1;
        }
    }
    REQUIRE(inside >= 2);

    // Set out so that M's jump to the next month's first tick lands the cultivator in the step area.
    const std::unique_ptr<game::Host> host = CultivatorAt(from, game::TICKS_PER_MONTH - static_cast<u64>(inside));
    Tick(*host, 1, { .actions = walk, .held = {} });
    REQUIRE_FALSE(host->Requested().has_value());
    StepMonth(*host, 2);
    REQUIRE(InStepArea(FootOf(*host), portal));
    Tick(*host, 3); // walking on from inside the step area takes it through
    REQUIRE(host->Requested().has_value());
    host->EnterScene();

    const u64 next = CycleTo(*host, 4, game::WORLD_SCENE);
    CHECK_FALSE(InStepArea(FootOf(*host), portal));
    Tick(*host, next);
    CHECK_FALSE(host->Requested().has_value());
}

} // namespace
