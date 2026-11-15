#pragma once

/// @file
/// @ingroup app
/// The main thread's view of a drawn snapshot (design doc 0002 §8.10, §10): where the camera
/// centres on the followed entity, which world point a window position shows, and which entity
/// is drawn there. Pure: tested with hand-built snapshots.

#include "core/frame.hpp"
#include "core/stable_id.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "gfx/camera.hpp"

#include <cstddef>
#include <optional>

namespace app {

/// The most the frame clock moves on from one frame to the next, in nanoseconds: a tenth of a second.
inline constexpr u64 FRAME_STEP_LIMIT_NS = core::NS_PER_SECOND / 10;

/// How far past the window's view the logic publishes, on each side, as a fraction of the view's size on that axis
/// (CullArea()). A snapshot culled to a view is drawn while the camera moves on, until a fresh view has culled a newer
/// one: about 43 ms at most at 60 Hz (a publish interval for the view to reach the logic, core::DEFAULT_DISPLAY_DELAY_NS,
/// and another interval while that snapshot shows), or one frame when frames run longer, FRAME_STEP_LIMIT_NS at most.
/// Zoom runs at most ZOOM_NOTCHES_PER_SECOND and one notch a frame (gfx::ZoomedBy(): a notch out shows 1.15 times as
/// much world), so 1.3 notches in 43 ms, or one in a long frame, grow each side by at most 0.10 of the view; a pan at
/// full speed at the new zoom moves at most 0.12 of it on either axis (PAN_SCREENS_PER_SECOND of the shorter side's
/// world, 1.2 times as much, for a tenth of a second; CameraControl counts no more of a longer frame). A quarter covers
/// both at once, 0.22, so nothing pops in at the window's edge; wider would publish more for no picture. The bound it
/// guarantees: the camera may move about a quarter of the view while a snapshot shows and show nothing missing. A flick of a
/// drag (which pans 1:1 with the cursor) faster than that, or a snap back from far away (a jump to the player), can show the
/// edge for a frame or two until the next view culls a fresh publish: a known limit, which phase 6 chunk 5a's measurements own;
/// so can a window that grows by more than the margin at once (maximised, or made fullscreen), at its new edges. While a
/// calendar step's transition shows, no fresh publish comes, so the camera holds still (HoldsCamera(), CameraInput::still).
inline constexpr f32 CULL_MARGIN = 0.25F;

/// How many wheel notches a second the camera zooms by at most, however short the frames (CameraControl::Zoom()): one
/// every two publish intervals, which keeps CULL_MARGIN's bound at any frame rate. A flick of three notches takes a tenth
/// of a second.
inline constexpr f32 ZOOM_NOTCHES_PER_SECOND = static_cast<f32>(core::TICKS_PER_SECOND) / 2.0F;

/// The main thread's frame clock (gfx::View::frameClock): seconds of wall time from its first frame, which runs on
/// while the game is paused, unlike the render clock. A gap longer than FRAME_STEP_LIMIT_NS between frames (a hidden
/// window, a stall) moves it on by that limit only: the first frame after it moves a label's fade by two thirds of
/// gfx::LABEL_FADE_SECONDS at most, so a fade under way still shows its last third rather than being skipped whole.
class FrameClock {
public:
    /// Moves the clock on to wall time `nowNs` (platform::NowNs()) and gives its time in seconds: 0 on the first call,
    /// then the time since the last call added, at most FRAME_STEP_LIMIT_NS of it.
    f64 Advance(u64 nowNs);

private:
    std::optional<u64> m_lastNs = std::nullopt;
    u64 m_elapsedNs = 0;
};

/// How a frame draws a snapshot: `camera`, the render clock's `alpha` between the snapshot's ticks, the hovered
/// instance `outlined`, and the frame clock's `frameSeconds` (FrameClock::Advance()).
[[nodiscard]] gfx::View FrameView(const gfx::Camera& camera, f64 alpha, std::optional<std::size_t> outlined, f64 frameSeconds);

/// The followed entity's foot at the render clock: `follow` interpolated by `alpha`, as its sprite is drawn.
[[nodiscard]] core::Vec2 FollowedCentre(const core::Follow& follow, f64 alpha);

/// The world point `camera` shows at `position`, in the coordinates of a window `width` by `height`.
[[nodiscard]] core::Vec2 WindowToWorld(const gfx::Camera& camera, core::Vec2 position, i32 width, i32 height);

/// The world `camera` shows in a window `width` by `height`, grown on each side by CULL_MARGIN of its size: what the
/// logic culls a publish to (core::ViewReport::area).
[[nodiscard]] core::Rect CullArea(const gfx::Camera& camera, i32 width, i32 height);

/// Whether the camera holds still against the player's pan, drag, edge pan, snap back and zoom over `drawn`
/// (CameraInput::still): before the first snapshot (null), and while a calendar step's transition shows
/// (core::Snapshot::transition), when the logic publishes nothing new and the snapshot drawn stays culled to the view
/// it was sent, so panning past CULL_MARGIN would show its edge. Which scenes pan at all is the scene's camera kind's
/// (CameraControl).
[[nodiscard]] bool HoldsCamera(const core::Snapshot* drawn);

/// The index of the instance drawn on top at `world` in `snapshot` interpolated by `alpha`: the last
/// in drawing order with an ID whose quad, turned by its interpolated rotation, contains the point, or
/// none. Transparent pixels count as hits, until sprites with much empty space overlap
/// enough to matter.
[[nodiscard]] std::optional<std::size_t> PickInstance(const core::Snapshot& snapshot, f64 alpha, core::Vec2 world);

/// The entity of PickInstance()'s instance, or zero for none.
[[nodiscard]] core::StableId Pick(const core::Snapshot& snapshot, f64 alpha, core::Vec2 world);

} // namespace app
