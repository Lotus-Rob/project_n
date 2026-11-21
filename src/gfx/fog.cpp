#include "gfx/fog.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "gfx/colour.hpp"

namespace gfx {

namespace {

constexpr f32 BYTE_RANGE = 255.0F;
constexpr f32 SIGHT_EDGE = 0.5F;
constexpr f32 SHADE_EDGE = 0.5F; // fog.hlsli's UnexploredShade(): no shade from half explored on
constexpr f32 CENTRE = 0.5F;
constexpr f32 SMOOTH_A = 3.0F; // smoothstep: t * t * (3 - 2t)
constexpr f32 SMOOTH_B = 2.0F;
/// fog.hlsli's LUMINANCE: Rec. 709's weights of linear red, green and blue.
constexpr std::array<f32, 3> FOG_LUMINANCE{ { 0.2126F, 0.7152F, 0.0722F } };

/// `along`, from 0 to 1, eased so both ends are flat (smoothstep), as the shaders ease their fades.
f32 Smoothstep(f32 along) {
    return along * along * (SMOOTH_A - (SMOOTH_B * along));
}

f32 Texel(const FogFrame& fog, u32 column, u32 row, u32 channel) {
    const std::size_t index = (((std::size_t{ row } * fog.columns) + column) * 2) + channel;
    return index < fog.values.size() ? static_cast<f32>(fog.values[index]) / BYTE_RANGE : 1.0F;
}

} // namespace

f32 FogSample(const FogFrame& fog, core::Vec2 world, u32 channel) {
    if (fog.columns == 0 || fog.rows == 0 || fog.tileSize <= 0.0F) {
        return 1.0F;
    }
    // Texel centres sit half a tile in; clamping the coordinate is clamp-to-edge.
    const f32 across = std::clamp((world.x / fog.tileSize) - CENTRE, 0.0F, static_cast<f32>(fog.columns - 1));
    const f32 down = std::clamp((world.y / fog.tileSize) - CENTRE, 0.0F, static_cast<f32>(fog.rows - 1));
    const f32 left = std::floor(across);
    const f32 top = std::floor(down);
    const f32 weightRight = across - left;
    const f32 weightBelow = down - top;
    const auto column = static_cast<u32>(left);
    const auto row = static_cast<u32>(top);
    const u32 right = std::min(column + 1, fog.columns - 1);
    const u32 below = std::min(row + 1, fog.rows - 1);
    const f32 upper = std::lerp(Texel(fog, column, row, channel), Texel(fog, right, row, channel), weightRight);
    const f32 lower = std::lerp(Texel(fog, column, below, channel), Texel(fog, right, below, channel), weightRight);
    return std::lerp(upper, lower, weightBelow);
}

f32 SightFade(const FogFrame& fog, core::Vec2 world) {
    const f32 sample = FogSample(fog, world, FOG_IN_SIGHT);
    return Smoothstep(std::clamp((sample - SIGHT_EDGE) / (1.0F - SIGHT_EDGE), 0.0F, 1.0F));
}

f32 ExploredFade(const FogFrame& fog, core::Vec2 world) {
    return Smoothstep(std::clamp(FogSample(fog, world, FOG_EXPLORED) / SHADE_EDGE, 0.0F, 1.0F));
}

f32 TileExplored(const FogFrame& fog, core::Vec2 world) {
    if (fog.columns == 0 || fog.rows == 0 || fog.tileSize <= 0.0F) {
        return 1.0F;
    }
    const f32 across = std::floor(world.x / fog.tileSize);
    const f32 down = std::floor(world.y / fog.tileSize);
    if (across < 0.0F || down < 0.0F || across >= static_cast<f32>(fog.columns) || down >= static_cast<f32>(fog.rows)) {
        return 1.0F;
    }
    return Texel(fog, static_cast<u32>(across), static_cast<u32>(down), FOG_EXPLORED);
}

FogUniforms FogUniformsOf(const FogFrame& fog, const core::FogLook& look) {
    if (fog.columns == 0 || fog.rows == 0 || fog.tileSize <= 0.0F) {
        return {};
    }
    const std::array<f32, 3> sepia = LinearOf(look.sepia);
    const f32 luminance = (sepia[0] * FOG_LUMINANCE[0]) + (sepia[1] * FOG_LUMINANCE[1]) + (sepia[2] * FOG_LUMINANCE[2]);
    // Over its own luminance, a colour's luminance times the tint keeps that luminance in the sepia's hue.
    const f32 over = luminance > 0.0F ? 1.0F / luminance : 0.0F;
    const std::array<f32, 3> mist = LinearOf(look.mist);
    const std::array<f32, 3> shade = LinearOf(look.shade);
    return { .mapSize = { static_cast<f32>(fog.columns), static_cast<f32>(fog.rows) },
             .tileSize = fog.tileSize,
             .on = 1,
             .sepia = { sepia[0] * over, sepia[1] * over, sepia[2] * over, look.drain },
             .mist = { mist[0], mist[1], mist[2], look.mistStrength },
             .shade = { shade[0], shade[1], shade[2], 0.0F } };
}

} // namespace gfx
