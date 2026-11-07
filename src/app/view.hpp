#pragma once

/// @file
/// @ingroup app
/// The main thread's view of a drawn snapshot (design doc 0002 §8.10, §10): where the camera
/// centres on the followed entity, which world point a window position shows, and which entity
/// is drawn there. Pure: tested with hand-built snapshots.

#include "core/frame.hpp"
#include "core/stable_id.hpp"
#include "core/vec2.hpp"
#include "gfx/camera.hpp"

namespace app {

/// The followed entity's foot at the render clock: `follow` interpolated by `alpha`, as its sprite is drawn.
[[nodiscard]] core::Vec2 FollowedCentre(const core::Follow& follow, f64 alpha);

/// The world point `camera` shows at `position`, in the coordinates of a window `width` by `height`.
[[nodiscard]] core::Vec2 WindowToWorld(const gfx::Camera& camera, core::Vec2 position, i32 width, i32 height);

/// The entity drawn on top at `world` in `snapshot` interpolated by `alpha`: the last instance in
/// drawing order with an ID whose quad contains the point, or zero for none. shortcut: the quad's
/// axis-aligned box, ignoring rotation and counting transparent pixels as hits, until something
/// small and rotated needs better.
[[nodiscard]] core::StableId Pick(const core::Snapshot& snapshot, f64 alpha, core::Vec2 world);

} // namespace app
