#include "composite.hlsli"

// One triangle that covers the whole target, from the vertex index alone: no vertex buffer.
VertexOut VertexMain(uint vertexId : SV_VertexID) {
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2); // (0, 0), (2, 0), (0, 2)
    VertexOut output;
    output.position = float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);
    output.uv = corner;
    return output;
}
