#include "ground.hlsli"

#define FOG_TEXTURE_SLOT 0
#include "fog.hlsli"

/// A river's band (its wide edge or its core, gfx::DrawRivers()) in its colour, premultiplied over the ground, its sides
/// anti-aliased, then the fog's land treatment.
float4 FragmentMain(RiverVertexOut input) : SV_Target0 {
    // Both measured across the band (v), so each side fades in over one screen pixel whatever the zoom.
    const float fromSide = min(input.uv.y, 1.0 - input.uv.y);
    const float coverage = saturate(fromSide / max(fwidth(input.uv.y), 1e-6));
    const float4 colour = input.colour * coverage;
    return FoggedLand(colour.rgb, colour.a, input.world);
}
