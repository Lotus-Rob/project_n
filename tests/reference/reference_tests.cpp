// Renders fixed views offscreen and compares them with the committed pictures in tests/reference/
// (design doc 0002 §8.9). Run through `project_n reference`, which picks Mesa's software Vulkan and
// the working directory: each view's picture lands in build/reference/, where `--update` takes it from.
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/math.hpp"
#include "game/arena.hpp"
#include "gfx/camera.hpp"
#include "gfx/image.hpp"
#include "gfx/renderer.hpp"
#include "platform/paths.hpp"
#include "platform/window.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 WIDTH = 1280;
constexpr u32 HEIGHT = 720;
// Room for another CPU's rounding, not for a visible change: a missing orb must fail. Runs at other
// vector widths and thread counts, and from GCC and Clang, came out identical to the byte.
constexpr u8 TOLERANCE = 2;
constexpr std::size_t ALLOWED_PIXELS = std::size_t{ WIDTH } * HEIGHT / 10'000;

constexpr u64 TWO_SECONDS = 120; // of ticks: the foxes and orbs have moved off their starting places
constexpr f64 HALFWAY = 0.5;

struct View {
    std::string_view name;
    game::Scene scene = game::Scene::Arena;
    u64 ticks = 0;
    f64 alpha = 1.0;
    f32 zoom = 1.0F;
};

void CheckAgainstReference(const View& view) {
    core::math::SetFloatingPointEnvironment();
    core::Jobs jobs;
    game::Arena arena{ view.scene };
    for (u64 tick = 1; tick <= view.ticks; ++tick) {
        arena.Tick({ .wall = tick, .simulation = tick, .state = tick }, {});
    }
    core::Snapshot snapshot;
    arena.Publish(snapshot, jobs);

    std::expected<platform::Window, core::Error> window = platform::Window::Open("reference", WIDTH, HEIGHT);
    REQUIRE_MESSAGE(window.has_value(), window.error().message);
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    REQUIRE_MESSAGE(assets.has_value(), assets.error().message);
    std::expected<gfx::Renderer, core::Error> renderer = gfx::Renderer::Create(*window, *assets);
    REQUIRE_MESSAGE(renderer.has_value(), renderer.error().message);
    const std::expected<gfx::Image, core::Error> drawn =
        renderer->Capture(snapshot, view.alpha, gfx::Camera{ .zoom = view.zoom }, WIDTH, HEIGHT);
    REQUIRE_MESSAGE(drawn.has_value(), drawn.error().message);

    const std::string file = std::format("{}.png", view.name);
    const std::expected<void, core::Error> saved = gfx::SavePng(*drawn, std::filesystem::path{ "build/reference" } / file);
    REQUIRE_MESSAGE(saved.has_value(), saved.error().message);
    const std::expected<gfx::Image, core::Error> reference = gfx::LoadPng(std::filesystem::path{ "tests/reference" } / file);
    REQUIRE_MESSAGE(reference.has_value(), reference.error().message);
    const std::size_t differing = gfx::DifferingPixels(*drawn, *reference, TOLERANCE);
    INFO("compare build/reference/", file, " with tests/reference/", file);
    CHECK(differing <= ALLOWED_PIXELS);
}

TEST_CASE("the painted scene: sprites, shadows and overlapping glows") {
    CheckAgainstReference({ .name = "painted", .scene = game::Scene::Painted });
}

TEST_CASE("the map cells at zoom 1") {
    CheckAgainstReference({ .name = "cells", .scene = game::Scene::Cells });
}

TEST_CASE("the map cells at the smallest zoom: the low mips") {
    CheckAgainstReference({ .name = "cells_far", .scene = game::Scene::Cells, .zoom = gfx::MIN_ZOOM });
}

TEST_CASE("the arena halfway between two ticks: interpolation and foot-y order") {
    CheckAgainstReference({ .name = "arena_halfway", .scene = game::Scene::Arena, .ticks = TWO_SECONDS, .alpha = HALFWAY });
}

} // namespace
