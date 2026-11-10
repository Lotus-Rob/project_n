#include "mesh.hlsli"

// An R8 page of distance fields (the glyph cache's). SDL_GPU's SPIR-V takes combined image samplers (design doc 0002 §8.8).
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float> page : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState pageSampler : register(s0, space2);

// gfx::internal::TextStyle (src/gfx/internal/features/meshes.hpp).
struct TextStyle {
    float outline;      // 1: draw the dark outline; 0: don't
    float shadow;       // 1: draw the soft shadow; 0: don't
    float2 shadowShift; // how far down and right the shadow falls, in page fractions
};

ConstantBuffer<TextStyle> style : register(b0, space3);

// FreeType's distance fields, as SDL3_ttf rasterises them with spread 8: each byte is 128 on the outline plus 16 for every
// base pixel inside it (128 / spread), clamped to 0..255. Sampled as unorm, the outline is 128/255 and a base pixel of
// distance 16/255.
static const float EDGE = 128.0 / 255.0;
static const float BASE_PIXEL = 16.0 / 255.0;
static const float OUTLINE = 2.0 * BASE_PIXEL; // the outline's width: 2 base pixels, scaling with the text
static const float SHADOW_OUTER = 0.2;         // the shadow fades in from about 5 base pixels outside the outline
static const float SHADOW_INNER = 0.6;         // to full a pixel and a half inside it: soft
// Where in the pixel the field is sampled: four points on a rotated grid, each a different x and y.
static const uint SAMPLES = 4;
static const float2 GRID[SAMPLES] = { float2(-0.125, -0.375), float2(0.375, -0.125), float2(0.125, 0.375), float2(-0.375, 0.125) };
// Each sample's edge ramp, in screen pixels, a little wider than the samples are apart: measured on the reference
// captures, it keeps a label's ink within 1% at sub-pixel offsets on pale ground (0.5 px: 2%) while edges stay crisp.
static const float RAMP_PIXELS = 0.75;
// The outline and shadow's dark ink: sRGB #20242A, linear. The same ink as gfx::TEXT_INK (src/gfx/text_place.hpp),
// which dark labels are drawn in: change both together.
static const float3 INK = float3(0.0144, 0.0176, 0.0232);

// How much of a sample's share of the pixel lies inside `edge`, where the field is `distance` and changes by
// `perPixel` across a screen pixel: a linear ramp RAMP_PIXELS wide.
float Coverage(float edge, float distance, float perPixel) {
    return saturate((distance - edge) / (perPixel * RAMP_PIXELS) + 0.5);
}

// The fill over the outline over `under`, at one sample.
float4 Composite(float distance, float perPixel, float4 under, float4 ink, float4 colour) {
    float4 composite = under;
    if (style.outline > 0.5) {
        composite = lerp(composite, ink, Coverage(EDGE - OUTLINE, distance, perPixel));
    }
    return lerp(composite, colour, Coverage(EDGE, distance, perPixel));
}

// The field's coverage, outline and shadow composited, premultiplied: the shadow under the outline under the fill, the
// ink as opaque as the text's colour, so a fading text fades whole. Each pixel averages SAMPLES points of the field on a
// rotated grid: a stroke or an outline thinner than a pixel then keeps its ink at any sub-pixel position, where one
// point would catch more or less of it as the text moves (shimmer and stepping).
float4 FragmentMain(VertexOut input) : SV_Target0 {
    // The field's change per screen pixel, from how many texels a pixel spans: the derivative of the field itself
    // collapses on a thin stroke's ridge, where it turns, so edges would sharpen and soften with the pixel phase.
    float pageWidth;
    float pageHeight;
    page.GetDimensions(pageWidth, pageHeight);
    const float2 texels = fwidth(input.uv) * float2(pageWidth, pageHeight);
    const float perPixel = max(0.5 * (texels.x + texels.y) * BASE_PIXEL, 1e-4);
    const float2 acrossX = ddx(input.uv);
    const float2 acrossY = ddy(input.uv);
    const float4 ink = float4(INK, 1.0) * input.colour.a;
    float4 under = float4(0.0, 0.0, 0.0, 0.0);
    if (style.shadow > 0.5) {
        const float shadow = page.SampleLevel(pageSampler, input.uv - style.shadowShift, 0);
        under = ink * smoothstep(SHADOW_OUTER, SHADOW_INNER, shadow);
    }
    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    [unroll] for (uint i = 0; i < SAMPLES; ++i) {
        const float2 uv = input.uv + (acrossX * GRID[i].x) + (acrossY * GRID[i].y);
        sum += Composite(page.SampleLevel(pageSampler, uv, 0), perPixel, under, ink, input.colour);
    }
    return sum / SAMPLES;
}
