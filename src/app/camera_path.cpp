#include "app/camera_path.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "game/scenes/world.hpp"
#include "gfx/camera.hpp"

namespace app {

namespace {

constexpr std::array CAMERA_SEGMENTS{
    CameraSegment{ .name = "follow", .seconds = 4.0F, .walks = true },
    CameraSegment{ .name = "pan", .seconds = 4.0F, .centre = PathCentre::Pans, .pan = { .x = 3.0F, .y = 0.0F } },
    CameraSegment{ .name = "zoom in and out",
                   .seconds = 4.0F,
                   .centre = PathCentre::Pans,
                   .zoomMiddle = gfx::MAX_ZOOM,
                   .zoomEnd = gfx::MIN_ZOOM },
    CameraSegment{ .name = "widest view",
                   .seconds = 6.0F,
                   .centre = PathCentre::At,
                   .point = game::WORLD_BUSIEST,
                   .zoomStart = gfx::MIN_ZOOM,
                   .zoomMiddle = gfx::MIN_ZOOM,
                   .zoomEnd = gfx::MIN_ZOOM },
    CameraSegment{ .name = "labels",
                   .seconds = 4.0F,
                   .centre = PathCentre::At,
                   .point = game::WORLD_BUSIEST,
                   .zoomStart = 0.6F,
                   .zoomMiddle = 0.6F,
                   .zoomEnd = 0.6F },
};

} // namespace

std::span<const CameraSegment> CameraPath() {
    return CAMERA_SEGMENTS;
}

std::optional<std::size_t> CameraPathSegment(f32 seconds) {
    std::size_t index = 0;
    f32 end = 0.0F;
    for (const CameraSegment& segment : CameraPath()) {
        end += segment.seconds;
        if (seconds <= end) {
            return index;
        }
        ++index;
    }
    return std::nullopt;
}

f32 SegmentThrough(std::size_t index, f32 seconds) {
    const std::span<const CameraSegment> path = CameraPath();
    f32 start = 0.0F;
    for (const CameraSegment& segment : path.first(index)) {
        start += segment.seconds;
    }
    return std::clamp((seconds - start) / path[index].seconds, 0.0F, 1.0F);
}

f32 SegmentZoom(const CameraSegment& segment, f32 through) {
    // An even rate in scale, so a zoom in or out looks as fast at every zoom.
    const bool firstHalf = through < 0.5F;
    const f32 from = firstHalf ? segment.zoomStart : segment.zoomMiddle;
    const f32 until = firstHalf ? segment.zoomMiddle : segment.zoomEnd;
    const f32 along = firstHalf ? through * 2.0F : (through * 2.0F) - 1.0F;
    return std::clamp(from * std::pow(until / from, along), gfx::MIN_ZOOM, gfx::MAX_ZOOM);
}

bool CameraPathOver(f32 seconds) {
    return !CameraPathSegment(seconds).has_value();
}

core::Vec2 CameraPathMove(f32 seconds) {
    const std::optional<std::size_t> index = CameraPathSegment(seconds);
    return index && CameraPath()[*index].walks ? core::Vec2{ .x = 1.0F, .y = 0.0F } : core::Vec2{};
}

} // namespace app
