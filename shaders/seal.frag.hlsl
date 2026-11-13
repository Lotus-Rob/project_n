#include "field.hlsli"
#include "seal.hlsli"

// The sprites' distance fields (gfx::SpriteArray::field): the seals' runes, flourishes and emblems are white sprites, so
// their fields alone draw them, in their part's colour. Then the layers, parts and sprites (gfx::SealPack), as storage
// buffers after the sampled texture (§8.8). SDL_GPU's SPIR-V takes combined image samplers.
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2DArray<float> field : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState fieldSampler : register(s0, space2);
[[vk::binding(1, 2)]] StructuredBuffer<SealLayer> layers : register(t1, space2);
[[vk::binding(2, 2)]] StructuredBuffer<SealPart> parts : register(t2, space2);
[[vk::binding(3, 2)]] StructuredBuffer<SealSprite> sprites : register(t3, space2);

// SealLookUniforms in src/gfx/internal/features/seals.cpp, 16 bytes: what the table's seals share. The halo is light round
// every line and glyph, fading out, so a seal glows rather than reading as flat ink (core::Seals::haloReach and
// haloStrength).
struct Look {
    float haloReach;    // how far a part's halo reaches past its edge, in seal sizes
    float haloStrength; // how strong it is at the edge
    float bandMargin;   // how far past a layer's radii its band's geometry reaches (gfx::SealBandMargin())
    float padding;      // to 16 bytes
};

ConstantBuffer<Look> look : register(b0, space3);

static const float PI = 3.14159265;
static const float TAU = 6.28318531;

// The pixel being shaded, on its seal's plane: where it is and how much of the plane a screen pixel spans there.
struct Pixel {
    float2 local; // in seal sizes from the centre, not turned
    float2 dx;    // how far `local` moves a pixel right
    float2 dy;    // and a pixel down
    float radius; // how far it is from the centre
    float angle;  // its direction from the centre, in radians
    float pixel;  // the most a screen pixel spans of the plane, in seal sizes
};

float2 Direction(float angle) {
    float sine, cosine;
    sincos(angle, sine, cosine);
    return float2(cosine, sine);
}

// How far the plane moves along `normal` (a unit direction on it) over one screen pixel, in seal sizes: a distance field's
// pixel there, from the derivatives taken once at the top, so no derivative is taken inside a branch.
float Across(Pixel p, float2 normal) {
    return max(length(float2(dot(normal, p.dx), dot(normal, p.dy))), 1e-6);
}

// How much of a pixel `across` wide a stroke covers, `halfWidth` each way of its middle, the pixel's centre `distance` from it: a
// box filter, so a line thinner than a pixel keeps its ink and fades rather than crawls as it moves.
float Stroke(float distance, float halfWidth, float across) {
    const float away = abs(distance);
    return saturate((halfWidth - away) / across + 0.5) - saturate((-halfWidth - away) / across + 0.5);
}

// The halo round a stroke `halfWidth` each way of its middle, `distance` from it: from its strength at the stroke's edge (and
// within it) to nothing the halo's reach past it.
float Halo(float distance, float halfWidth) {
    const float fade = 1.0 - saturate((abs(distance) - halfWidth) / max(look.haloReach, 1e-6));
    return look.haloStrength * fade * fade;
}

// A part's light: its ink's coverage, and its halo only where the ink leaves the pixel uncovered, so a line's own colour
// stays its colour rather than brightening towards white on the pale ground.
float Lit(float coverage, float halo) {
    return coverage + ((1.0 - saturate(coverage)) * halo);
}

// `value` wrapped into 0 to `period`.
float Wrap(float value, float period) {
    return value - (period * floor(value / period));
}

// The nearest to `angle` of `count` items round a ring, the first at `first`: its index from 0, then its angle.
float2 Nearest(float angle, float first, uint count) {
    const float step = TAU / count;
    const float turns = round((angle - first) / step);
    return float2(Wrap(turns, count), first + (turns * step));
}

