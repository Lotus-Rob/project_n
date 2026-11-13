#include "seal.hlsli"

// gfx::SealVertex (src/gfx/seal_draw.hpp), 24 bytes: a corner of a band's or a wash's quad, placed in the world on the CPU.
struct VertexIn {
    float2 position : TEXCOORD0; // world units
    float2 local : TEXCOORD1;    // on its seal's plane, in seal sizes
    uint layer : TEXCOORD2;      // its layer
    float grow : TEXCOORD3;      // 1 to grow it a pixel out from the centre, -1 towards it, 0 to keep it
};

// SealUniforms in src/gfx/internal/features/seals.cpp, 32 bytes.
struct Draw {
    float4 worldToClip;   // clip = world * xy + zw (gfx::ClipTransform)
    float2 pixelsPerClip; // the target's pixels per clip unit, across and down: half its size
    uint wash;            // 1 while the washes draw, 0 while the lines do
    uint padding;         // to a multiple of 16 bytes
};

// The layers again, here for their axes: a band's corner moves along its plane, and its place on it with it.
StructuredBuffer<SealLayer> layers : register(t0, space0);
ConstantBuffer<Draw> draw : register(b0, space1);

// How far a band's edge moves past where the CPU laid it, in screen pixels: its anti-aliased fringe stays inside the quad
// however small the seal is on screen.
static const float SEAL_GROW_PIXELS = 1.0;

SealOut VertexMain(VertexIn input) {
    float2 position = input.position;
    float2 local = input.local;
    const float radius = length(local);
    if (input.grow != 0.0 && radius > 0.0) {
        const SealLayer layer = layers[input.layer];
        const float2 outward = (local / radius) * input.grow; // on the plane, a seal size long
        const float2 world = (layer.axisX * outward.x) + (layer.axisDepth * outward.y);
        const float pixels = length(world * draw.worldToClip.xy * draw.pixelsPerClip);
        const float step = SEAL_GROW_PIXELS / max(pixels, 1e-6);
        position += world * step;
        local += outward * step;
    }
    SealOut output;
    output.position = float4(position * draw.worldToClip.xy + draw.worldToClip.zw, 0, 1);
    output.local = local;
    output.layer = input.layer;
    output.wash = draw.wash;
    return output;
}
