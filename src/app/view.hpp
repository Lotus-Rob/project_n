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

/// Notes that a snapshot of `scene` is drawn with `camera`, `shown` naming the scene drawn before (none at first),
/// which becomes `scene`: a scene other than `shown` starts at the reference view (gfx::Camera's defaults) at `zoom`
/// (the run's `--zoom`), the same one keeps the camera where the player panned and zoomed it.
void ShowScene(gfx::Camera& camera, std::optional<core::SceneId>& shown, core::SceneId scene, f32 zoom = 1.0F);

/// The followed entity's foot at the render clock: `follow` interpolated by `alpha`, as its sprite is drawn.
[[nodiscard]] core::Vec2 FollowedCentre(const core::Follow& follow, f64 alpha);

/// The world point `camera` shows at `position`, in the coordinates of a window `width` by `height`.
[[nodiscard]] core::Vec2 WindowToWorld(const gfx::Camera& camera, core::Vec2 position, i32 width, i32 height);

/// The index of the instance drawn on top at `world` in `snapshot` interpolated by `alpha`: the last
/// in drawing order with an ID whose quad, turned by its interpolated rotation, contains the point, or
/// none. Transparent pixels count as hits, until sprites with much empty space overlap
/// enough to matter.
[[nodiscard]] std::optional<std::size_t> PickInstance(const core::Snapshot& snapshot, f64 alpha, core::Vec2 world);

/// The entity of PickInstance()'s instance, or zero for none.
[[nodiscard]] core::StableId Pick(const core::Snapshot& snapshot, f64 alpha, core::Vec2 world);

} // namespace app