float CircleLight(Pixel p, SealPart part, float first) {
    const float2 normal = p.local / max(p.radius, 1e-6);
    const float halfWidth = part.width * 0.5;
    const float away = p.radius - part.radius;
    float light = Lit(Stroke(away, halfWidth, Across(p, normal)), Halo(away, halfWidth));
    if (part.dash > 0.0) {
        // Along the ring from the middle of the nearest dash, in seal sizes, counted from the ring's first dash: a period
        // that doesn't divide the turn leaves its one shorter dash there, turning with the ring, not at a fixed angle.
        const float around = Wrap(p.angle - first, TAU);
        const float along = abs(Wrap(around, part.dash + part.gap) - (part.dash * 0.5)) * part.radius;
        light *= Stroke(along, part.dash * 0.5 * part.radius, Across(p, float2(-normal.y, normal.x)));
    }
    return light;
}

float TicksLight(Pixel p, SealPart part, float first) {
    const float2 outward = Direction(Nearest(p.angle, first, part.count).y);
    const float2 side = float2(-outward.y, outward.x);
    const float along = dot(p.local, outward) - part.radius;
    const float across = dot(p.local, side);
    const float halfWidth = part.width * 0.5;
    const float lengthwise = Stroke(along, part.size * 0.5, Across(p, outward));
    const float beyond = max(abs(along) - (part.size * 0.5), 0.0);
    return Lit(Stroke(across, halfWidth, Across(p, side)) * lengthwise, Halo(sqrt((beyond * beyond) + (across * across)), halfWidth));
}

float DotsLight(Pixel p, SealPart part, float first) {
    const float2 offset = p.local - (Direction(Nearest(p.angle, first, part.count).y) * part.radius);
    const float away = length(offset);
    const float halfWidth = part.size * 0.5;
    return Lit(Stroke(away, halfWidth, Across(p, offset / max(away, 1e-6))), Halo(away, halfWidth));
}

// A star polygon's lines, or a polygon's (a step of 1): `count` points at the part's radius, each joined to the `joins`th
// after it. Every line lies the same distance from the centre, its normal midway between the points it joins; of them,
// only the line nearest the pixel's direction and the two that cross the pixel's circle near it can be nearest.
float StarLight(Pixel p, SealPart part, float first, uint joins) {
    const float slice = TAU / part.count;
    const float spread = PI * joins / part.count; // half the angle a line spans
    const float apothem = part.radius * cos(spread);
    const float halfLength = part.radius * sin(spread);
    const float firstNormal = first + spread;
    const float crossing = acos(saturate(apothem / max(p.radius, 1e-6)));
    float nearest = 1e9;
    float2 nearestNormal = float2(1.0, 0.0);
    [unroll] for (int side = -1; side <= 1; ++side) {
        const float toward = p.angle + (side * crossing);
        const float2 normal = Direction(firstNormal + (round((toward - firstNormal) / slice) * slice));
        const float offLine = dot(p.local, normal) - apothem;
        const float beyond = max(abs(dot(p.local, float2(-normal.y, normal.x))) - halfLength, 0.0);
        const float away = sqrt((offLine * offLine) + (beyond * beyond));
        if (away < nearest) {
            nearest = away;
            nearestNormal = normal;
        }
    }
    const float halfWidth = part.width * 0.5;
    return Lit(Stroke(nearest, halfWidth, Across(p, nearestNormal)), Halo(nearest, halfWidth));
}

