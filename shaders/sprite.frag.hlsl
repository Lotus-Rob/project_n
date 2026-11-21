#include "sprite.hlsli"

// The atlas pages as one texture array, and each sprite's distance field at its place on them (gfx::FieldBlockOf()).
// SDL_GPU's SPIR-V takes combined image samplers.
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2DArray<float4> atlas : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState atlasSampler : register(s0, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] Texture2DArray<float> field : register(t1, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] SamplerState fieldSampler : register(s1, space2);
#define FOG_TEXTURE_SLOT 2
#include "fog.hlsli"

/// A dissolve's blocks are this many screen pixels square, so it stays crisp at every zoom.
static const float DISSOLVE_BLOCK_PIXELS = 4.0;

/// A number from 0 to 1 for the block `block`, the same wherever and whenever it is drawn (a PCG hash, 2D).
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

/// `colour` as a dissolve leaves it (core::SpriteDraw::dissolve): blocks of the sprite's own UV (from its region's corner,
/// not its place on the page, so the pattern travels with it and the atlas packing doesn't reshuffle it) are discarded, more
/// of them the further it has gone, and the rest fade. `uvPerPixel` is fwidth(input.uv), taken before any discard.
float4 Dissolved(float4 colour, VertexOut input, float2 uvPerPixel) {
    if (input.dissolve > 0.0) {
        const float2 block = floor((input.uv - input.region.xy) / max(uvPerPixel * DISSOLVE_BLOCK_PIXELS, 1e-7));
        if (BlockHash(block) < input.dissolve) {
            discard;
        }
        return colour * (1.0 - input.dissolve);
    }
    return colour;
}

/// A sprite wearing a glow, its quad grown by the glow's reach: the sprite over its glow. The glow covers everything under
/// the sprite and fades out from its edge (the field's one half) between the shape's two distances, reaching nothing at the
/// reach; light's alpha is 0, so it adds to what is under it, while ink covers it.
float4 Glowing(VertexOut input, float2 uvPerPixel) {
    // The gutter round the region repeats the sprite's edge for filtering: past the region the sprite is transparent.
    const bool inRegion = all(input.uv >= input.region.xy) && all(input.uv <= input.region.zw);
    const float4 sampled = atlas.Sample(atlasSampler, float3(input.uv, input.layer)) * input.colour;
    // Texels from the sprite's edge, negative inside. This sample's mip and fwidth() below take derivatives after
    // Shaded()'s branches, which is sound: they test flat per-instance inputs (`glowOnly`, `glowShape`), so every pixel
    // of a primitive's 2x2 quads takes the same way.
    const float outside = FieldTexels(field.Sample(fieldSampler, float3(input.uv, input.layer)));
    // A screen pixel in texels: the fade is never sharper than that.
    const float pixel = max(fwidth(outside), 1e-3);
    const float2 shape = input.glowShape;
    // The fade ends at the reach itself, not a pixel past it: the quad ends there, and a reach of the field's whole 16
    // texels would otherwise cover every texel beyond the field, which reads 16.
    const float coverage = outside <= 0.0 ? 1.0 : 1.0 - smoothstep(shape.y - pixel, shape.x, outside);
    if (input.glowOnly != 0) {
        // Only the glow: no sprite colour, and none of it over the sprite's opaque pixels.
        return input.glowColour * (coverage * (1.0 - (inRegion ? sampled.a : 0.0)));
    }
    // The glow goes with the sprite's discarded blocks; it fades by the dissolve already (the vertex stage).
    const float4 sprite = Dissolved(inRegion ? sampled : 0.0, input, uvPerPixel);
    return sprite + input.glowColour * (coverage * (1.0 - sprite.a));
}

/// `colour` (premultiplied) as the fog leaves it: land drained, veiled and shaded by its tile's state, a thing that keeps
/// its colour only shaded, and a sprite fading with sight at its sight point.
float4 Fogged(float4 colour, VertexOut input) {
    if (fog.on == 0) {
        return colour;
    }
    if ((input.fogFlags & INSTANCE_FOGGED_LAND) != 0) {
        colour = FoggedLand(colour.rgb, colour.a, input.world);
    }
    else if ((input.fogFlags & INSTANCE_FOG_SHADE) != 0) {
        colour = FogShaded(colour.rgb, colour.a, input.world);
    }
    if ((input.fogFlags & INSTANCE_SIGHT_FADE) != 0) {
        colour *= smoothstep(0.5, 1.0, FogAt(input.sightPoint).y);
    }
    return colour;
}

/// The sprite's premultiplied colour before the fog: over its glow when it wears one, the glow alone for a glow-only draw,
/// else its texel, dissolved.
float4 Shaded(VertexOut input) {
    // Before any discard: derivatives need every pixel of the quad. The branches below test only flat per-instance
    // inputs, the same for every pixel of a primitive's quads, so derivatives taken inside them (Glowing(), the samples)
    // stay defined.
    const float2 uvPerPixel = fwidth(input.uv);
    if (input.glowShape.x > 0.0) {
        return Glowing(input, uvPerPixel);
    }
    if (input.glowOnly != 0) {
        return 0.0; // a role that reaches nothing draws nothing
    }
    return Dissolved(atlas.Sample(atlasSampler, float3(input.uv, input.layer)) * input.colour, input, uvPerPixel);
}

float4 FragmentMain(VertexOut input) : SV_Target0 {
    return Fogged(Shaded(input), input);
}
