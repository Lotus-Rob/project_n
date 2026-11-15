#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/jobs.hpp"
#include "game/host.hpp"
#include "game/scenes.hpp"
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

TEST_CASE("the walkable scenes lead round arena, stress and world") {
    CHECK(game::NextWalkableScene(game::ARENA_SCENE) == game::STRESS_SCENE);
    CHECK(game::NextWalkableScene(game::STRESS_SCENE) == game::WORLD_SCENE);
    CHECK(game::NextWalkableScene(game::WORLD_SCENE) == game::ARENA_SCENE); // and round again, past painted and cells
    // The still scenes have no cultivator to walk, so no portal.
    CHECK_FALSE(game::SceneOf(game::PAINTED_SCENE).Traits().portal.has_value());
    CHECK_FALSE(game::SceneOf(game::CELLS_SCENE).Traits().portal.has_value());
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

TEST_CASE("only the world scene shows the map") {
    for (const game::Scene* const scene : game::Scenes()) {
        CAPTURE(scene->Traits().name);
        CHECK(scene->Traits().worldMap == (scene->Traits().name == "world"));
    }
    // Its publish holds none of the map: the renderer draws it from the map and the look, with no painted ground under.
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    core::Jobs jobs;
    game::Host host(manifest, game::WORLD_SCENE);
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

} // namespace