// The light of `sprite` drawn `height` tall at `centre`, its top towards `up` (a unit direction on the plane): from its
// distance field, anti-aliased inside its edge, with its halo outside. Sampled with the gradients worked out from the
// plane's, as the sample may sit in a branch other pixels of its quad skip.
float SpriteLight(Pixel p, SealSprite sprite, float2 centre, float2 up, float height) {
    const float2 right = float2(-up.y, up.x);
    const float texels = sprite.height / height; // a seal size, in the sprite's texels
    const float2 block = float2(sprite.width, sprite.height) + (2.0 * FIELD_REACH_TEXELS);
    // Texels from the sprite's middle, right and down as its image runs.
    const float2 offset = p.local - centre;
    const float2 at = float2(dot(offset, right), -dot(offset, up)) * texels;
    if (any(abs(at) >= block * 0.5)) {
        return 0.0; // past its field's reach
    }
    const float2 uvPerTexel = (sprite.fieldMax - sprite.fieldMin) / block;
    const float2 uv = ((sprite.fieldMin + sprite.fieldMax) * 0.5) + (at * uvPerTexel);
    const float2 uvDx = float2(dot(p.dx, right), -dot(p.dx, up)) * texels * uvPerTexel;
    const float2 uvDy = float2(dot(p.dy, right), -dot(p.dy, up)) * texels * uvPerTexel;
    const float outside = FieldTexels(field.SampleGrad(fieldSampler, float3(uv, sprite.page), uvDx, uvDy));
    // A screen pixel in texels, its longer way across: the field's own direction isn't known without more samples, and a
    // filter too wide only softens an edge, where one too narrow lets it alias where the plane is squashed.
    const float pixel = max(texels * p.pixel, 1e-3);
    const float fade = 1.0 - saturate(outside / clamp(look.haloReach * texels, 1e-3, FIELD_REACH_TEXELS));
    return Lit(saturate(0.5 - (outside / pixel)), look.haloStrength * fade * fade);
}

// A script's glyph or a flourish nearest the pixel, its top outward (or a script's inward).
float RingSpriteLight(Pixel p, SealPart part, float first) {
    if (part.spriteCount == 0) {
        return 0.0;
    }
    const float2 item = Nearest(p.angle, first, part.count);
    const float2 itemOut = Direction(item.y);
    const SealSprite sprite = sprites[part.firstSprite + (uint(item.x) % part.spriteCount)];
    return SpriteLight(p, sprite, itemOut * part.radius, part.inward != 0 ? -itemOut : itemOut, part.size);
}

// How far from the centre a part's ink and halo reach, in seal sizes: the pixels outside that never test it. Its start
// less the margin is SealBandStart() in src/gfx/seal_draw.cpp, which sorts a layer's parts by it: the two change together.
float2 PartBand(Pixel p, SealPart part) {
    const float margin = look.haloReach + p.pixel;
    if (part.kind == SEAL_CIRCLE) {
        return float2(part.radius - (part.width * 0.5) - margin, part.radius + (part.width * 0.5) + margin);
    }
    if (part.kind == SEAL_TICKS || part.kind == SEAL_DOTS) {
        const float reach = (max(part.size, part.width) * 0.5) + margin;
        return float2(part.radius - reach, part.radius + reach);
    }
    if (part.kind == SEAL_STAR || part.kind == SEAL_POLYGON) {
        const float joins = part.kind == SEAL_STAR ? part.step : 1.0;
        const float reach = (part.width * 0.5) + margin;
        return float2((part.radius * cos(PI * joins / part.count)) - reach, part.radius + reach);
    }
    if (part.kind == SEAL_EMBLEM) {
        return float2(-1.0, 1e9);
    }
    // A glyph or a flourish: its half diagonal and its field's reach stay within its height for the art's proportions.
    return float2(part.radius - part.size - margin, part.radius + part.size + margin);
}

float PartLight(Pixel p, SealLayer layer, SealPart part) {
    const float first = layer.angle + part.rotation;
    if (part.kind == SEAL_CIRCLE) {
        return CircleLight(p, part, first);
    }
    if (part.kind == SEAL_TICKS) {
        return TicksLight(p, part, first);
    }
    if (part.kind == SEAL_DOTS) {
        return DotsLight(p, part, first);
    }
    if (part.kind == SEAL_STAR) {
        return StarLight(p, part, first, part.step);
    }
    if (part.kind == SEAL_POLYGON) {
        return StarLight(p, part, first, 1);
    }
    if (part.kind == SEAL_EMBLEM) {
        if (part.spriteCount == 0) {
            return 0.0;
        }
        // Upright at its zero: its top towards the top of the plane.
        return SpriteLight(p, sprites[part.firstSprite], float2(0.0, 0.0), Direction(layer.angle - (PI * 0.5)), part.size);
    }
    return RingSpriteLight(p, part, first); // a script or a flourish
}

