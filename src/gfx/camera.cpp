#include "gfx/camera.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace gfx {

ClipTransform WorldToClip(const Camera& camera, u32 width, u32 height) {
    const auto pixelsX = static_cast<f32>(std::max(width, 1U));
    const auto pixelsY = static_cast<f32>(std::max(height, 1U));
    const f32 pixelsPerUnit = std::min(pixelsX / REFERENCE_SIZE.x, pixelsY / REFERENCE_SIZE.y) * camera.zoom;
    // Clip space spans 2 across the target.
    const f32 scaleX = 2.0F * pixelsPerUnit / pixelsX;
    const f32 scaleY = -2.0F * pixelsPerUnit / pixelsY;
    return { .scaleX = scaleX, .scaleY = scaleY, .offsetX = -camera.centre.x * scaleX, .offsetY = -camera.centre.y * scaleY };
}

} // namespace gfx
