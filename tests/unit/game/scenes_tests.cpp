#include "core/cxx23.hpp" // IWYU pragma: keep
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
    REQUIRE(game::Scenes().size() == 4);
    CHECK(game::SceneName(game::ARENA_SCENE) == "arena");
    CHECK(game::SceneName(game::STRESS_SCENE) == "stress");
    CHECK(game::SceneName(game::PAINTED_SCENE) == "painted");
    CHECK(game::SceneName(game::CELLS_SCENE) == "cells");
    CHECK(game::NextScene(game::ARENA_SCENE) == game::STRESS_SCENE);
    CHECK(game::NextScene(game::STRESS_SCENE) == game::PAINTED_SCENE);
    CHECK(game::NextScene(game::PAINTED_SCENE) == game::CELLS_SCENE);
    CHECK(game::NextScene(game::CELLS_SCENE) == game::ARENA_SCENE); // and round again
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
    constexpr std::array<std::pair<core::SceneKey, core::CameraKind>, 4> CAMERAS{ {
        { game::ARENA_SCENE, core::CameraKind::Follow },
        { game::STRESS_SCENE, core::CameraKind::Follow },
        { game::PAINTED_SCENE, core::CameraKind::Pan },
        { game::CELLS_SCENE, core::CameraKind::Pan },
    } };
    core::Jobs jobs{ 1 };
    for (const auto& [scene, camera] : CAMERAS) {
        game::Host host(test::TestManifest(), scene);
        core::Snapshot snapshot;
        host.Publish(snapshot, jobs, std::nullopt);
        CHECK(snapshot.camera == camera);
    }
}

} // namespace
