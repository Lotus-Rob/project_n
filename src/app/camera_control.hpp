#pragma once

/// @file
/// @ingroup app
/// The camera's control (the phase 6 chunk 5a spec, Goal 2): how the camera follows, pans, is dragged, zooms and snaps
/// back over each kind of scene (core::CameraKind), frame by frame, and how the scripted path (`app/camera_path.hpp`)
/// moves it for the measurements (Goal 4). Pure: tested with hand-built frames.

#include "app/bindings.hpp"
#include "core/frame.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "gfx/camera.hpp"

#include <cstddef>
#include <optional>

namespace app {

/// How fast the camera pans at full movement (the pan keys, the right stick, the screen's edge, and Move in a still
/// scene), in screens a second: a screen is the world the window's shorter side shows, so a pan feels the same at every
/// zoom and in every window. CULL_MARGIN is argued from it.
inline constexpr f32 PAN_SCREENS_PER_SECOND = 1.0F;

/// How close to the window's edge the cursor pans the camera, as a fraction of the window's shorter side: about 22
/// pixels in a 1080-pixel window. A cursor arriving over the window (from elsewhere, or with the focus regained) pans
/// nothing until it has been seen inside clear of the band, so crossing the edge on the way in never pans.
inline constexpr f32 EDGE_BAND = 0.02F;

/// What the camera is told each drawn frame.
struct CameraInput {
    /// How the camera moves over the scene drawn (core::Snapshot::camera).
    core::CameraKind kind = core::CameraKind::Pan;
    /// The scene change that entered the scene drawn (core::ShownChange()): a new one starts the camera afresh.
    u64 change = 1;
    /// What the scene follows, at the render clock (FollowedCentre()); none when it follows nothing.
    std::optional<core::Vec2> followed = std::nullopt;
    /// Where the movement controls point (Bindings::Move()): they pan a still scene only.
    core::Vec2 move{};
    /// Where the camera pan controls point (Bindings::Pan()).
    core::Vec2 pan{};
    /// Where the mouse cursor is, in window coordinates; none while it is elsewhere (or a gamepad is in use).
    std::optional<core::Vec2> cursor = std::nullopt;
    /// Where the pointer was when this frame's drag movement started (Drag::Take()); none when not dragging.
    std::optional<core::Vec2> dragFrom = std::nullopt;
    /// Whether the window has the focus: without it the cursor neither drags nor pans at the edge.
    bool focused = true;
    /// Whether the cursor pans at the window's edge (core::Settings::edgePan).
    bool edgePan = true;
    /// The window's width, in the cursor's coordinates.
    i32 width = 0;
    /// Its height, likewise.
    i32 height = 0;
    /// The time since the last frame, in seconds; panning counts at most FRAME_STEP_LIMIT_NS of it.
    f32 seconds = 0.0F;
    /// Whether the player's camera inputs hold still this frame (a calendar step's transition shows: HoldsCamera()):
    /// no pan, drag, edge pan, snap back or zoom moves it, so it never shows past what was published; it still follows.
    /// A snap back or wheel notches given meanwhile wait for the first frame that isn't still.
    bool still = false;
    /// How far into the scripted path (CameraPath()) the run is, in seconds (`--camera-path`); none while no path drives the
    /// camera. While one does, it moves the camera in place of the player's pan, drag, edge pan, snap back and zoom (dropped,
    /// not kept for later); a Follow scene's camera still only follows, at the path's zoom, and a still frame holds it as it
    /// holds the player's inputs.
    std::optional<f32> path = std::nullopt;
};

/// Whether a press of `control` starts dragging the camera: the middle button always (a scene that follows ignores the
/// drag), the right button outside a combat scene (`combat` false), where it casts nothing.
[[nodiscard]] bool DragsCamera(Control control, bool combat);

/// How far the pointer may stray, in window pixels, while the middle button is down for its click to count as a click,
/// not a drag: a hand's tremor, not a movement.
inline constexpr f32 CLICK_SLOP_PIXELS = 4.0F;

/// A drag of the camera by a pointer button: where each frame's drag movement starts.
class Drag {
public:
    /// `button` went down with the pointer at `where`: a drag starts there, unless one is under way.
    void Press(Control button, core::Vec2 where);

