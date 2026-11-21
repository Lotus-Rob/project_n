#include "ground.hlsli"

ConstantBuffer<GroundPlace> place : register(b0, space1);

/// The ground's quad as a strip of four corners from the vertex index alone: no vertex buffer.
GroundVertexOut VertexMain(uint vertexId : SV_VertexID) {
    const float2 corner = float2(vertexId & 1, vertexId >> 1); // (0, 0), (1, 0), (0, 1), (1, 1)
    const float2 world = lerp(place.quad.xy, place.quad.zw, corner);
    GroundVertexOut output;
    output.position = float4(world * place.worldToClip.xy + place.worldToClip.zw, 0, 1);
    output.world = world;
    return output;
}
