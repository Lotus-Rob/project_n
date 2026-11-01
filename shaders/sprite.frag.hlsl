#include "sprite.hlsli"

// SDL_GPU's SPIR-V takes combined image samplers (§8.8).
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> atlas : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState atlasSampler : register(s0, space2);

float4 FragmentMain(VertexOut input) : SV_Target0 {
    return atlas.Sample(atlasSampler, input.uv);
}
