#include "seal.hlsli"

// LightUniforms in src/gfx/internal/features/seals.cpp, 48 bytes.
struct LightDraw {
    float4 worldToClip;   // clip = world * xy + zw (gfx::ClipTransform)
    float2 pixelsPerClip; // the target's pixels per clip unit, across and down: half its size
    uint firstRecord;     // this plane's first record in the records
    uint recordCount;     // and how many it has: SV_InstanceID counts its particles from 0
    uint ink;             // 1 while the particles' ink draws, 0 while their light does
    uint3 padding;        // to a multiple of 16 bytes
};

// Every drawn seal's light (gfx::SealDrawing's ground records, then the others').
StructuredBuffer<SealParticles> records : register(t0, space0);
ConstantBuffer<LightDraw> draw : register(b0, space1);

// The least a particle spans on screen, in pixels, each way: a smaller one is drawn this wide and that much fainter, so it
// keeps its light and never crawls between pixels.
static const float SEAL_LEAST_PIXELS = 2.0;

// `halfSize` (half a quad's side, world units) grown to at least SEAL_LEAST_PIXELS across on screen; `strength` dimmed
// as much.
float2 Least(float2 halfSize, inout float strength) {
    const float pixels = 2.0 * length(halfSize * draw.worldToClip.xy * draw.pixelsPerClip);
    if (pixels < SEAL_LEAST_PIXELS) {
        const float grow = SEAL_LEAST_PIXELS / max(pixels, 1e-6);
        strength /= grow;
        return halfSize * grow;
    }
    return halfSize;
}

SealLightOut VertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
    // The record holding this particle: the last whose first particle is at or before it.
    uint low = draw.firstRecord;
    uint high = draw.firstRecord + draw.recordCount - 1;
    [loop] while (low < high) {
        const uint middle = (low + high + 1) / 2;
        if (records[middle].firstParticle <= instanceId) {
            low = middle;
        }
        else {
            high = middle - 1;
        }
    }
    const SealParticles light = records[low];
    const SealParticle particle = SealParticleAt(light, instanceId - light.firstParticle);
    float strength = particle.strength;
    const float2 along = Least(particle.along, strength);
    const float2 across = Least(particle.across, strength);
    const float2 corner = float2(vertexId & 1, vertexId >> 1) * 2.0 - 1.0; // a strip of 4 vertices
    const float2 world = particle.centre + (across * corner.x) + (along * corner.y);
    SealLightOut output;
    output.position = float4(world * draw.worldToClip.xy + draw.worldToClip.zw, 0, 1);
    output.corner = corner;
    const float4 colour = draw.ink != 0 ? UnpackColour(light.washColour) * light.washOpacity
                                        : UnpackColour(light.colour) * light.opacity;
    output.colour = colour * strength;
    output.kind = particle.kind;
    output.phase = particle.phase;
    output.aspect = length(along) / max(length(across), 1e-6);
    output.shape = float4(light.pillarFade, light.pillarRest, light.pillarBand, light.shardEdge);
    return output;
}
