#include "ground.hlsli"

#define FOG_TEXTURE_SLOT 0
#include "fog.hlsli"

/// The band being drawn: where it starts fading toward its sides, as a share of its half width from its middle (gfx's
/// RimStart() for an edge band, where the core ends; 1 for a core, solid to its sides). RiverBand in
/// src/gfx/internal/features/rivers.cpp.
struct RiverBand {
    float rimStart;
    float3 padding;
};
ConstantBuffer<RiverBand> band : register(b1, space3);

/// A river's band (its wide edge or its core, gfx::DrawRivers()) in its colour, premultiplied over the ground: an edge
/// band's rim fading softly from beside the core to nothing at its side, every side anti-aliased, then the fog's land
/// treatment.
float4 FragmentMain(RiverVertexOut input) : SV_Target0 {
    // Measured across the band (v), so each side fades in over one screen pixel whatever the zoom.
    const float fromSide = min(input.uv.y, 1.0 - input.uv.y);
    const float coverage = saturate(fromSide / max(fwidth(input.uv.y), 1e-6));
    const float fromMiddle = abs(input.uv.y * 2.0 - 1.0);
    const float fading = saturate((fromMiddle - band.rimStart) / max(1.0 - band.rimStart, 1e-6));
    const float rim = 1.0 - fading * fading * (3.0 - 2.0 * fading);
    const float4 colour = input.colour * (coverage * rim);
    return FoggedLand(colour.rgb, colour.a, input.world);
}
