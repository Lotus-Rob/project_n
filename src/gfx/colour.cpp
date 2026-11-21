#include "gfx/colour.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/ui_colours.hpp"

namespace gfx {

namespace {

constexpr f32 BYTE_RANGE = 255.0F;
constexpr u32 CHANNEL_BITS = 8;
constexpr u32 CHANNEL_MASK = 0xFFU;

} // namespace

std::array<f32, 3> LinearOf(const std::array<u8, 3>& srgb) {
    const u32 packed = core::FromSrgb(srgb[0], srgb[1], srgb[2], core::OPAQUE_ALPHA);
    std::array<f32, 3> linear{};
    for (u32 channel = 0; channel < linear.size(); ++channel) {
        linear.at(channel) = static_cast<f32>((packed >> (channel * CHANNEL_BITS)) & CHANNEL_MASK) / BYTE_RANGE;
    }
    return linear;
}

} // namespace gfx
