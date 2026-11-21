#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/manifest.hpp"
#include "core/world_look.hpp"
#include "gfx/route_draw.hpp"
#include "support/manifest.hpp"

#include <doctest/doctest.h>

namespace {

using core::atlas::SpriteId;

constexpr f32 SPACING = 48.0F;

// The L-shaped route: east for 100 from START, then south for 50.
constexpr core::Vec2 START{ .x = 10.0F, .y = 20.0F };
constexpr core::Vec2 CORNER{ .x = 110.0F, .y = 20.0F };
constexpr core::Vec2 L_END{ .x = 110.0F, .y = 70.0F };
// Dots fall at 0, 48, 96 and 144 along it.
constexpr core::Vec2 SECOND_DOT{ .x = 58.0F, .y = 20.0F };
constexpr core::Vec2 THIRD_DOT{ .x = 106.0F, .y = 20.0F };
constexpr core::Vec2 FOURTH_DOT{ .x = 110.0F, .y = 64.0F };
constexpr std::size_t L_DOTS = 4;

// A shorter one, 30 east then 40 south: 70 long, so dots at 0 and 48.
constexpr core::Vec2 SHORT_CORNER{ .x = 30.0F, .y = 0.0F };
constexpr core::Vec2 SHORT_END{ .x = 30.0F, .y = 40.0F };
constexpr std::size_t SHORT_DOTS = 2;

constexpr core::Vec2 CURSOR_TILE{ .x = 3.5F * 256.0F, .y = 2.5F * 256.0F }; // the middle of the tile at column 3, row 2
constexpr core::Vec2 ONE_STEP_END{ .x = 40.0F, .y = 0.0F };                 // under one spacing: only the first dot
constexpr std::size_t APPENDED = 2;                                         // one dot and the end marker
constexpr std::size_t ALREADY_THERE = 7; // instances and their batch the route's draw is appended to

core::RouteLook Look() {
    return { .dot = SpriteId::RouteDot, .spacing = SPACING, .marker = SpriteId::RouteMarker, .cursor = SpriteId::TargetTile };
}

core::Instance Placed(const core::Manifest& manifest, SpriteId sprite, core::Vec2 foot) {
    return core::MakeInstance(manifest, { .sprite = sprite, .previous = foot, .current = foot });
}

bool Is(const core::Manifest& manifest, const core::Instance& instance, SpriteId sprite, core::Vec2 foot) {
    const core::Instance expected = Placed(manifest, sprite, foot);
    return std::ranges::equal(std::as_bytes(std::span{ &instance, 1 }), std::as_bytes(std::span{ &expected, 1 }));
}

gfx::TerrainDrawing Drawn(std::span<const core::Vec2> route, const core::Manifest& manifest) {
    gfx::TerrainDrawing drawing;
    gfx::DrawRoute(route, Look(), manifest, drawing);
    return drawing;
}

gfx::TerrainDrawing Targeted(std::optional<core::Vec2> cursor, const core::Manifest& manifest) {
    gfx::TerrainDrawing drawing;
    gfx::DrawTargetTile(cursor, Look(), manifest, drawing);
    return drawing;
}

} // namespace

TEST_CASE("route dots fall every spacing along the line") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const std::vector<core::Vec2> route{ START, CORNER, L_END };
    const gfx::TerrainDrawing drawing = Drawn(route, *manifest);
    REQUIRE(drawing.instances.size() == L_DOTS + 1);
    CHECK(Is(*manifest, drawing.instances[0], SpriteId::RouteDot, START));
    CHECK(Is(*manifest, drawing.instances[1], SpriteId::RouteDot, SECOND_DOT));
    CHECK(Is(*manifest, drawing.instances[2], SpriteId::RouteDot, THIRD_DOT));
    // The corner is 100 along, so the dot at 144 is 44 down the second leg.
    CHECK(Is(*manifest, drawing.instances[3], SpriteId::RouteDot, FOURTH_DOT));
    REQUIRE(drawing.batches.size() == 1);
    CHECK(drawing.batches[0].first == 0);
    CHECK(drawing.batches[0].count == L_DOTS + 1);
    CHECK(drawing.batches[0].pipeline == core::Pipeline::Normal);
}

