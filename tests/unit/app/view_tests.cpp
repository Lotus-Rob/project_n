#include "app/view.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/tick_clock.hpp"
#include "gfx/labels.hpp"

#include <doctest/doctest.h>

namespace {

constexpr f32 QUAD = 100.0F; // every test quad is this wide and high

core::Instance Quad(core::Vec2 previous, core::Vec2 current) {
    return { .previous = previous, .current = current, .size = core::PackPair(core::PackHalf(QUAD), core::PackHalf(QUAD)) };
}

constexpr core::Vec2 ORIGIN{};
constexpr core::Vec2 MOVED{ .x = 200.0F, .y = 0.0F };
constexpr f64 HALFWAY = 0.5;
constexpr f32 HALF = 0.5F;

TEST_CASE("the camera follows the target's foot interpolated by alpha") {
    const core::Follow follow{ .previous = ORIGIN, .current = MOVED };
    CHECK(app::FollowedCentre(follow, 0.0).x == ORIGIN.x);
    CHECK(app::FollowedCentre(follow, HALFWAY).x == doctest::Approx(MOVED.x * HALF));
    CHECK(app::FollowedCentre(follow, 1.0).x == MOVED.x);
}

TEST_CASE("the window's centre shows the camera's centre, and its top-left corner lies up and left") {
    constexpr i32 WIDTH = 1920;
    constexpr i32 HEIGHT = 1080;
    const gfx::Camera camera{ .centre = MOVED };
    const core::Vec2 centre = app::WindowToWorld(camera, { .x = WIDTH * HALF, .y = HEIGHT * HALF }, WIDTH, HEIGHT);
    CHECK(centre.x == doctest::Approx(MOVED.x));
    CHECK(centre.y == doctest::Approx(MOVED.y));
    const core::Vec2 corner = app::WindowToWorld(camera, {}, WIDTH, HEIGHT);
    CHECK(corner.x == doctest::Approx(MOVED.x - (gfx::REFERENCE_SIZE.x * HALF)));
    CHECK(corner.y == doctest::Approx(MOVED.y - (gfx::REFERENCE_SIZE.y * HALF)));
}

TEST_CASE("picking takes the instance drawn on top") {
    constexpr core::StableId UNDER{ 1 };
    constexpr core::StableId OVER{ 2 };
    core::Snapshot snapshot;
    snapshot.instances = { Quad(ORIGIN, ORIGIN), Quad(ORIGIN, ORIGIN) };
    snapshot.ids = { UNDER, OVER };
    CHECK(app::Pick(snapshot, 1.0, ORIGIN) == OVER);
    CHECK(app::PickInstance(snapshot, 1.0, ORIGIN) == std::optional<std::size_t>{ 1 }); // what hover outlines
}

TEST_CASE("picking skips instances without an ID, and misses where nothing is drawn") {
    constexpr core::StableId ENTITY{ 1 };
    constexpr core::Vec2 FAR{ .x = 1'000.0F, .y = 0.0F };
    core::Snapshot snapshot;
    snapshot.instances = { Quad(ORIGIN, ORIGIN), Quad(ORIGIN, ORIGIN) };
    snapshot.ids = { ENTITY, core::StableId{} };
    CHECK(app::Pick(snapshot, 1.0, ORIGIN) == ENTITY);
    CHECK(app::Pick(snapshot, 1.0, FAR) == core::StableId{});
}

TEST_CASE("picking hits a moving instance where alpha draws it") {
    constexpr core::StableId MOVING{ 1 };
    core::Snapshot snapshot;
    snapshot.instances = { Quad(ORIGIN, MOVED) };
    snapshot.ids = { MOVING };
    CHECK(app::Pick(snapshot, 0.0, ORIGIN) == MOVING);
    CHECK(app::Pick(snapshot, 1.0, ORIGIN) == core::StableId{});
    CHECK(app::Pick(snapshot, 1.0, MOVED) == MOVING);
}

TEST_CASE("picking turns with a rotated instance, as alpha draws it") {
    constexpr core::StableId TURNING{ 1 };
    constexpr f32 LENGTH = 100.0F;
    constexpr f32 THICKNESS = 10.0F;
    constexpr f32 QUARTER_TURN = std::numbers::pi_v<f32> / 2.0F;
    constexpr core::Vec2 ALONG_X{ .x = 40.0F, .y = 0.0F };
    constexpr core::Vec2 ALONG_Y{ .x = 0.0F, .y = 40.0F };
    core::Snapshot snapshot;
    snapshot.instances = { { .size = core::PackPair(core::PackHalf(LENGTH), core::PackHalf(THICKNESS)),
                             .rotation = core::PackPair(core::PackHalf(0.0F), core::PackHalf(QUARTER_TURN)) } };
    snapshot.ids = { TURNING };
    CHECK(app::Pick(snapshot, 0.0, ALONG_X) == TURNING); // not turned yet: lying along x
    CHECK(app::Pick(snapshot, 0.0, ALONG_Y) == core::StableId{});
    CHECK(app::Pick(snapshot, 1.0, ALONG_Y) == TURNING); // a quarter turn: standing along y
    CHECK(app::Pick(snapshot, 1.0, ALONG_X) == core::StableId{});
}

TEST_CASE("showing another scene puts the camera back at the reference view") {
    constexpr auto FIRST = core::SceneId{ 0 };
    constexpr auto SECOND = core::SceneId{ 1 };
    constexpr f32 ZOOMED = 2.0F;
    gfx::Camera camera{ .centre = MOVED, .zoom = ZOOMED };
    std::optional<core::SceneId> shown = FIRST;
    app::ShowScene(camera, shown, SECOND);
    CHECK(camera.centre.x == gfx::REFERENCE_CENTRE.x);
    CHECK(camera.centre.y == gfx::REFERENCE_CENTRE.y);
    CHECK(camera.zoom == 1.0F);
}

TEST_CASE("showing another scene starts it at the run's zoom") {
    constexpr auto FIRST = core::SceneId{ 0 };
    constexpr auto SECOND = core::SceneId{ 1 };
    gfx::Camera camera{ .centre = MOVED, .zoom = 1.0F };
    std::optional<core::SceneId> shown = FIRST;
    app::ShowScene(camera, shown, SECOND, gfx::MIN_ZOOM);
    CHECK(camera.centre.x == gfx::REFERENCE_CENTRE.x);
    CHECK(camera.zoom == gfx::MIN_ZOOM);
}

TEST_CASE("showing the same scene again leaves the camera where the player put it") {
    constexpr auto FIRST = core::SceneId{ 0 };
    constexpr f32 ZOOMED = 2.0F;
    gfx::Camera camera{ .centre = MOVED, .zoom = ZOOMED };
    std::optional<core::SceneId> shown = FIRST;
    app::ShowScene(camera, shown, FIRST);
    CHECK(camera.centre.x == MOVED.x);
    CHECK(camera.zoom == ZOOMED);
}

TEST_CASE("labels fade while the frame clock runs and the render clock holds, as when the game is paused") {
    constexpr u64 FRAME_NS = 50 * core::NS_PER_MS; // three frames make LABEL_FADE_SECONDS
    constexpr f64 PAUSED_ALPHA = 0.25;             // the render clock's part in the view: held, as while paused
    constexpr u32 SCREEN_WIDTH = 1280;
    constexpr u32 SCREEN_HEIGHT = 720;
    constexpr f32 READABLE = 13.0F;
    constexpr f32 TOO_SMALL = 5.0F;
    constexpr f32 ONE_THIRD = 1.0F / 3.0F;
    constexpr f32 TWO_THIRDS = 2.0F / 3.0F;
    constexpr core::Vec2 ANCHOR{ .x = 100.0F, .y = 300.0F };
    constexpr f32 BOX_WIDTH = 40.0F;
    constexpr f32 BOX_HEIGHT = 20.0F;
    std::array<gfx::LabelInput, 1> labels{
        { { .anchor = ANCHOR, .width = BOX_WIDTH, .height = BOX_HEIGHT, .pixels = READABLE } }
    };
    app::FrameClock frames;
    gfx::LabelPass pass;
    const auto place = [&](u64 nowNs) {
        const gfx::View view = app::FrameView({}, PAUSED_ALPHA, std::nullopt, frames.Advance(nowNs));
        const std::span<const gfx::PlacedLabel> placed = pass.Place(labels, SCREEN_WIDTH, SCREEN_HEIGHT, view.frameClock, 1.0F);
        REQUIRE(placed.size() == 1);
        return placed[0].opacity;
    };
    CHECK(place(0) == 1.0F);
    labels[0].pixels = TOO_SMALL; // zoomed out while paused
    CHECK(place(FRAME_NS) == doctest::Approx(TWO_THIRDS));
    CHECK(place(2 * FRAME_NS) == doctest::Approx(ONE_THIRD));
    CHECK(place(3 * FRAME_NS) == doctest::Approx(0.0F));
}

TEST_CASE("the frame clock starts at zero and moves on by wall time") {
    constexpr u64 START_NS = 7 * core::NS_PER_SECOND;
    constexpr u64 STEP_NS = 20 * core::NS_PER_MS;
    constexpr f64 STEP_SECONDS = 0.02;
    constexpr f64 TWO_STEPS_SECONDS = 0.04;
    app::FrameClock frames;
    CHECK(frames.Advance(START_NS) == 0.0);
    CHECK(frames.Advance(START_NS + STEP_NS) == doctest::Approx(STEP_SECONDS));
    CHECK(frames.Advance(START_NS + (2 * STEP_NS)) == doctest::Approx(TWO_STEPS_SECONDS));
}

TEST_CASE("a long gap moves the frame clock on by its step limit at most") {
    constexpr u64 MINUTE_NS = 60 * core::NS_PER_SECOND;
    constexpr f64 LIMIT_SECONDS = static_cast<f64>(app::FRAME_STEP_LIMIT_NS) / static_cast<f64>(core::NS_PER_SECOND);
    constexpr f64 TENTH = 0.1;
    app::FrameClock frames;
    static_cast<void>(frames.Advance(0));
    CHECK(frames.Advance(MINUTE_NS) == doctest::Approx(LIMIT_SECONDS));
    CHECK(LIMIT_SECONDS == doctest::Approx(TENTH));
}

} // namespace
