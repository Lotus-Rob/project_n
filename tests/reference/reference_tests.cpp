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
#include "support/manifest.hpp"

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
    bool outlines = false; // centred on the player's cultivator, hovered, with the first fox selected
    bool refit = false;    // a smaller picture first, so a scene target that isn't refitted shows
};

void CheckAgainstReference(const View& view) {
    core::math::SetFloatingPointEnvironment();
    core::Jobs jobs;
    game::Arena arena(test::TestManifest(), view.scene);
    for (u64 tick = 1; tick <= view.ticks; ++tick) {
        arena.Tick({ .wall = tick, .simulation = tick, .state = tick }, {});
    }
    if (view.outlines) {
        // The fox spawned first: the lowest ID among them.
        core::StableId fox{ std::numeric_limits<u64>::max() };
        const core::Registry& entities = arena.State()[game::ACTIVE].Entities();
        for (auto [entity, creature, stableId] : entities.view<const game::Creature, const core::StableId>().each()) {
            if (creature.kind == game::CreatureKind::Fox && std::to_underlying(stableId) < std::to_underlying(fox)) {
                fox = stableId;
            }
        }
        const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = fox } } };
        arena.Tick(
            { .wall = view.ticks + 1, .simulation = view.ticks + 1, .state = view.ticks + 1 }, { .actions = select, .held = {} }
        );
        REQUIRE(arena.Selected() == fox);
    }
    core::Snapshot snapshot;
    arena.Publish(snapshot, jobs);
    snapshot.scene = arena.Scene(); // as core::Logic names it

    std::expected<platform::Window, core::Error> window = platform::Window::Open("reference", WIDTH, HEIGHT);
    REQUIRE_MESSAGE(window.has_value(), window.error().message);
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    REQUIRE_MESSAGE(assets.has_value(), assets.error().message);
    std::expected<gfx::Renderer, core::Error> renderer = gfx::Renderer::Create(*window, *assets, test::TestManifest());
    REQUIRE_MESSAGE(renderer.has_value(), renderer.error().message);
    renderer->BeginScene(snapshot.scene, jobs);
    std::expected<bool, core::Error> held = false;
    while (held && !*held) {
        held = renderer->ContinueScene(std::numeric_limits<u64>::max());
    }
    REQUIRE_MESSAGE(held.has_value(), held.error().message);
    gfx::View shown{ .alpha = view.alpha, .camera = { .zoom = view.zoom } };
    if (view.outlines) {
        const auto player = std::ranges::find(snapshot.ids, arena.Player());
        REQUIRE(player != snapshot.ids.end());
        shown.outlined = static_cast<std::size_t>(player - snapshot.ids.begin());
        shown.camera.centre = snapshot.follow.value_or(core::Follow{}).current;
    }
    if (view.refit) {
        const std::expected<gfx::Image, core::Error> smaller = renderer->Capture(snapshot, shown, WIDTH / 2, HEIGHT / 3);
        REQUIRE_MESSAGE(smaller.has_value(), smaller.error().message);
    }
    const std::expected<gfx::Image, core::Error> drawn = renderer->Capture(snapshot, shown, WIDTH, HEIGHT);
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

TEST_CASE("the painted scene: sprites, shadows and overlapping glows, after a smaller picture") {
    CheckAgainstReference({ .name = "painted", .scene = game::Scene::Painted, .refit = true });
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

TEST_CASE("the hovered entity's pale outline and the selected one's gold, close up") {
    CheckAgainstReference({ .name = "outlines", .scene = game::Scene::Arena, .zoom = gfx::MAX_ZOOM, .outlines = true });
}

} // namespace
