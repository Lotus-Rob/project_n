#pragma once

/// @file
/// @ingroup app
/// The scripted camera path `--camera-path` drives the camera along for the measurements: its
/// segments, which one is under way at a time, and the Move it presses. CameraControl moves the camera by
/// it (CameraInput::path). Pure: tested with literal times.

#include "core/tick_clock.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace app {

/// Where the camera's centre goes over a segment of the path (CameraPath()).
enum class PathCentre : u8 {
    Follows, ///< On what the scene follows.
    Pans,    ///< From where the segment starts, by CameraSegment::pan over the segment.
    At,      ///< On CameraSegment::point.
};

/// One segment of the path (CameraPath()): where the camera goes over it, and how long it takes.
struct CameraSegment {
    /// What the log and the profiler call it (`camera path: <name>`).
    std::string_view name;
    /// How long it lasts, in seconds.
    f32 seconds = 0.0F;
    /// Where the camera's centre goes.
    PathCentre centre = PathCentre::Follows;
    /// For PathCentre::Pans, how far the centre moves over the segment, at an even speed, in screens (the world the
    /// window's shorter side shows at the camera's zoom); zero holds it where the segment starts.
    core::Vec2 pan{};
    /// For PathCentre::At, the world point the camera centres on.
    core::Vec2 point{};
    /// The zoom at the segment's start; it moves at an even rate in scale to zoomMiddle halfway, then on to zoomEnd.
    f32 zoomStart = 1.0F;
    /// The zoom halfway through the segment.
    f32 zoomMiddle = 1.0F;
    /// The zoom at the segment's end.
    f32 zoomEnd = 1.0F;
    /// Whether the cultivator walks east (Move held to the right) through the segment.
    bool walks = false;
};

/// How long after the scene runs (the first frame drawn at core::SceneStage::Running) the path starts, so the run's
/// start-up and the scene's first frames stay out of what it measures.
inline constexpr u64 CAMERA_PATH_DELAY_NS = 2 * core::NS_PER_SECOND;

/// The path's segments, in order, made for the world map: `follow` (4 s, the cultivator walking east), `pan` (4 s, 3
/// screens on east), `zoom in and out` (4 s, in to the closest zoom halfway, then out to the farthest), `widest view`
/// (6 s at the farthest zoom over the busiest region, game::WORLD_BUSIEST) and `labels` (4 s at zoom 0.6 there, where
/// labels show); the run quits after them. The pan (0.75 screens a second) and the zoom (at most 8 times in 2 seconds,
/// under 1.11 times in a tenth of a second) stay within what CULL_MARGIN covers. The cut to the busiest region (about 9
/// screens) and the step in to the labels' zoom do not: the first frames after the cut show ground no publish covered
/// yet, until one culled to the new view catches up, as after a snap back from far.
[[nodiscard]] std::span<const CameraSegment> CameraPath();

/// The segment of the path under way `seconds` into it, by its index in CameraPath(): each runs from just after the
/// previous one's end to its own end, included (the first from 0, or before); none after the last one's end.
[[nodiscard]] std::optional<std::size_t> CameraPathSegment(f32 seconds);

/// How far through its `index`th segment the path is `seconds` into it, from 0 at the segment's start to 1 at its end.
[[nodiscard]] f32 SegmentThrough(std::size_t index, f32 seconds);

/// The zoom `through` (0 to 1) the way along `segment`.
[[nodiscard]] f32 SegmentZoom(const CameraSegment& segment, f32 through);

/// Whether the path is over `seconds` into it, past its last segment's end: the run quits.
[[nodiscard]] bool CameraPathOver(f32 seconds);

/// Where the path's scripted Move points `seconds` into it: east through a segment that walks (CameraSegment::walks),
/// nowhere elsewhere. The logic sees it as it sees the player's Move.
[[nodiscard]] core::Vec2 CameraPathMove(f32 seconds);

} // namespace app
