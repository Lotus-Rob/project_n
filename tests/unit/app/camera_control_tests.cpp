#include "app/bindings.hpp"
#include "app/camera_control.hpp"
#include "app/view.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "game/scene.hpp"
#include "game/scenes.hpp"
#include "game/scenes/world.hpp"
#include "gfx/camera.hpp"

#include <doctest/doctest.h>

namespace {

using core::CameraKind;

// A window the reference size fits exactly: at zoom 1 a pixel is a world unit, and the shorter side is its height.
constexpr i32 WIDTH = 1'920;
constexpr i32 HEIGHT = 1'080;
constexpr f32 SECONDS = 0.05F; // one frame's time
constexpr f32 HALF_ZOOM = 0.5F;
constexpr f32 DOUBLE_ZOOM = 2.0F;
constexpr core::Vec2 WINDOW_CENTRE{ .x = WIDTH / 2.0F, .y = HEIGHT / 2.0F };
constexpr core::Vec2 PLAYER{ .x = 5'000.0F, .y = -3'000.0F };
constexpr core::Vec2 WALKED{ .x = 5'400.0F, .y = -2'800.0F }; // the player a few steps on
constexpr core::Vec2 RIGHT{ .x = 1.0F, .y = 0.0F };
constexpr core::Vec2 DOWN{ .x = 0.0F, .y = 1.0F };
constexpr core::Vec2 LEFT_EDGE{ .x = 0.0F, .y = 500.0F }; // on the window's left edge, in the edge band
constexpr f32 PAST = 5.0F;                                // pixels past the window's edge, as a captured mouse reports

// A frame of `kind`'s first scene change with nothing pressed and the cursor away from the edges.
app::CameraInput Frame(CameraKind kind) {
    return { .kind = kind, .cursor = WINDOW_CENTRE, .width = WIDTH, .height = HEIGHT, .seconds = SECONDS };
}

// The world distance a full-speed pan covers in one frame at `zoom` in the test window.
f32 PanStep(f32 zoom) {
    return app::PAN_SCREENS_PER_SECOND * (static_cast<f32>(HEIGHT) / zoom) * SECONDS;
}

// The edge band's width in the test window's pixels.
constexpr f32 BAND = app::EDGE_BAND * static_cast<f32>(HEIGHT);

void CheckAt(const app::CameraControl& control, core::Vec2 expected) {
    CHECK(control.Camera().centre.x == doctest::Approx(expected.x));
    CHECK(control.Camera().centre.y == doctest::Approx(expected.y));
}

TEST_CASE("a scene that follows nothing starts its camera where its publishes cull before a view arrives") {
    std::size_t still = 0;
    for (const game::Scene* scene : game::Scenes()) {
        const game::SceneTraits& traits = scene->Traits();
        if (traits.camera != CameraKind::Pan) {
            continue; // these follow the player, round whom the host culls
        }
        ++still;
        CAPTURE(traits.name);
        app::CameraControl control{ 1.0F };
        control.Update(Frame(CameraKind::Pan));
        constexpr f32 HALF = 0.5F;
        CheckAt(control, (traits.view.min + traits.view.max) * HALF); // its centre
    }
    CHECK(still == 2); // painted and cells
}

TEST_CASE("dragging moves the world with the pointer") {
    constexpr core::Vec2 FROM{ .x = 1'000.0F, .y = 500.0F };
    constexpr core::Vec2 DROPPED{ .x = 900.0F, .y = 500.0F }; // 100 pixels left
    for (const f32 zoom : { HALF_ZOOM, DOUBLE_ZOOM }) {
        CAPTURE(zoom);
        app::CameraControl control{ zoom };
        control.Update(Frame(CameraKind::Pan));
        const gfx::Camera before = control.Camera();
        app::CameraInput drag = Frame(CameraKind::Pan);
        drag.dragFrom = FROM;
        drag.cursor = DROPPED;
        control.Update(drag);
        // The world point that was under the pointer is under it still: the centre moved 100 pixels' worth of world.
        const core::Vec2 grabbed = app::WindowToWorld(before, FROM, WIDTH, HEIGHT);
        const core::Vec2 held = app::WindowToWorld(control.Camera(), DROPPED, WIDTH, HEIGHT);
        CHECK(held.x == doctest::Approx(grabbed.x));
        CHECK(held.y == doctest::Approx(grabbed.y));
        CheckAt(control, { .x = before.centre.x + (100.0F / zoom), .y = before.centre.y });
        CHECK(control.Camera().zoom == zoom);
    }
}

TEST_CASE("edge pan starts only inside the edge band and moves the same screens per second at every zoom") {
    constexpr f32 DIAGONAL = std::numbers::sqrt2_v<f32> / 2.0F;
    struct Sample {
        core::Vec2 cursor;
        core::Vec2 direction; // where it pans, in steps
    };
    const std::array samples{
        Sample{ .cursor = { .x = 0.0F, .y = 500.0F }, .direction = { .x = -1.0F, .y = 0.0F } },         // the left edge
        Sample{ .cursor = { .x = BAND - 1.0F, .y = 500.0F }, .direction = { .x = -1.0F, .y = 0.0F } },  // just inside it
        Sample{ .cursor = { .x = BAND + 1.0F, .y = 500.0F }, .direction = {} },                         // just outside
        Sample{ .cursor = { .x = WIDTH - 1.0F, .y = 500.0F }, .direction = { .x = 1.0F, .y = 0.0F } },  // the right edge
        Sample{ .cursor = { .x = 900.0F, .y = 0.0F }, .direction = { .x = 0.0F, .y = -1.0F } },         // the top
        Sample{ .cursor = { .x = 900.0F, .y = HEIGHT - BAND - 1.0F }, .direction = {} },                // above the bottom band
        Sample{ .cursor = { .x = 900.0F, .y = HEIGHT - 1.0F }, .direction = { .x = 0.0F, .y = 1.0F } }, // the bottom
        Sample{ .cursor = WINDOW_CENTRE, .direction = {} },                                             // the middle
        Sample{ .cursor = { .x = WIDTH - 1.0F, .y = HEIGHT - 1.0F },
                .direction = { .x = DIAGONAL, .y = DIAGONAL } }, // a corner: no faster
    };
    for (const f32 zoom : { HALF_ZOOM, 1.0F, DOUBLE_ZOOM }) {
        for (const Sample& sample : samples) {
            CAPTURE(zoom);
            CAPTURE(sample.cursor.x);
            CAPTURE(sample.cursor.y);
            app::CameraControl control{ zoom };
            control.Update(Frame(CameraKind::Pan));
            const core::Vec2 before = control.Camera().centre;
            app::CameraInput edge = Frame(CameraKind::Pan);
            edge.cursor = sample.cursor;
            control.Update(edge);
            CheckAt(control, before + (sample.direction * PanStep(zoom)));
        }
    }
}

TEST_CASE("edge pan stops while the cursor is outside the window, or edge pan is off") {
    app::CameraInput away = Frame(CameraKind::Pan);
    away.cursor = std::nullopt; // over another window
    app::CameraInput leftOf = Frame(CameraKind::Pan);
    leftOf.cursor = core::Vec2{ .x = -PAST, .y = LEFT_EDGE.y };
    app::CameraInput rightOf = Frame(CameraKind::Pan);
    rightOf.cursor = core::Vec2{ .x = WIDTH + PAST, .y = LEFT_EDGE.y };
    app::CameraInput below = Frame(CameraKind::Pan);
    below.cursor = core::Vec2{ .x = WINDOW_CENTRE.x, .y = HEIGHT }; // just past the last row of pixels
    app::CameraInput off = Frame(CameraKind::Pan);
    off.cursor = LEFT_EDGE;
    off.edgePan = false; // settings.yaml's edge_pan
    app::CameraInput unfocused = Frame(CameraKind::Pan);
    unfocused.cursor = LEFT_EDGE;
    unfocused.focused = false;
    for (const app::CameraInput& input : { away, leftOf, rightOf, below, off, unfocused }) {
        app::CameraControl control{ 1.0F };
        control.Update(Frame(CameraKind::Pan));
        control.Update(input);
        CheckAt(control, gfx::REFERENCE_CENTRE);
    }
}

TEST_CASE("edge pan waits until the cursor has been inside the window clear of the edge band") {
    app::CameraControl control{ 1.0F };
    app::CameraInput frame = Frame(CameraKind::Roam);
    frame.followed = PLAYER;
    frame.cursor = std::nullopt; // over another window
    control.Update(frame);
    frame.cursor = LEFT_EDGE; // arriving across the window's edge
    control.Update(frame);
    CheckAt(control, PLAYER);
    CHECK_FALSE(control.Free()); // still following
    frame.cursor = WINDOW_CENTRE;
    control.Update(frame);
    frame.cursor = LEFT_EDGE; // pushed to the edge from inside
    control.Update(frame);
    CHECK(control.Free());
    CheckAt(control, PLAYER + (RIGHT * -PanStep(1.0F)));
}

TEST_CASE("regaining the focus disarms edge pan until the cursor is clear of the edge band") {
    app::CameraInput unfocused = Frame(CameraKind::Pan);
    unfocused.focused = false;
    app::CameraInput edge = Frame(CameraKind::Pan);
    edge.cursor = LEFT_EDGE;
    // Seen unfocused in a frame, or told while no frame was drawn (minimised).
    for (const bool drawnUnfocused : { true, false }) {
        CAPTURE(drawnUnfocused);
        app::CameraControl control{ 1.0F };
        control.Update(Frame(CameraKind::Pan)); // armed: the cursor in the middle
        if (drawnUnfocused) {
            control.Update(unfocused);
        }
        else {
            control.LoseFocus();
        }
        control.Update(edge);
        CheckAt(control, gfx::REFERENCE_CENTRE);
        control.Update(Frame(CameraKind::Pan));
        control.Update(edge);
        CheckAt(control, gfx::REFERENCE_CENTRE + (RIGHT * -PanStep(1.0F)));
    }
}

TEST_CASE("a snap back pressed while the camera holds still applies once it may move") {
    app::CameraControl control{ 1.0F };
    app::CameraInput frame = Frame(CameraKind::Roam);
    frame.followed = PLAYER;
    frame.pan = RIGHT;
    control.Update(frame);
    REQUIRE(control.Free());
    frame.pan = {};
    frame.still = true; // a calendar step's transition shows
    control.SnapBack();
    control.Update(frame);
    CHECK(control.Free());
    frame.still = false;
    frame.followed = WALKED;
    control.Update(frame);
    CHECK_FALSE(control.Free());
    CheckAt(control, WALKED);
    // One pressed as a scene change arrives is dropped with the old scene: the new one starts afresh anyway. The change
    // arrives while the camera holds still, so a snap back kept would still wait and undo the first pan after it.
    control.SnapBack();
    frame.change = 2;
    frame.still = true;
    control.Update(frame);
    frame.still = false;
    frame.pan = RIGHT;
    control.Update(frame);
    CHECK(control.Free());
}

TEST_CASE("the middle button drags the camera, and the right one outside combat") {
    CHECK(app::DragsCamera(app::Control::DragCamera, false));
    CHECK(app::DragsCamera(app::Control::DragCamera, true)); // a following scene ignores the drag
    CHECK(app::DragsCamera(app::Control::Secondary, false));
    CHECK_FALSE(app::DragsCamera(app::Control::Secondary, true)); // it casts the spender there
    CHECK_FALSE(app::DragsCamera(app::Control::Primary, false));  // the left stays free for selecting
}

TEST_CASE("a drag ends when focus is lost") {
    constexpr core::Vec2 PRESSED{ .x = 1'000.0F, .y = 500.0F };
    constexpr core::Vec2 MOVED{ .x = 950.0F, .y = 520.0F };
    constexpr core::Vec2 LATER{ .x = 900.0F, .y = 540.0F };
    app::Drag drag;
    drag.Press(app::Control::DragCamera, PRESSED);
    const std::optional<core::Vec2> first = drag.Take(MOVED, true, WIDTH, HEIGHT);  // this frame's movement starts at the press
    const std::optional<core::Vec2> second = drag.Take(LATER, true, WIDTH, HEIGHT); // the next where the last ended
    const std::optional<core::Vec2> unfocusedFrom = drag.Take(LATER, false, WIDTH, HEIGHT); // focus lost: it ends
    const std::optional<core::Vec2> refocused = drag.Take(MOVED, true, WIDTH, HEIGHT);      // and stays ended
    REQUIRE(first.has_value());
    CHECK(first.value_or(core::Vec2{}).x == PRESSED.x);
    REQUIRE(second.has_value());
    CHECK(second.value_or(core::Vec2{}).x == MOVED.x);
    CHECK_FALSE(unfocusedFrom.has_value());
    CHECK_FALSE(refocused.has_value());
    // Nor does the camera move by a drag given while the window is unfocused.
    app::CameraControl control{ 1.0F };
    control.Update(Frame(CameraKind::Pan));
    app::CameraInput unfocused = Frame(CameraKind::Pan);
    unfocused.dragFrom = PRESSED;
    unfocused.cursor = MOVED;
    unfocused.focused = false;
    control.Update(unfocused);
    CheckAt(control, gfx::REFERENCE_CENTRE);
}

TEST_CASE("a drag ends when the window loses focus while no frame is drawn") {
    constexpr core::Vec2 PRESSED{ .x = 1'000.0F, .y = 500.0F };
    app::Drag drag;
    drag.Press(app::Control::DragCamera, PRESSED);
    drag.End(); // minimised with the button down: its release never reaches the window
    const std::optional<core::Vec2> restored = drag.Take(PRESSED, true, WIDTH, HEIGHT);
    CHECK_FALSE(restored.has_value());
}

TEST_CASE("a drag ends when the pointer leaves the window") {
    constexpr core::Vec2 PRESSED{ .x = 1'000.0F, .y = 500.0F };
    constexpr core::Vec2 OUTSIDE{ .x = -10.0F, .y = 500.0F }; // a captured mouse past the window's edge
    for (const std::optional<core::Vec2> left : { std::optional<core::Vec2>{}, std::optional{ OUTSIDE } }) {
        app::Drag drag;
        drag.Press(app::Control::DragCamera, PRESSED);
        const std::optional<core::Vec2> leaving = drag.Take(left, true, WIDTH, HEIGHT);
        const std::optional<core::Vec2> back = drag.Take(PRESSED, true, WIDTH, HEIGHT); // over the window again: still ended
        CHECK_FALSE(leaving.has_value());
        CHECK_FALSE(back.has_value());
    }
}

TEST_CASE("a drag ends when the button that started it is released") {
    constexpr core::Vec2 PRESSED{ .x = 1'000.0F, .y = 500.0F };
    app::Drag drag;
    drag.Press(app::Control::Secondary, PRESSED);
    drag.Release(app::Control::DragCamera); // another button: the drag goes on
    const std::optional<core::Vec2> goingOn = drag.Take(PRESSED, true, WIDTH, HEIGHT);
    drag.Release(app::Control::Secondary);
    const std::optional<core::Vec2> ended = drag.Take(PRESSED, true, WIDTH, HEIGHT);
    CHECK(goingOn.has_value());
    CHECK_FALSE(ended.has_value());
}

TEST_CASE("in a roaming scene, panning frees the camera and snap back follows again") {
    app::CameraInput keys = Frame(CameraKind::Roam);
    keys.pan = RIGHT;
    app::CameraInput edge = Frame(CameraKind::Roam);
    edge.cursor = LEFT_EDGE;
    app::CameraInput drag = Frame(CameraKind::Roam);
    drag.dragFrom = WINDOW_CENTRE;
    drag.cursor = WINDOW_CENTRE + (RIGHT * -PAST);
    for (app::CameraInput panned : { keys, edge, drag }) {
        app::CameraControl control{ 1.0F };
        app::CameraInput following = Frame(CameraKind::Roam);
        following.followed = PLAYER;
        control.Update(following);
        CheckAt(control, PLAYER);
        CHECK_FALSE(control.Free());
        panned.followed = PLAYER;
        control.Update(panned);
        CHECK(control.Free());
        CHECK(control.Camera().centre.x != doctest::Approx(PLAYER.x)); // panned off the player
        const core::Vec2 left = control.Camera().centre;
        following.followed = WALKED;
        control.Update(following);
        CheckAt(control, left); // free: where the player left it, though the player walked on
        control.SnapBack();
        control.Update(following);
        CHECK_FALSE(control.Free());
        CheckAt(control, WALKED);
    }
}

TEST_CASE("walking doesn't re-attach a free camera") {
    app::CameraControl control{ 1.0F };
    app::CameraInput frame = Frame(CameraKind::Roam);
    frame.followed = PLAYER;
    frame.pan = RIGHT;
    control.Update(frame);
    const core::Vec2 left = control.Camera().centre;
    frame.pan = {};
    frame.move = DOWN; // walking, which in a roaming scene moves the player, not the camera
    frame.followed = WALKED;
    control.Update(frame);
    CHECK(control.Free());
    CheckAt(control, left);
}

TEST_CASE("a following scene ignores pan, drag, edge and snap back") {
    app::CameraControl control{ 1.0F };
    app::CameraInput frame = Frame(CameraKind::Follow);
    frame.followed = PLAYER;
    frame.move = RIGHT;
    frame.pan = DOWN;
    frame.cursor = core::Vec2{ .x = 0.0F, .y = 0.0F }; // a corner, in the edge band
    frame.dragFrom = WINDOW_CENTRE;
    control.Update(frame);
    CheckAt(control, PLAYER);
    frame.followed = WALKED;
    control.SnapBack();
    control.Update(frame);
    CheckAt(control, WALKED); // the player only, wherever it walks
    CHECK_FALSE(control.Free());
}

TEST_CASE("a still scene pans with Move and the pan keys") {
    app::CameraControl control{ 1.0F };
    app::CameraInput frame = Frame(CameraKind::Pan);
    frame.move = RIGHT;
    control.Update(frame);
    CheckAt(control, gfx::REFERENCE_CENTRE + (RIGHT * PanStep(1.0F)));
    frame.move = {};
    frame.pan = DOWN;
    control.Update(frame);
    CheckAt(control, gfx::REFERENCE_CENTRE + ((RIGHT + DOWN) * PanStep(1.0F)));
    frame.move = RIGHT;
    frame.pan = RIGHT; // both at once: no faster than one
    control.Update(frame);
    CheckAt(control, gfx::REFERENCE_CENTRE + (RIGHT + RIGHT + DOWN) * PanStep(1.0F));
    CHECK_FALSE(control.Free()); // it follows nothing to be freed from
    frame.move = {};
    frame.pan = {};
    control.SnapBack(); // back to the scene's own view
    control.Update(frame);
    CheckAt(control, gfx::REFERENCE_CENTRE);
}

TEST_CASE("a frame longer than the frame step limit pans no farther than the limit") {
    constexpr f32 STALL = 2.0F; // seconds, as after a stall
    constexpr f32 LIMIT = static_cast<f32>(app::FRAME_STEP_LIMIT_NS) / static_cast<f32>(core::NS_PER_SECOND);
    app::CameraControl control{ 1.0F };
    app::CameraInput frame = Frame(CameraKind::Pan);
    frame.pan = RIGHT;
    frame.seconds = STALL;
    control.Update(frame);
    CheckAt(control, gfx::REFERENCE_CENTRE + (RIGHT * (PanStep(1.0F) * LIMIT / SECONDS)));
}

TEST_CASE("each scene change starts from the scene's view, following, between scenes sharing sprites too") {
    constexpr f32 START_ZOOM = 0.6F; // the run's --zoom
    constexpr u64 FREED = 3;         // the change the camera is freed in
    app::CameraControl control{ START_ZOOM };
    app::CameraInput frame = Frame(CameraKind::Roam);
    frame.change = FREED;
    frame.followed = PLAYER;
    control.Update(frame);
    frame.pan = RIGHT;
    control.Zoom(1.0F);
    control.Update(frame);
    REQUIRE(control.Free());
    REQUIRE(control.Camera().zoom != doctest::Approx(START_ZOOM));
    // The next change enters a scene, maybe drawn with the same sprites (the camera never asks which): it starts afresh.
    frame.change = FREED + 1;
    frame.pan = {};
    frame.followed = WALKED;
    control.Update(frame);
    CHECK_FALSE(control.Free());
    CheckAt(control, WALKED);
    CHECK(control.Camera().zoom == START_ZOOM);
    // A scene that follows nothing starts at the reference view's centre.
    app::CameraInput still = Frame(CameraKind::Pan);
    still.change = FREED + 2;
    control.Update(still);
    CheckAt(control, gfx::REFERENCE_CENTRE);
    CHECK(control.Camera().zoom == START_ZOOM);
}

TEST_CASE("zoom stays within 0.25 and 2") {
    constexpr i32 FRAMES = 40;           // more than the notches need to reach either limit
    constexpr f32 FAR_IN = 30.0F;        // notches: from 1, the closest zoom is 5 away
    constexpr f32 FAR_OUT = -2 * FAR_IN; // and the farthest 10 from there
    app::CameraControl control{ 1.0F };
    control.Update(Frame(CameraKind::Pan));
    control.Zoom(FAR_IN);
    for (i32 frame = 0; frame < FRAMES; ++frame) {
        control.Update(Frame(CameraKind::Pan));
    }
    CHECK(control.Camera().zoom == gfx::MAX_ZOOM);
    control.Zoom(FAR_OUT);
    for (i32 frame = 0; frame < FRAMES; ++frame) {
        control.Update(Frame(CameraKind::Pan));
    }
    CHECK(control.Camera().zoom == gfx::MIN_ZOOM);
}

TEST_CASE("zoom takes one wheel notch a frame, and drops notches past its limit") {
    constexpr f32 NOTCHES = 3.0F;
    constexpr f32 PAST_CLOSEST = 20.0F; // notches, from about 1.5: the closest zoom is 2 away
    constexpr i32 FRAMES = 10;          // enough for the 2, not for all 20
    app::CameraControl control{ 1.0F };
    control.Update(Frame(CameraKind::Pan));
    control.Zoom(NOTCHES);
    control.Update(Frame(CameraKind::Pan));
    CHECK(control.Camera().zoom == doctest::Approx(gfx::ZoomedBy(1.0F, 1.0F))); // one notch, the rest waiting
    control.Update(Frame(CameraKind::Pan));
    control.Update(Frame(CameraKind::Pan));
    CHECK(control.Camera().zoom == doctest::Approx(gfx::ZoomedBy(1.0F, NOTCHES)));
    control.Update(Frame(CameraKind::Pan));
    CHECK(control.Camera().zoom == doctest::Approx(gfx::ZoomedBy(1.0F, NOTCHES))); // none left
    // At the closest zoom, notches further in are dropped: the next notch out zooms out at once.
    control.Zoom(PAST_CLOSEST);
    for (i32 frame = 0; frame < FRAMES; ++frame) {
        control.Update(Frame(CameraKind::Pan));
    }
    REQUIRE(control.Camera().zoom == gfx::MAX_ZOOM);
    control.Zoom(-1.0F);
    control.Update(Frame(CameraKind::Pan));
    CHECK(control.Camera().zoom == doctest::Approx(gfx::ZoomedBy(gfx::MAX_ZOOM, -1.0F)));
}

TEST_CASE("the camera holds still while a calendar step's transition shows, and still follows") {
    app::CameraControl control{ 1.0F };
    app::CameraInput frame = Frame(CameraKind::Roam);
    frame.followed = PLAYER;
    control.Update(frame);
    frame.still = true;
    frame.pan = RIGHT;
    frame.cursor = LEFT_EDGE;
    frame.dragFrom = WINDOW_CENTRE;
    frame.followed = WALKED;
    control.Zoom(1.0F);
    control.Update(frame);
    CHECK_FALSE(control.Free());
    CheckAt(control, WALKED);
    CHECK(control.Camera().zoom == 1.0F); // the notch waits for the transition's end
    frame.still = false;
    frame.pan = {};
    frame.cursor = WINDOW_CENTRE;
    frame.dragFrom = std::nullopt;
    control.Update(frame);
    CHECK(control.Camera().zoom == doctest::Approx(gfx::ZoomedBy(1.0F, 1.0F)));
}

TEST_CASE("the camera path gives each segment's camera at its times") {
    constexpr core::Vec2 MIDWAY{ .x = 5'200.0F, .y = -2'900.0F }; // the player halfway along its walk
    constexpr core::Vec2 ELSEWHERE{ .x = 6'000.0F, .y = 0.0F };   // where the player is once the camera stops following
    constexpr f32 SCREEN = HEIGHT;                                // at zoom 1 a pixel is a world unit
    constexpr core::Vec2 PANNED{ .x = WALKED.x + (3.0F * SCREEN), .y = WALKED.y };
    constexpr f32 LABEL_ZOOM = 0.6F;
    struct Sample {
        const char* why = "";
        f32 seconds = 0.0F;
        core::Vec2 followed{};
        core::Vec2 centre{};
        f32 zoom = 1.0F;
        bool free = false;
    };
    const std::array samples{
        Sample{
            .why = "follow: its start", .seconds = 0.0F, .followed = PLAYER, .centre = PLAYER, .zoom = 1.0F, .free = false },
        Sample{
            .why = "follow: its middle", .seconds = 2.0F, .followed = MIDWAY, .centre = MIDWAY, .zoom = 1.0F, .free = false },
        Sample{ .why = "follow: its end", .seconds = 4.0F, .followed = WALKED, .centre = WALKED, .zoom = 1.0F, .free = false },
        Sample{ .why = "pan: a tenth in, from where the follow ended",
                .seconds = 4.4F,
                .followed = ELSEWHERE,
                .centre = WALKED + (RIGHT * (0.3F * SCREEN)),
                .zoom = 1.0F,
                .free = true },
        Sample{ .why = "pan: its middle, halfway across 3 screens",
                .seconds = 6.0F,
                .followed = ELSEWHERE,
                .centre = WALKED + (RIGHT * (1.5F * SCREEN)),
                .zoom = 1.0F,
                .free = true },
        Sample{ .why = "pan: its end, 3 screens across",
                .seconds = 8.0F,
                .followed = ELSEWHERE,
                .centre = PANNED,
                .zoom = 1.0F,
                .free = true },
        Sample{ .why = "zoom: a quarter in, zooming in",
                .seconds = 9.0F,
                .followed = ELSEWHERE,
                .centre = PANNED,
                .zoom = std::numbers::sqrt2_v<f32>,
                .free = true },
        Sample{ .why = "zoom: its middle, the closest",
                .seconds = 10.0F,
                .followed = ELSEWHERE,
                .centre = PANNED,
                .zoom = gfx::MAX_ZOOM,
                .free = true },
        Sample{ .why = "zoom: three quarters in, zooming out",
                .seconds = 11.0F,
                .followed = ELSEWHERE,
                .centre = PANNED,
                .zoom = std::numbers::sqrt2_v<f32> / 2.0F,
                .free = true },
        Sample{ .why = "zoom: its end, the farthest",
                .seconds = 12.0F,
                .followed = ELSEWHERE,
                .centre = PANNED,
                .zoom = gfx::MIN_ZOOM,
                .free = true },
        Sample{ .why = "widest view: its start, over the busiest region",
                .seconds = 12.5F,
                .followed = ELSEWHERE,
                .centre = game::WORLD_BUSIEST,
                .zoom = gfx::MIN_ZOOM,
                .free = true },
        Sample{ .why = "widest view: its middle",
                .seconds = 15.0F,
                .followed = ELSEWHERE,
                .centre = game::WORLD_BUSIEST,
                .zoom = gfx::MIN_ZOOM,
                .free = true },
        Sample{ .why = "widest view: its end",
                .seconds = 18.0F,
                .followed = ELSEWHERE,
                .centre = game::WORLD_BUSIEST,
                .zoom = gfx::MIN_ZOOM,
                .free = true },
        Sample{ .why = "labels: its start",
                .seconds = 18.5F,
                .followed = ELSEWHERE,
                .centre = game::WORLD_BUSIEST,
                .zoom = LABEL_ZOOM,
                .free = true },
        Sample{ .why = "labels: its middle",
                .seconds = 20.0F,
                .followed = ELSEWHERE,
                .centre = game::WORLD_BUSIEST,
                .zoom = LABEL_ZOOM,
                .free = true },
        Sample{ .why = "labels: its end, the path's last moment",
                .seconds = 22.0F,
                .followed = ELSEWHERE,
                .centre = game::WORLD_BUSIEST,
                .zoom = LABEL_ZOOM,
                .free = true },
    };
    app::CameraControl control{ 1.0F };
    for (const Sample& sample : samples) {
        CAPTURE(sample.why);
        app::CameraInput frame = Frame(CameraKind::Roam);
        frame.followed = sample.followed;
        frame.path = sample.seconds;
        frame.pan = RIGHT; // the player's pan: the path replaces it
        control.Update(frame);
        CheckAt(control, sample.centre);
        CHECK(control.Camera().zoom == doctest::Approx(sample.zoom));
        CHECK(control.Free() == sample.free);
    }
}

TEST_CASE("the camera path only zooms a combat scene, which keeps following") {
    constexpr f32 ZOOM_MIDDLE = 10.0F; // the zoom segment's middle: the closest zoom
    constexpr f32 WIDEST = 15.0F;      // the widest view, centred on the busiest region elsewhere
    app::CameraControl control{ 1.0F };
    app::CameraInput frame = Frame(CameraKind::Follow);
    frame.followed = PLAYER;
    frame.path = ZOOM_MIDDLE;
    control.Update(frame);
    CheckAt(control, PLAYER);
    CHECK(control.Camera().zoom == doctest::Approx(gfx::MAX_ZOOM));
    frame.followed = WALKED;
    frame.path = WIDEST;
    control.Update(frame);
    CheckAt(control, WALKED);
    CHECK(control.Camera().zoom == doctest::Approx(gfx::MIN_ZOOM));
    CHECK_FALSE(control.Free());
}

TEST_CASE("a still frame holds the camera path's pan and zoom, and still follows") {
    constexpr f32 FOLLOWING = 2.0F;
    constexpr f32 PANNING = 6.0F;
    constexpr f32 ZOOM_MIDDLE = 10.0F;
    constexpr core::Vec2 ELSEWHERE{ .x = 6'000.0F, .y = 0.0F };
    app::CameraControl control{ 1.0F };
    app::CameraInput frame = Frame(CameraKind::Roam);
    frame.followed = PLAYER;
    frame.path = 0.0F;
    control.Update(frame);
    frame.still = true;
    frame.followed = WALKED;
    frame.path = FOLLOWING;
    control.Update(frame);
    CheckAt(control, WALKED); // following goes on
    frame.followed = ELSEWHERE;
    frame.path = PANNING;
    control.Update(frame);
    CheckAt(control, WALKED); // no pan
    frame.path = ZOOM_MIDDLE;
    control.Update(frame);
    CHECK(control.Camera().zoom == 1.0F); // no zoom
    frame.still = false;
    control.Update(frame);
    CheckAt(control, WALKED); // the zoom segment holds where the camera was as it began
    CHECK(control.Camera().zoom == doctest::Approx(gfx::MAX_ZOOM));
}

TEST_CASE("a scene change during the camera path's pan pans on from the new scene's start") {
    constexpr f32 FOLLOW_END = 4.0F;
    constexpr f32 PAN_MIDDLE = 6.0F;
    constexpr f32 HALF_PAN = 1.5F * HEIGHT; // halfway across 3 screens, at zoom 1
    app::CameraControl control{ 1.0F };
    app::CameraInput frame = Frame(CameraKind::Roam);
    frame.followed = PLAYER;
    frame.path = FOLLOW_END;
    control.Update(frame);
    frame.path = PAN_MIDDLE;
    control.Update(frame);
    CheckAt(control, PLAYER + (RIGHT * HALF_PAN));
    frame.change = 2;
    frame.followed = WALKED;
    control.Update(frame);
    CheckAt(control, WALKED + (RIGHT * HALF_PAN));
}

} // namespace