    /// `button` went up at `where`: the drag it started ends. Returns whether it was a click of the middle button
    /// (Control::DragCamera) that dragged nothing, never straying more than CLICK_SLOP_PIXELS from where it went down
    /// nor ended by the window: snap back's mouse binding.
    [[nodiscard]] bool Release(Control button, core::Vec2 where);

    /// Ends any drag: the window lost the focus or was minimised, where a button's release may never reach it.
    void End();

    /// Where this frame's drag movement started (CameraInput::dragFrom), none when not dragging; the next frame's starts
    /// where `cursor` is now. The drag ends, and stays ended until the next press, when the window lost the focus
    /// (`focused` false) or the pointer left the window (`cursor` none or outside `width` by `height`).
    [[nodiscard]] std::optional<core::Vec2> Take(std::optional<core::Vec2> cursor, bool focused, i32 width, i32 height);

private:
    std::optional<core::Vec2> m_from = std::nullopt; // where the next frame's movement starts, while dragging
    Control m_button = Control::DragCamera;          // what started it
    core::Vec2 m_pressedAt{};                        // where it started
    bool m_strayed = false;                          // whether the pointer went past CLICK_SLOP_PIXELS since
};

/// The camera over each kind of scene: a Follow scene centres on what it follows and nothing else moves it; a Pan
/// scene pans by Move, the pan controls, the screen's edge and a drag, and snap back returns it to the scene's start;
/// a Roam scene follows until any of those but Move pans it, then stays where the player leaves it until snap back
/// follows again. The wheel zooms every kind, one notch a frame, within gfx::MIN_ZOOM and gfx::MAX_ZOOM. While
/// CameraInput::path is set, the scripted path (CameraPath()) moves it instead.
class CameraControl {
public:
    /// A camera whose every scene starts at `startZoom` (the run's `--zoom`).
    explicit CameraControl(f32 startZoom);

    /// Moves the camera on by one frame's `input`. A new scene change starts it afresh, following, at the start zoom,
    /// centred on what the scene follows or, with nothing followed, on gfx::REFERENCE_CENTRE.
    void Update(const CameraInput& input);

    /// What the window shows.
    [[nodiscard]] const gfx::Camera& Camera() const {
        return m_camera;
    }

    /// Whether the camera stays where the player panned it instead of following: a Roam scene's, from a pan until snap
    /// back or the next scene change.
    [[nodiscard]] bool Free() const {
        return m_free;
    }

    /// Zooms by `notches` of the wheel (gfx::ZoomedBy()), at most one a frame from the next Update(): the rest wait for
    /// the frames after, so a view never grows by more than a notch between two frames. Notches past a zoom limit are
    /// dropped.
    void Zoom(f32 notches);

    /// Snaps the camera back (a Roam scene's to what it follows, a Pan scene's to where it started; a Follow scene's
    /// never left) at the next Update() that isn't still. A scene change in between drops it: the scene starts afresh.
    void SnapBack();

    /// The window lost the focus or was minimised, maybe while no frame was drawn: edge pan waits, as for a cursor
    /// arriving (EDGE_BAND), until the cursor has been seen inside the window clear of the band.
    void LoseFocus();

private:
    // Moves the camera as the scripted path says `seconds` (CameraInput::path) in.
    void FollowPath(const CameraInput& input, f32 seconds);

    f32 m_startZoom;
    gfx::Camera m_camera;
    core::Vec2 m_start{};                       // where the scene's camera started: a Pan scene's snap back
    std::optional<u64> m_change = std::nullopt; // the scene change the camera shows
    f32 m_notches = 0.0F;                       // waiting to zoom
    bool m_snapBack = false;                    // waiting to snap back
    bool m_edgeArmed = false;                   // the cursor was seen inside clear of the edge band since it arrived
    bool m_free = false;
    std::optional<std::size_t> m_segment = std::nullopt; // the path segment the camera was last moved in
    core::Vec2 m_segmentFrom{};                          // where the camera was as that segment started
};

} // namespace app
