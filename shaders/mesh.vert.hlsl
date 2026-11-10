#include "mesh.hlsli"

// gfx::MeshVertex (src/gfx/mesh_vertex.hpp), 16 bytes; the pipeline's vertex attributes unpack
// the UV and the colour.
struct VertexIn {
    float2 position : TEXCOORD0; // target pixels
    float2 uv : TEXCOORD1;       // unorm16 on the page
    float4 colour : TEXCOORD2;   // premultiplied RGBA8, red in the low byte
};

struct Draw {
    float4 pixelsToClip; // clip = pixels * xy + zw (gfx::ClipTransform)
};

ConstantBuffer<Draw> draw : register(b0, space1);

VertexOut VertexMain(VertexIn input) {
    VertexOut output;
    output.position = float4(input.position * draw.pixelsToClip.xy + draw.pixelsToClip.zw, 0, 1);
    output.uv = input.uv;
    output.colour = input.colour;
    return output;
}
