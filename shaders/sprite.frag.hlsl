#include "sprite.hlsli"

// The atlas pages as one texture array (§8.7). SDL_GPU's SPIR-V takes combined image samplers (§8.8).
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2DArray<float4> atlas : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState atlasSampler : register(s0, space2);

// A dissolve's blocks are this many screen pixels square, so it stays crisp at every zoom.
static const float DISSOLVE_BLOCK_PIXELS = 4.0;

// A number from 0 to 1 for the block `block`, the same wherever and whenever it is drawn (a PCG hash, 2D).
float BlockHash(float2 block) {
    uint2 v = uint2(int2(block)) * 1664525u + 1013904223u;
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v ^= v >> 16u;
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v ^= v >> 16u;
    return (v.x >> 8) * (1.0 / 16777216.0);
}

// The sprite's coverage at `uv`: transparent outside its region, where its neighbours on the page lie.
float CoverageAt(float2 uv, VertexOut input) {
    if (any(uv < input.region.xy) || any(uv > input.region.zw)) {
        return 0.0;
    }
    return atlas.SampleLevel(atlasSampler, float3(uv, input.layer), 0).a;
}

float4 FragmentMain(VertexOut input) : SV_Target0 {
    // Before any branch: derivatives need every pixel of the quad.
    const float2 uvPerPixel = fwidth(input.uv);
    if (input.ring == 0.0) {
        const float4 colour = atlas.Sample(atlasSampler, float3(input.uv, input.layer)) * input.colour;
        if (input.dissolve > 0.0) {
            // Blocks of the sprite's own UV (from its region's corner, not its place on the page), so the pattern travels with it
            // and the atlas packing doesn't reshuffle it; more of them gone, the rest fainter.
            const float2 block = floor((input.uv - input.region.xy) / max(uvPerPixel * DISSOLVE_BLOCK_PIXELS, 1e-7));
            if (BlockHash(block) < input.dissolve) {
                discard;
            }
            return colour * (1.0 - input.dissolve);
        }
        return colour;
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
