// The fog of war over the world map, shared by every shader that draws its land: the fog's texture, its uniforms and how
// they treat a colour. The including shader defines FOG_TEXTURE_SLOT, the fragment sampler slot the fog texture takes
// after its own textures; the fog's uniforms are always the fragment stage's first uniform buffer.

#define FOG_CONCAT(prefix, slot) prefix##slot
#define FOG_REGISTER(prefix, slot) FOG_CONCAT(prefix, slot)

// The fog over the world map, a texel per tile: explored in red, in sight in green (gfx::FogFrame), filtered linearly and
// clamped at the edge, as gfx::FogSample() samples it.
[[vk::combinedImageSampler]] [[vk::binding(FOG_TEXTURE_SLOT, 2)]] Texture2D<float2> fogField
    : register(FOG_REGISTER(t, FOG_TEXTURE_SLOT), space2);
[[vk::combinedImageSampler]] [[vk::binding(FOG_TEXTURE_SLOT, 2)]] SamplerState fogSampler
    : register(FOG_REGISTER(s, FOG_TEXTURE_SLOT), space2);

/// gfx::FogUniforms in src/gfx/fog.hpp, 64 bytes.
struct Fog {
    float2 mapSize; // the field's tiles across and down
    float tileSize; // a tile's side in world units
    uint on;        // 1 while the view has fog; 0 leaves every colour as it is
    float4 sepia;   // rgb: the sepia colour over its own luminance, linear; a: how far the colour drains
    float4 mist;    // rgb: the mist's linear colour; a: how much of it shows
    float4 shade;   // rgb: the linear colour under the clouds; a unused
};
ConstantBuffer<Fog> fog : register(b0, space3);

/// The fog flags of a sprite instance: core::INSTANCE_FOGGED_LAND, core::INSTANCE_SIGHT_FADE and core::INSTANCE_FOG_SHADE.
static const uint INSTANCE_FOGGED_LAND = 1;
static const uint INSTANCE_SIGHT_FADE = 2;
static const uint INSTANCE_FOG_SHADE = 4;

/// Rec. 709's weights of linear red, green and blue in luminance: FOG_LUMINANCE in src/gfx/fog.cpp.
static const float3 LUMINANCE = float3(0.2126, 0.7152, 0.0722);

/// The fog's explored (x) and in-sight (y) values at `world`, each 0 to 1.
float2 FogAt(float2 world) {
    // The field has no mips, so the sample needs no derivatives, which a discarded neighbour would leave undefined.
    return fogField.SampleLevel(fogSampler, world / (fog.mapSize * fog.tileSize), 0.0);
}

/// `rgb`, premultiplied by `alpha`, shaded at a pixel whose fog reads `explored`: wholly the shade colour where unexplored,
/// none of it from half explored on, the value at an unexplored tile's edge, so the shade stays under its edge row of
/// clouds.
float3 ShadedUnexplored(float3 rgb, float alpha, float explored) {
    return lerp(rgb, fog.shade.rgb * alpha, 1.0 - smoothstep(0.0, 0.5, explored));
}

/// Land at `world`, its colour `rgb` premultiplied by `alpha`, as the fog leaves it: drained toward sepia and veiled in
/// mist where it is explored but out of sight, shaded where unexplored; as it is while the view has no fog.
float4 FoggedLand(float3 rgb, float alpha, float2 world) {
    if (fog.on == 0) {
        return float4(rgb, alpha);
    }
    const float2 state = FogAt(world);
    const float explored = state.x;
    // Seen once but out of sight now.
    const float remembered = explored * (1.0 - state.y);
    const float3 drained = lerp(rgb, dot(rgb, LUMINANCE) * fog.sepia.rgb, fog.sepia.a);
    float3 result = lerp(rgb, drained, remembered);
    result = lerp(result, fog.mist.rgb * alpha, fog.mist.a * remembered);
    return float4(ShadedUnexplored(result, alpha, explored), alpha);
}

/// A thing at `world` that stays in colour out of sight, its colour `rgb` premultiplied by `alpha`, as the fog leaves it:
/// only shaded where unexplored, so it hides under the clouds as land does; as it is while the view has no fog.
float4 FogShaded(float3 rgb, float alpha, float2 world) {
    if (fog.on == 0) {
        return float4(rgb, alpha);
    }
    return float4(ShadedUnexplored(rgb, alpha, FogAt(world).x), alpha);
}
