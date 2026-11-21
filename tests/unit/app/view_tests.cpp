#include "app/camera_control.hpp"
#include "app/view.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/tick_clock.hpp"
#include "gfx/fog.hpp"
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
constexpr gfx::FogFrame NO_FOG{};

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
    CHECK(app::Pick(snapshot, 1.0, ORIGIN, NO_FOG) == OVER);
    CHECK(app::PickInstance(snapshot, 1.0, ORIGIN, NO_FOG) == std::optional<std::size_t>{ 1 }); // what hover outlines
}

TEST_CASE("picking skips instances without an ID, and misses where nothing is drawn") {
    constexpr core::StableId ENTITY{ 1 };
    constexpr core::Vec2 FAR{ .x = 1'000.0F, .y = 0.0F };
    core::Snapshot snapshot;
    snapshot.instances = { Quad(ORIGIN, ORIGIN), Quad(ORIGIN, ORIGIN) };
    snapshot.ids = { ENTITY, core::StableId{} };
    CHECK(app::Pick(snapshot, 1.0, ORIGIN, NO_FOG) == ENTITY);
    CHECK(app::Pick(snapshot, 1.0, FAR, NO_FOG) == core::StableId{});
}

TEST_CASE("picking hits a moving instance where alpha draws it") {
    constexpr core::StableId MOVING{ 1 };
    core::Snapshot snapshot;
    snapshot.instances = { Quad(ORIGIN, MOVED) };
    snapshot.ids = { MOVING };
    CHECK(app::Pick(snapshot, 0.0, ORIGIN, NO_FOG) == MOVING);
    CHECK(app::Pick(snapshot, 1.0, ORIGIN, NO_FOG) == core::StableId{});
    CHECK(app::Pick(snapshot, 1.0, MOVED, NO_FOG) == MOVING);
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
    CHECK(app::Pick(snapshot, 0.0, ALONG_X, NO_FOG) == TURNING); // not turned yet: lying along x
    CHECK(app::Pick(snapshot, 0.0, ALONG_Y, NO_FOG) == core::StableId{});
    CHECK(app::Pick(snapshot, 1.0, ALONG_Y, NO_FOG) == TURNING); // a quarter turn: standing along y
    CHECK(app::Pick(snapshot, 1.0, ALONG_X, NO_FOG) == core::StableId{});
}

/// A one-tile fog over `values` (explored, then in sight, each 0 to 255), which every point of the map then samples.
gfx::FogFrame OneTile(const std::array<u8, 2>& values) {
    return { .columns = 1, .rows = 1, .tileSize = 256.0F, .values = values, .revision = 1 };
}

TEST_CASE("a walker drawn at under half opacity can't be picked and one over half can") {
    constexpr core::StableId WALKER{ 1 };
    core::Snapshot snapshot;
    snapshot.instances = { Quad(ORIGIN, ORIGIN) };
    snapshot.instances.front().flags = core::INSTANCE_SIGHT_FADE;
    snapshot.ids = { WALKER };
    const std::array<u8, 2> unseenBytes{ 255, 0 };
    const std::array<u8, 2> seenBytes{ 255, 255 };
    const std::array<u8, 2> justUnderBytes{ 255, 184 }; // sample about 0.72: fade about 0.41
    const std::array<u8, 2> justOverBytes{ 255, 200 };  // sample about 0.78: fade about 0.60
    const gfx::FogFrame unseen = OneTile(unseenBytes);
    CHECK(gfx::SightFade(unseen, ORIGIN) < HALF);
    CHECK(app::Pick(snapshot, 1.0, ORIGIN, unseen) == core::StableId{});
    CHECK_FALSE(app::PickInstance(snapshot, 1.0, ORIGIN, unseen).has_value());
    const gfx::FogFrame seen = OneTile(seenBytes);
    CHECK(gfx::SightFade(seen, ORIGIN) > HALF);
    CHECK(app::Pick(snapshot, 1.0, ORIGIN, seen) == WALKER);
    const gfx::FogFrame justUnder = OneTile(justUnderBytes);
    CHECK(gfx::SightFade(justUnder, ORIGIN) < HALF);
    CHECK(app::Pick(snapshot, 1.0, ORIGIN, justUnder) == core::StableId{});
    const gfx::FogFrame justOver = OneTile(justOverBytes);
    CHECK(gfx::SightFade(justOver, ORIGIN) > HALF);
    CHECK(app::Pick(snapshot, 1.0, ORIGIN, justOver) == WALKER);
}

TEST_CASE("a faded walker on top lets the instance under it be picked, and a missed one is never looked up") {
    constexpr core::StableId GROUND{ 1 };
    constexpr core::StableId WALKER{ 2 };
    constexpr core::Vec2 FAR{ .x = 1'000.0F, .y = 0.0F };
    core::Snapshot snapshot;
    snapshot.instances = { Quad(ORIGIN, ORIGIN), Quad(ORIGIN, ORIGIN) };
    snapshot.instances.back().flags = core::INSTANCE_SIGHT_FADE;
    snapshot.ids = { GROUND, WALKER };
    const std::array<u8, 2> unseenBytes{ 255, 0 };
    const std::array<u8, 2> seenBytes{ 255, 255 };
    const gfx::FogFrame unseen = OneTile(unseenBytes);
    const gfx::FogFrame seen = OneTile(seenBytes);
    CHECK(app::PickInstance(snapshot, 1.0, ORIGIN, unseen) == std::optional<std::size_t>{ 0 });
    CHECK(app::PickInstance(snapshot, 1.0, ORIGIN, seen) == std::optional<std::size_t>{ 1 });
    CHECK_FALSE(app::PickInstance(snapshot, 1.0, FAR, unseen).has_value());
}

TEST_CASE("an instance without the sight fade is picked whatever the fog says") {
    constexpr core::StableId TREE{ 1 };
    core::Snapshot snapshot;
    snapshot.instances = { Quad(ORIGIN, ORIGIN) };
    snapshot.ids = { TREE };
    const std::array<u8, 2> unseenBytes{ 255, 0 };
    const gfx::FogFrame unseen = OneTile(unseenBytes);
    CHECK(app::Pick(snapshot, 1.0, ORIGIN, unseen) == TREE);
}

TEST_CASE("without fog every instance is pickable as before") {
    constexpr core::StableId WALKER{ 1 };
    core::Snapshot snapshot;
    snapshot.instances = { Quad(ORIGIN, ORIGIN) };
    snapshot.instances.front().flags = core::INSTANCE_SIGHT_FADE;
    snapshot.ids = { WALKER };
    CHECK(app::Pick(snapshot, 1.0, ORIGIN, NO_FOG) == WALKER);
}

constexpr i32 WINDOW_WIDTH = 1280;
constexpr i32 WINDOW_HEIGHT = 720;

// Whether `area` is `expected`, within float rounding.
void CheckArea(const core::Rect& area, const core::Rect& expected) {
    CHECK(area.min.x == doctest::Approx(expected.min.x));
    CHECK(area.min.y == doctest::Approx(expected.min.y));
    CHECK(area.max.x == doctest::Approx(expected.max.x));
    CHECK(area.max.y == doctest::Approx(expected.max.y));
}

TEST_CASE("the cull area holds the window's view and its margin") {
    // At the reference centre, the window shows REFERENCE_SIZE / zoom, and the margin adds a quarter of it on each side.
    constexpr core::Rect WIDEST{ .min = { .x = -4'800.0F, .y = -2'700.0F }, .max = { .x = 6'720.0F, .y = 3'780.0F } };
    constexpr core::Rect REFERENCE{ .min = { .x = -480.0F, .y = -270.0F }, .max = { .x = 2'400.0F, .y = 1'350.0F } };
    constexpr core::Rect CLOSEST{ .min = { .x = 240.0F, .y = 135.0F }, .max = { .x = 1'680.0F, .y = 945.0F } };
    CheckArea(app::CullArea({ .zoom = gfx::MIN_ZOOM }, WINDOW_WIDTH, WINDOW_HEIGHT), WIDEST);  // 7,680 by 4,320 shown
    CheckArea(app::CullArea({ .zoom = 1.0F }, WINDOW_WIDTH, WINDOW_HEIGHT), REFERENCE);        // 1,920 by 1,080 shown
    CheckArea(app::CullArea({ .zoom = gfx::MAX_ZOOM }, WINDOW_WIDTH, WINDOW_HEIGHT), CLOSEST); // 960 by 540 shown
}

TEST_CASE("the cull area follows the window's size") {
    constexpr i32 WIDE_WIDTH = 2'560;
    constexpr i32 WIDE_HEIGHT = 1'080;
    // 2,560 by 1,080 is wider than 16:9: the height still fits 1,080 world units, and the width shows 2,560.
    constexpr core::Rect NARROWER{ .min = { .x = -480.0F, .y = -270.0F }, .max = { .x = 2'400.0F, .y = 1'350.0F } };
    constexpr core::Rect WIDER{ .min = { .x = -960.0F, .y = -270.0F }, .max = { .x = 2'880.0F, .y = 1'350.0F } };
    CheckArea(app::CullArea({}, WINDOW_WIDTH, WINDOW_HEIGHT), NARROWER);
    CheckArea(app::CullArea({}, WIDE_WIDTH, WIDE_HEIGHT), WIDER); // resized: the next frame's view widens with it
}

// The world rectangle `camera` shows in a window `width` by `height`.
core::Rect WindowArea(const gfx::Camera& camera, i32 width, i32 height) {
    return { .min = app::WindowToWorld(camera, {}, width, height),
             .max =
                 app::WindowToWorld(camera, { .x = static_cast<f32>(width), .y = static_cast<f32>(height) }, width, height) };
}

TEST_CASE("the cull margin holds a wheel notch out and a full-speed pan for the longest frame step") {
    struct Window {
        i32 width = 0;
        i32 height = 0;
    };
    constexpr std::array WINDOWS{
        Window{ .width = 1'280, .height = 720 },   // 16:9
        Window{ .width = 720, .height = 1'280 },   // upright: the width fits the reference
        Window{ .width = 3'840, .height = 1'080 }, // 32:9
    };
    constexpr auto ZOOMS = std::to_array({ gfx::MIN_ZOOM, 1.0F, gfx::MAX_ZOOM });
    constexpr auto SIGNS = std::to_array({ -1.0F, 1.0F });
    constexpr f32 SECONDS = static_cast<f32>(app::FRAME_STEP_LIMIT_NS) / static_cast<f32>(core::NS_PER_SECOND);
    for (const Window& window : WINDOWS) {
        for (const f32 zoom : ZOOMS) {
            for (const f32 sign : SIGNS) {
                CAPTURE(window.width);
                CAPTURE(zoom);
                CAPTURE(sign);
                const gfx::Camera before{ .zoom = zoom };
                const core::Rect area = app::CullArea(before, window.width, window.height);
                gfx::Camera after{ .zoom = gfx::ZoomedBy(zoom, -1.0F) };
                const core::Rect zoomedOut = WindowArea(after, window.width, window.height);
                const f32 screen = std::min(zoomedOut.max.x - zoomedOut.min.x, zoomedOut.max.y - zoomedOut.min.y);
                // Along both axes at once: farther than any pan, which is at most 1 long.
                const f32 pan = sign * app::PAN_SCREENS_PER_SECOND * screen * SECONDS;
                after.centre = after.centre + core::Vec2{ .x = pan, .y = pan };
                const core::Rect shown = WindowArea(after, window.width, window.height);
                CHECK(
                    (shown.min.x >= area.min.x && shown.min.y >= area.min.y && shown.max.x <= area.max.x &&
                     shown.max.y <= area.max.y)
                );
            }
        }
    }
}

TEST_CASE("the camera holds still while a calendar step's transition shows") {
    const core::Snapshot still;
    core::Snapshot following;
    following.follow = core::Follow{};
    core::Snapshot transition;
    transition.transition = true;
    CHECK(app::HoldsCamera(nullptr)); // nothing drawn yet
    CHECK_FALSE(app::HoldsCamera(&still));
    CHECK_FALSE(app::HoldsCamera(&following)); // whether a scene that follows pans is its camera kind's (CameraControl)
    CHECK(app::HoldsCamera(&transition));
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