TEST_CASE("the marker sits on the route's end") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const std::vector<core::Vec2> route{ core::Vec2{}, SHORT_CORNER, SHORT_END };
    const gfx::TerrainDrawing drawing = Drawn(route, *manifest);
    REQUIRE(drawing.instances.size() == SHORT_DOTS + 1);
    CHECK(Is(*manifest, drawing.instances.back(), SpriteId::RouteMarker, SHORT_END));
}

TEST_CASE("nothing is drawn without a route of two points") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const std::vector<core::Vec2> lone{ START };
    for (const std::span<const core::Vec2> route : { std::span<const core::Vec2>{}, std::span<const core::Vec2>{ lone } }) {
        const gfx::TerrainDrawing drawing = Drawn(route, *manifest);
        CHECK(drawing.instances.empty());
        CHECK(drawing.batches.empty());
    }
}

TEST_CASE("the cursor's tile draws its outline") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::Vec2 centre = CURSOR_TILE;
    const gfx::TerrainDrawing drawing = Targeted(centre, *manifest);
    REQUIRE(drawing.instances.size() == 1);
    CHECK(Is(*manifest, drawing.instances[0], SpriteId::TargetTile, centre));
    CHECK(drawing.batches.size() == 1);
}

TEST_CASE("no cursor draws no target tile") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const gfx::TerrainDrawing drawing = Targeted(std::nullopt, *manifest);
    CHECK(drawing.instances.empty());
    CHECK(drawing.batches.empty());
}

TEST_CASE("the target tile is laid out apart from the route's dots") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const std::vector<core::Vec2> route{ core::Vec2{}, ONE_STEP_END };
    const gfx::TerrainDrawing dots = Drawn(route, *manifest);
    REQUIRE(dots.instances.size() == APPENDED);
    for (const core::Instance& instance : dots.instances) {
        CHECK_FALSE(Is(*manifest, instance, SpriteId::TargetTile, CURSOR_TILE));
    }
    const gfx::TerrainDrawing target = Targeted(CURSOR_TILE, *manifest);
    REQUIRE(target.instances.size() == 1);
    CHECK(Is(*manifest, target.instances[0], SpriteId::TargetTile, CURSOR_TILE));
}

TEST_CASE("the route's batch covers only what was appended") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const std::vector<core::Vec2> route{ core::Vec2{}, ONE_STEP_END };
    gfx::TerrainDrawing drawing;
    drawing.instances.resize(ALREADY_THERE);
    drawing.batches.push_back({ .first = 0, .count = ALREADY_THERE, .pipeline = core::Pipeline::Normal });
    gfx::DrawRoute(route, Look(), *manifest, drawing);
    REQUIRE(drawing.instances.size() == ALREADY_THERE + APPENDED);
    CHECK(Is(*manifest, drawing.instances[ALREADY_THERE + APPENDED - 1], SpriteId::RouteMarker, ONE_STEP_END));
    REQUIRE(drawing.batches.size() == 2);
    CHECK(drawing.batches[1].first == ALREADY_THERE);
    CHECK(drawing.batches[1].count == APPENDED);
}

TEST_CASE("a route longer than the buffer keeps its end marker") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const f32 far = SPACING * static_cast<f32>(gfx::MAX_ROUTE_INSTANCES + gfx::MAX_ROUTE_INSTANCES);
    const std::vector<core::Vec2> route{ core::Vec2{}, { .x = far, .y = 0.0F } };
    const gfx::TerrainDrawing drawing = Drawn(route, *manifest);
    CHECK(drawing.instances.size() == gfx::MAX_ROUTE_INSTANCES);
    CHECK(Is(*manifest, drawing.instances.back(), SpriteId::RouteMarker, { .x = far, .y = 0.0F }));
}
