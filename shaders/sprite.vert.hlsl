#include "sprite.hlsli"

struct Instance {
    float2 position;    // world units
    uint halfSize;      // two f16: half the width, then half the height
    uint uvRect;        // four unorm8: u, v, width and height in the atlas
};

struct Camera {
    float4x4 viewProjection;
};

StructuredBuffer<Instance> instances : register(t0, space0);
ConstantBuffer<Camera> camera : register(b0, space1);

VertexOut VertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
    const Instance instance = instances[instanceId];
    const float2 corner = float2(vertexId & 1, vertexId >> 1); // a strip of 4 vertices
    const float2 halfSize = float2(f16tof32(instance.halfSize), f16tof32(instance.halfSize >> 16));
    const float4 rect = float4(instance.uvRect & 0xFF, (instance.uvRect >> 8) & 0xFF,
                               (instance.uvRect >> 16) & 0xFF, instance.uvRect >> 24) / 255.0;
    VertexOut output;
    output.position = mul(camera.viewProjection, float4(instance.position + (corner * 2 - 1) * halfSize, 0, 1));
    output.uv = rect.xy + corner * rect.zw;
    return output;
}
