#include "app/camera_path.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/tick_clock.hpp"
#include "game/host.hpp"
#include "support/manifest.hpp"
#include "support/world.hpp"

#include <doctest/doctest.h>

namespace {

constexpr f32 LAST = 22.0F; // the end of the path's last segment

TEST_CASE("the camera path ends with a quit after its last segment") {
    constexpr f32 AFTER = 22.05F;
    CHECK_FALSE(app::CameraPathOver(0.0F));
    CHECK_FALSE(app::CameraPathOver(LAST));
    CHECK(app::CameraPathSegment(LAST) == app::CameraPath().size() - 1);
    CHECK(app::CameraPathOver(AFTER));
    CHECK_FALSE(app::CameraPathSegment(AFTER));
}

TEST_CASE("the camera path holds Move from its start through its follow segment only") {
    constexpr f32 FOLLOW_END = 4.0F;
    constexpr f32 PANNING = 4.05F;
    for (const f32 seconds : { 0.0F, FOLLOW_END }) {
        CAPTURE(seconds);
        const core::Vec2 move = app::CameraPathMove(seconds);
        CHECK(move.x == 1.0F); // east
        CHECK(move.y == 0.0F);
    }
    for (const f32 seconds : { PANNING, LAST }) {
        CAPTURE(seconds);
        const core::Vec2 move = app::CameraPathMove(seconds);
        CHECK(move.x == 0.0F);
        CHECK(move.y == 0.0F);
    }
}

TEST_CASE("the camera path's walk east from the world's start keeps clear of the world's portal") {
    // Every walking segment's Move points east, at the built-in game data's walk speed (world units a tick), so the foot
    // stays on the strip from the start to this far east of it; the portal's step area, and a radius to spare, stay off it,
    // or a measured run would change scene partway.
    f32 walking = 0.0F;
    for (const app::CameraSegment& segment : app::CameraPath()) {
        walking += segment.walks ? segment.seconds : 0.0F;
    }
    const f32 reach = walking * static_cast<f32>(core::TICKS_PER_SECOND) * game::BuiltInData().cultivator.walkSpeed;
    game::Host host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    const core::Domain& active = host.State()[game::ACTIVE];
    const core::Vec2 start = active.Entities().get<game::Body>(active.Find(host.Player())).current;
    const std::optional<core::Vec2> portal = host.Portal();
    REQUIRE(portal.has_value());
    const core::Vec2 point = portal.value_or(core::Vec2{});
    REQUIRE(reach > 0.0F);
    constexpr f32 ACROSS = 2.0F * game::PORTAL_RADIUS; // the step area's half width and a radius to spare
    constexpr f32 DEEP = (game::PORTAL_RADIUS * game::PORTAL_DEPTH_RATIO) + game::PORTAL_RADIUS;
    const bool west = point.x + ACROSS < start.x;
    const bool east = point.x - ACROSS > start.x + reach;
    const bool offTheRow = std::abs(point.y - start.y) > DEEP;
    CAPTURE(reach);
    CHECK((west || east || offTheRow));
}

} // namespace
