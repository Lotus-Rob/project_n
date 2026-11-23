#include "gfx/ground.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"
#include "core/ui_colours.hpp"
#include "gfx/colour.hpp"

namespace gfx {

namespace {

constexpr f32 ALPHA_RANGE = 255.0F;

/// `colour` premultiplied by `strength`, held between 0 and 1.
u32 StrengthColour(const std::array<u8, 3>& colour, f32 strength) {
    const auto alpha = static_cast<u8>(std::lround(std::clamp(strength, 0.0F, 1.0F) * ALPHA_RANGE));
    return core::FromSrgb(colour[0], colour[1], colour[2], alpha);
}

/// `linear` and `strength` as one row of four.
std::array<f32, 4> RowOf(const std::array<f32, 3>& linear, f32 strength) {
    return { { linear[0], linear[1], linear[2], strength } };
}

} // namespace

GroundUniforms GroundUniformsOf(const core::WorldMap& map, const core::WorldLook& look, u32 mode) {
    PN_PROFILE_ZONE();
    const core::GroundLook& ground = look.ground;
    GroundUniforms uniforms{ .mapSize = { { static_cast<f32>(map.Columns()), static_cast<f32>(map.Rows()) } },
                             .tileSize = map.TileSize(),
                             .mode = mode,
                             .paper = LinearOf(ground.paper),
                             .grain = ground.grain,
                             .edgeWarp = ground.edgeWarp,
                             .edgeSoft = ground.edgeSoft,
                             .blotch = ground.blotch,
                             .depthTiles = ground.depthTiles,
                             .shore = LinearOf(ground.shore),
                             .shoreStrength = ground.shoreStrength,
                             .grid = LinearOf(ground.grid),
                             .gridStrength = ground.gridStrength,
                             .veil = LinearOf(look.veil.colour),
                             .veilStrength = look.veil.strength,
                             .shallowKind = ground.shallowKind,
                             .deepKind = ground.deepKind,
                             .shoreWidth = ground.shoreWidth,
                             .veilScale = look.veil.scale,
                             .gridFade = ground.gridFade };
    const std::size_t kinds = std::min<std::size_t>(look.kinds.size(), core::MAX_GROUND_KINDS);
    for (std::size_t kind = 0; kind < kinds; ++kind) {
        const core::KindLook& kindLook = look.kinds[kind];
        uniforms.washes.at(kind) = RowOf(LinearOf(kindLook.wash), kindLook.strength);
    }
    return uniforms;
}

std::optional<core::Rect> GroundQuad(core::Rect view, const core::WorldMap& map) {
    PN_PROFILE_ZONE();
    const core::Rect overlap{ .min = { .x = std::max(view.min.x, 0.0F), .y = std::max(view.min.y, 0.0F) },
                              .max = { .x = std::min(view.max.x, static_cast<f32>(map.Columns()) * map.TileSize()),
                                       .y = std::min(view.max.y, static_cast<f32>(map.Rows()) * map.TileSize()) } };
    if (overlap.min.x >= overlap.max.x || overlap.min.y >= overlap.max.y) {
        return std::nullopt;
    }
    return overlap;
}

RibbonShape RibbonShapeOf(const core::WorldMap& map, const core::RibbonLook& look) {
    return { .coreWidth = look.width * map.TileSize(),
             .edgeWidth = look.edge * map.TileSize(),
             .coreColour = core::FromSrgb(look.colour[0], look.colour[1], look.colour[2], core::OPAQUE_ALPHA),
             .edgeColour = StrengthColour(look.edgeColour, look.edgeStrength) };
}

f32 RimStart(f32 coreWidth, f32 edgeWidth) {
    if (edgeWidth <= 0.0F) {
        return 1.0F;
    }
    return std::clamp(coreWidth / edgeWidth, 0.0F, 1.0F);
}

} // namespace gfx
