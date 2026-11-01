#include "sprite.hlsli"

// The atlas pages as one texture array (§8.7). SDL_GPU's SPIR-V takes combined image samplers (§8.8).
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2DArray<float4> atlas : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState atlasSampler : register(s0, space2);

float4 FragmentMain(VertexOut input) : SV_Target0 {
    return atlas.Sample(atlasSampler, float3(input.uv, input.layer)) * input.colour;
}
