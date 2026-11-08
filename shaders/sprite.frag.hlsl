#include "sprite.hlsli"

// The atlas pages as one texture array (§8.7). SDL_GPU's SPIR-V takes combined image samplers (§8.8).
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2DArray<float4> atlas : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState atlasSampler : register(s0, space2);

// The sprite's coverage at `uv`: transparent outside its region, where its neighbours on the page lie.
float CoverageAt(float2 uv, VertexOut input) {
    if (any(uv < input.region.xy) || any(uv > input.region.zw)) {
        return 0.0;
    }
    return atlas.SampleLevel(atlasSampler, float3(uv, input.layer), 0).a;
}

float4 FragmentMain(VertexOut input) : SV_Target0 {
    if (input.ring == 0.0) {
        return atlas.Sample(atlasSampler, float3(input.uv, input.layer)) * input.colour;
    }
    // An outline: the ring of transparent pixels within the ring's width of the sprite's coverage.
    const float inside = CoverageAt(input.uv, input);
    float near = 0.0;
    [unroll] for (uint i = 0; i < 16; ++i) {
        float sine, cosine;
        sincos(i * (6.28318530 / 16.0), sine, cosine);
        near = max(near, CoverageAt(input.uv + float2(cosine, sine) * input.ring, input));
        near = max(near, CoverageAt(input.uv + float2(cosine, sine) * (input.ring * 0.5), input));
    }
    return input.colour * saturate(near - inside);
}
