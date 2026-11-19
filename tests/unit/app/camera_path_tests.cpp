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

TEST_CASE("the camera path walks east in steps through its follow segment only") {
    constexpr f32 FOLLOW_END = 4.0F;
    constexpr f32 PANNING = 4.05F;
    // Standing for the first tenth of every half second, east for the rest.
    for (const f32 seconds : { 0.0F, 0.05F, 0.55F, 3.55F, FOLLOW_END }) {
        CAPTURE(seconds);
        const core::Vec2 move = app::CameraPathMove(seconds);
        CHECK(move.x == 0.0F);
        CHECK(move.y == 0.0F);
    }
    for (const f32 seconds : { 0.1F, 0.3F, 0.6F, 3.95F }) {
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

TEST_CASE("the follow segment makes eight presses") {
    constexpr i32 SAMPLES_PER_SECOND = 600;
    i32 presses = 0;
    bool eastBefore = false;
    for (i32 i = 0; i <= static_cast<i32>(LAST) * SAMPLES_PER_SECOND; ++i) {
        const bool east = app::CameraPathMove(static_cast<f32>(i) / static_cast<f32>(SAMPLES_PER_SECOND)).x > 0.0F;
        presses += (east && !eastBefore) ? 1 : 0;
        eastBefore = east;
    }
    CHECK(presses == 8);
}

TEST_CASE("the camera path's walk east from the world's start keeps clear of the world's portal") {
    // Move points east for at most the walking segments' length, at the built-in game data's walk speed (world units a
    // tick), so the foot stays on the strip from the start to this far east of it; the portal's step area, and a radius to
    // spare, stay off it, or a measured run would change scene partway.
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

TEST_CASE("the month step comes once, at its second or the first frame after, and never without one") {
    constexpr f32 STEP_SECOND = 15.0F;
    constexpr f32 JUST_BEFORE = 14.99F;
    constexpr f32 NEXT_FRAME = 15.02F;
    constexpr f32 AFTER = 15.04F;
    app::PathMonthStep step{ STEP_SECOND };
    CHECK_FALSE(step.Due(0.0F));
    CHECK_FALSE(step.Due(JUST_BEFORE));
    CHECK(step.Due(NEXT_FRAME));
    CHECK_FALSE(step.Due(AFTER));
    CHECK_FALSE(step.Due(LAST));
    app::PathMonthStep first{ 0.0F };
    CHECK(first.Due(0.0F));
    CHECK_FALSE(first.Due(AFTER));
    app::PathMonthStep none{ std::nullopt };
    CHECK_FALSE(none.Due(STEP_SECOND));
}

TEST_CASE("the measured month step falls in the widest view over the busiest roads") {
    constexpr f32 MEASURED = 15.0F; // `--month-step-at` in measure's world run (MEASURE_RUNS in tools/project_n.py)
    const std::optional<std::size_t> segment = app::CameraPathSegment(MEASURED);
    REQUIRE(segment.has_value());
    CHECK(app::CameraPath()[segment.value_or(0)].name == "widest view");
}

} // namespace
