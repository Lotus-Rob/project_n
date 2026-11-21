#include "ground.hlsli"

/// gfx::RibbonVertex (src/gfx/ribbon.hpp), 16 bytes; the pipeline's vertex attributes unpack the UV and the colour.
struct VertexIn {
    float2 position : TEXCOORD0; // world units
    float2 uv : TEXCOORD1;       // unorm16: along, then across
    float4 colour : TEXCOORD2;   // premultiplied RGBA8, red in the low byte
};

struct Draw {
    float4 worldToClip; // clip = world * xy + zw (gfx::ClipTransform)
};

ConstantBuffer<Draw> draw : register(b0, space1);

RiverVertexOut VertexMain(VertexIn input) {
    RiverVertexOut output;
    output.position = float4(input.position * draw.worldToClip.xy + draw.worldToClip.zw, 0, 1);
    output.uv = input.uv;
    output.colour = input.colour;
    output.world = input.position;
    return output;
}
