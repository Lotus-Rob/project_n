#include "sprite.hlsli"

// core::Instance (src/core/frame.hpp), 40 bytes. 16-bit fields come in pairs, low half first, and
// are unpacked by hand: SDL_GPU's baseline doesn't guarantee native 16-bit types (§8.5).
struct Instance {
    float2 previous;  // the quad's centre at the previous tick, world units
    float2 current;   // and at this tick
    uint size;        // two f16: width, then height
    uint rotation;    // two f16: clockwise radians at the previous tick, then at this one
    uint regionMin;   // two unorm16: the region's top-left on its page
    uint regionMax;   // two unorm16: its bottom-right
    uint layerFlags;  // u16 layer (page), then u16 flags
    uint colour;      // premultiplied RGBA8, red in the low byte
};

struct Draw {
    float4 worldToClip;  // clip = world * xy + zw (gfx::ClipTransform)
    float alpha;         // the render clock's progress from previous to current
    uint firstInstance;  // this batch's first instance: SV_InstanceID counts from 0 in every draw
};

StructuredBuffer<Instance> instances : register(t0, space0);
ConstantBuffer<Draw> draw : register(b0, space1);

float2 UnpackHalves(uint packed) {
    return float2(f16tof32(packed), f16tof32(packed >> 16));
}

float2 UnpackUnorm16(uint packed) {
    return float2(packed & 0xFFFF, packed >> 16) / 65535.0;
}

VertexOut VertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
    const Instance instance = instances[draw.firstInstance + instanceId];
    const float2 corner = float2(vertexId & 1, vertexId >> 1); // a strip of 4 vertices
    const float2 rotation = UnpackHalves(instance.rotation);
    const float angle = lerp(rotation.x, rotation.y, draw.alpha);
    float sine, cosine;
    sincos(angle, sine, cosine);
    const float2 offset = (corner - 0.5) * UnpackHalves(instance.size);
    // Clockwise on screen: world y grows downwards.
    const float2 turned = float2(offset.x * cosine - offset.y * sine, offset.x * sine + offset.y * cosine);
    const float2 world = lerp(instance.previous, instance.current, draw.alpha) + turned;
    const float2 regionMin = UnpackUnorm16(instance.regionMin);
    const float2 regionMax = UnpackUnorm16(instance.regionMax);
    const uint colour = instance.colour;
    VertexOut output;
    output.position = float4(world * draw.worldToClip.xy + draw.worldToClip.zw, 0, 1);
    output.uv = lerp(regionMin, regionMax, corner);
    output.layer = instance.layerFlags & 0xFFFF;
    output.colour = float4(colour & 0xFF, (colour >> 8) & 0xFF, (colour >> 16) & 0xFF, colour >> 24) / 255.0;
    return output;
}
