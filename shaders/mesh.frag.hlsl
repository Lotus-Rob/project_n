#include "mesh.hlsli"

// An R8 page of coverage (the glyph cache's). SDL_GPU's SPIR-V takes combined image samplers (design doc 0002 §8.8).
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float> page : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState pageSampler : register(s0, space2);

float4 FragmentMain(VertexOut input) : SV_Target0 {
    return input.colour * page.Sample(pageSampler, input.uv);
}