// How much of the pixel lies inside the regular polygon of `sides` with its corners `radius` out, its first at `first`:
// 1 from `full` pixels inside its sides, fading over a pixel to nothing at `full` - 1 (0 ends at the sides; 0.5 centres
// the fade on them).
float InsidePolygon(Pixel p, uint sides, float radius, float first, float full) {
    const float slice = TAU / sides;
    const float firstNormal = first + (slice * 0.5);
    const float2 normal = Direction(firstNormal + (round((p.angle - firstNormal) / slice) * slice));
    const float offSide = dot(p.local, normal) - (radius * cos(PI / sides));
    return saturate(full - (offSide / Across(p, normal)));
}

// How much of the pixel lies between a frame layer's polygons with their corners `inner` and `outer` out, fading over a
// pixel `full` from their sides as InsidePolygon() does.
float BetweenPolygons(Pixel p, SealLayer layer, float inner, float outer, float full) {
    const float first = layer.angle + layer.frameRotation;
    const float inside = InsidePolygon(p, layer.frameSides, outer, first, full);
    return inner > 0.0 ? inside * (1.0 - InsidePolygon(p, layer.frameSides, inner, first, 1.0 - full)) : inside;
}

// A frame layer's band as its geometry has it (gfx::DrawSeals()): between polygons with their corners the band margin
// inside its inner radius and past its outer one and its flourishes' spill, fading out before the geometry ends, so
// nothing the frame lets through is cut off with a hard edge.
float FrameMask(Pixel p, SealLayer layer) {
    const float margin = look.bandMargin;
    return BetweenPolygons(p, layer, layer.inner - margin, layer.outer + layer.spill + margin, 0.0);
}

// How much of a part a frame layer lets through: the frame's own polygon and star lines whole (its band holds them), the
// others cut off at its polygons with a pixel's fade, flourishes past its outer one by their spill.
float FrameClip(Pixel p, SealLayer layer, SealPart part) {
    if (part.kind == SEAL_POLYGON || part.kind == SEAL_STAR) {
        return 1.0;
    }
    const float spill = part.kind == SEAL_FLOURISH ? layer.spill : 0.0;
    return BetweenPolygons(p, layer, layer.inner, layer.outer + spill, 0.5);
}

// A seal's wash: its ink, darkest at its centre's side of its reach and fading out to nothing at it, over its softness's
// share of the reach (at least a pixel, so even a hard edge is smooth).
float4 Wash(Pixel p, SealLayer layer) {
    const float fade = max(layer.washReach * layer.washSoftness, Across(p, p.local / max(p.radius, 1e-6)));
    const float inside = smoothstep(0.0, 1.0, (layer.washReach - p.radius) / fade);
    return UnpackColour(layer.washColour) * (layer.washOpacity * inside);
}

float4 FragmentMain(SealOut input) : SV_Target0 {
    Pixel p;
    p.local = input.local;
    // Before any branch: derivatives need every pixel of the quad.
    p.dx = ddx(input.local);
    p.dy = ddy(input.local);
    p.radius = length(p.local);
    p.angle = p.radius > 0.0 ? atan2(p.local.y, p.local.x) : 0.0;
    p.pixel = max(length(p.dx), length(p.dy));
    const SealLayer layer = layers[input.layer];
    if (input.wash != 0) {
        return Wash(p, layer);
    }
    const float3 colours[2] = { UnpackColour(layer.colour0).rgb, UnpackColour(layer.colour1).rgb };
    float3 light = 0.0;
    // Sorted by where their bands start (gfx::PackSeals()): once a part's band starts past the pixel, every later one's
    // does too, so only the parts up to there are tested, and only those whose band holds the pixel are drawn.
    for (uint index = 0; index < layer.partCount; ++index) {
        const SealPart part = parts[layer.firstPart + index];
        const float2 band = PartBand(p, part);
        if (p.radius < band.x) {
            break;
        }
        if (p.radius > band.y) {
            continue;
        }
        const bool framed = layer.frameSides >= 3;
        light += colours[min(part.slot, 1)] * (PartLight(p, layer, part) * (framed ? FrameClip(p, layer, part) : 1.0));
    }
    const float mask = layer.frameSides >= 3 ? FrameMask(p, layer) : 1.0;
    // Light: its alpha is 0, so the additive blend only adds it.
    return float4(light * (layer.opacity * mask), 0.0);
}
