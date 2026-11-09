#include "composite.hlsli"

// SDL_GPU's SPIR-V takes combined image samplers (design doc 0002 §8.8).
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> scene : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState sceneSampler : register(s0, space2);

// The piecewise sRGB curve, as the hardware's sRGB encode applies it.
float3 EncodeSrgb(float3 light) {
    const float3 curve = 1.055 * pow(light, 1.0 / 2.4) - 0.055;
    return lerp(curve, light * 12.92, step(light, 0.0031308));
}

float4 FragmentMain(VertexOut input) : SV_Target0 {
    return float4(EncodeSrgb(saturate(scene.Sample(sceneSampler, input.uv).rgb)), 1.0);
}
