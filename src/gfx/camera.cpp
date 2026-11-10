#include "gfx/camera.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"

namespace gfx {

f32 ZoomedBy(f32 zoom, f32 notches) {
    PN_PROFILE_ZONE();
    constexpr f32 ZOOM_PER_NOTCH = 1.15F;
    return std::clamp(zoom * std::pow(ZOOM_PER_NOTCH, notches), MIN_ZOOM, MAX_ZOOM);
}

f32 PixelsPerUnit(const Camera& camera, u32 width, u32 height) {
    PN_PROFILE_ZONE();
    const auto pixelsX = static_cast<f32>(std::max(width, 1U));
    const auto pixelsY = static_cast<f32>(std::max(height, 1U));
    return std::min(pixelsX / REFERENCE_SIZE.x, pixelsY / REFERENCE_SIZE.y) * camera.zoom;
}

ClipTransform WorldToClip(const Camera& camera, u32 width, u32 height) {
    PN_PROFILE_ZONE();
    const auto pixelsX = static_cast<f32>(std::max(width, 1U));
    const auto pixelsY = static_cast<f32>(std::max(height, 1U));
    const f32 pixelsPerUnit = PixelsPerUnit(camera, width, height);
    // Clip space spans 2 across the target.
    const f32 scaleX = 2.0F * pixelsPerUnit / pixelsX;
    const f32 scaleY = -2.0F * pixelsPerUnit / pixelsY;
    return { .scaleX = scaleX, .scaleY = scaleY, .offsetX = -camera.centre.x * scaleX, .offsetY = -camera.centre.y * scaleY };
}

} // namespace gfx
