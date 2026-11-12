#include "sprite.hlsli"

// core::Instance (src/core/frame.hpp), 56 bytes. 16-bit fields come in pairs, low half first, and
// are unpacked by hand: SDL_GPU's baseline doesn't guarantee native 16-bit types (§8.5).
struct Instance {
    float2 previous;  // the quad's centre at the previous tick, world units
    float2 current;   // and at this tick
    uint size;        // two f16: width, then height
    uint rotation;    // two f16: clockwise radians at the previous tick, then at this one
    uint regionMin;   // two unorm16: the region's top-left on its page
    uint regionMax;   // two unorm16: its bottom-right
    uint layerFlags;  // u16 layer (page), then the dissolve as unorm16
    uint colour;      // premultiplied RGBA8, red in the low byte
    uint glow;        // role (bits 0-7), result (8-15), element (16-23), released (bit 24); 0 for no glow
    uint glowValue;   // two unorm16: the glow's value at the previous tick, then at this one
    uint glowStart;   // the glow's start tick, low 32 bits
    uint reserved;    // pads the struct to 56 bytes, a multiple of 8 as SPIR-V's buffer layout needs
};

// gfx::GlowRecord (src/gfx/glow_table.hpp), 96 bytes: one role's glow; the table is indexed by core::GlowRole.
struct GlowRecord {
    float size;        // how far past the edge it reaches at zoom 1, in texels
    float feather;     // 0 a hard ring, 1 a soft fade
    float opacity;     // its strength at full drive
    float zoomShrink;  // how much it keeps its width on screen as the camera zooms
    uint blend;        // core::GlowBlend
    uint colour;       // linear premultiplied RGBA8; 0 for the instance's element colour
    uint driveKind;    // core::DriveKind
    uint curve;        // core::GlowCurve: the drive's curve
    float periodTicks; // a pulse's beat in ticks
    float depth;       // how far a pulse falls below full strength
    float fadeTicks;   // a charge's fade in ticks
    uint fadeCurve;    // core::GlowCurve: the fade's curve
    uint resultCount;  // how many of `results` count
    uint results[8];   // a progress's result colours, as `colour` is (core::MAX_GLOW_RESULTS)
    uint padding[3];   // to a multiple of 16 bytes
};

// The table's length: core::GLOW_ROLES.
static const uint GLOW_ROLES = 8;
// core::GlowBlend::Ink: the glow paints over what is under it; Light (0) only adds light.
static const uint GLOW_BLEND_INK = 1;
// The last core::HitKind (Burn): `elementColours` holds one colour for each.
static const uint LAST_ELEMENT = 3;
// core::DriveKind's values.
static const uint DRIVE_PULSE = 1;
static const uint DRIVE_CHARGE = 2;
static const uint DRIVE_PROGRESS = 3;
// core::GlowCurve's values (Linear is 0).
static const uint CURVE_EASE_IN = 1;
static const uint CURVE_EASE_OUT = 2;
static const uint CURVE_SMOOTH = 3;
static const uint CURVE_SINE = 4;
// core::REDUCED_LIGHT_OPACITY: how far reduce flashing scales a light glow's opacity.
static const float REDUCED_LIGHT_OPACITY = 0.5;
static const float PI = 3.14159265;

// DrawUniforms in src/gfx/internal/features/sprites.cpp, 80 bytes.
struct Draw {
    float4 worldToClip;   // clip = world * xy + zw (gfx::ClipTransform)
    float alpha;          // the render clock's progress from previous to current
    uint firstInstance;   // this batch's first instance: SV_InstanceID counts from 0 in every draw
    uint outlineColour;   // an outline draw's premultiplied RGBA8; 0 for sprites
    float ring;           // an outline draw's ring width in UV, its quad grown by as much on every side
    uint glowTick;        // the render clock's whole tick, low 32 bits (gfx::GlowTime)
    float glowFraction;   // how far past that tick the render clock is, 0 to 1
    float zoom;           // the camera's zoom: glows thin as it grows
    uint reduceFlashing;  // the accessibility setting, 1 when on
    uint4 elementColours; // the palette's effect colour of each core::HitKind, linear premultiplied RGBA8
    float texelUv;        // one texel of an atlas page, in UV
    float3 padding;       // to a multiple of 16 bytes
};

StructuredBuffer<Instance> instances : register(t0, space0);
StructuredBuffer<GlowRecord> glows : register(t1, space0);
ConstantBuffer<Draw> draw : register(b0, space1);

float2 UnpackHalves(uint packed) {
    return float2(f16tof32(packed), f16tof32(packed >> 16));
}

float2 UnpackUnorm16(uint packed) {
    return float2(packed & 0xFFFF, packed >> 16) / 65535.0;
}

float4 UnpackColour(uint colour) {
    return float4(colour & 0xFF, (colour >> 8) & 0xFF, (colour >> 16) & 0xFF, colour >> 24) / 255.0;
}

// core::CurveAt() (src/core/glow.cpp), line for line: `fraction`, held to 0..1, through `curve`.
float CurveAt(uint curve, float fraction) {
    const float held = clamp(fraction, 0.0, 1.0);
    if (curve == CURVE_EASE_IN) {
        return held * held;
    }
    if (curve == CURVE_EASE_OUT) {
        return 1.0 - ((1.0 - held) * (1.0 - held));
    }
    if (curve == CURVE_SMOOTH) {
        return held * held * (3.0 - (2.0 * held));
    }
    if (curve == CURVE_SINE) {
        return (1.0 - cos(PI * held)) * 0.5;
    }
    return held;
}

// core::GlowStrength() (src/core/glow.cpp), line for line: the drive's strength from 0 to 1. Its age is in ticks, as the
// record's period and fade are. The C++ takes a pulse's phase in f64; here the age is the wrapping difference of 32-bit
// ticks, whose whole ticks f32 holds exactly for 2^24 ticks (three days) after the start.
float GlowStrength(GlowRecord glow, float value, float ageTicks, uint result, bool released, bool reduceFlashing) {
    if (glow.driveKind == DRIVE_PULSE) {
        if (reduceFlashing) {
            return 1.0 - (glow.depth / 2.0);
        }
        const float beats = ageTicks / glow.periodTicks;
        const float phase = beats - floor(beats);
        const float away = abs((2.0 * phase) - 1.0);
        return 1.0 - (glow.depth * (1.0 - CurveAt(glow.curve, away)));
    }
    if (glow.driveKind == DRIVE_CHARGE) {
        const float charged = CurveAt(glow.curve, value);
        if (!released) {
            return charged;
        }
        const float faded = min(ageTicks / glow.fadeTicks, 1.0);
        return charged * (1.0 - CurveAt(glow.fadeCurve, faded));
    }
    if (glow.driveKind == DRIVE_PROGRESS) {
        return result == 0 ? CurveAt(glow.curve, value) : 1.0;
    }
    return 1.0;
}

VertexOut VertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
    const Instance instance = instances[draw.firstInstance + instanceId];
    const float2 corner = float2(vertexId & 1, vertexId >> 1); // a strip of 4 vertices
    const float2 rotation = UnpackHalves(instance.rotation);
    const float angle = lerp(rotation.x, rotation.y, draw.alpha);
    float sine, cosine;
    sincos(angle, sine, cosine);
    const float2 regionMin = UnpackUnorm16(instance.regionMin);
    const float2 regionMax = UnpackUnorm16(instance.regionMax);
    const float2 size = UnpackHalves(instance.size);
    const float4 colour = UnpackColour(draw.outlineColour != 0 ? draw.outlineColour : instance.colour);
    const float dissolve = (instance.layerFlags >> 16) / 65535.0;
    // How far past the region the quad reaches, in UV: an outline's ring, or a glow's reach.
    float grow = draw.ring;
    float4 glowColour = 0.0;
    float2 glowShape = 0.0;
    const uint role = instance.glow & 0xFF;
    if (role != 0 && role < GLOW_ROLES && draw.ring == 0.0) {
        const GlowRecord glow = glows[role];
        // Thinner as the camera zooms in; zoomed out, it stops at the field's reach.
        const float reach = min(glow.size * pow(1.0 / draw.zoom, glow.zoomShrink), FIELD_REACH_TEXELS);
        const uint element = min((instance.glow >> 16) & 0xFF, LAST_ELEMENT);
        const uint result = (instance.glow >> 8) & 0xFF;
        const bool released = ((instance.glow >> 24) & 1) != 0;
        const bool reduceFlashing = draw.reduceFlashing != 0;
        // A progress with a result shows the result's colour.
        uint packedTint = glow.colour != 0 ? glow.colour : draw.elementColours[element];
        if (glow.driveKind == DRIVE_PROGRESS && result != 0 && result <= glow.resultCount) {
            packedTint = glow.results[result - 1];
        }
        const float4 tint = UnpackColour(packedTint);
        const float2 values = UnpackUnorm16(instance.glowValue);
        // Wrapping 32-bit ticks: exact across the low bits' wrap in a long session.
        const float age = float(int(draw.glowTick - instance.glowStart)) + draw.glowFraction;
        const float strength =
            GlowStrength(glow, lerp(values.x, values.y, draw.alpha), age, result, released, reduceFlashing);
        // Reduce flashing dims light, never ink: an outline dimmed would read worse, and it doesn't flash.
        const float opacity = glow.opacity * (reduceFlashing && glow.blend != GLOW_BLEND_INK ? REDUCED_LIGHT_OPACITY : 1.0);
        // The instance's alpha already fades the sprite's premultiplied colour once; the glow fades with it again, so a
        // fading sprite's glow is gone before the sprite is.
        glowColour = tint * (strength * opacity * colour.a * colour.a * (1.0 - dissolve));
        if (glow.blend != GLOW_BLEND_INK) {
            glowColour.a = 0.0; // light: premultiplied alpha's blend then adds it
        }
        glowShape = float2(reach, reach * (1.0 - glow.feather));
        grow = reach * draw.texelUv;
    }
    // A ring or a glow grows the quad past the sprite's edge, by as much in world units as in UV.
    const float2 grownSize = size * (1.0 + 2.0 * grow / max(regionMax - regionMin, 1e-6));
    const float2 offset = (corner - 0.5) * grownSize;
    // Clockwise on screen: world y grows downwards.
    const float2 turned = float2(offset.x * cosine - offset.y * sine, offset.x * sine + offset.y * cosine);
    const float2 world = lerp(instance.previous, instance.current, draw.alpha) + turned;
    VertexOut output;
    output.position = float4(world * draw.worldToClip.xy + draw.worldToClip.zw, 0, 1);
    output.uv = lerp(regionMin - grow, regionMax + grow, corner);
    output.region = float4(regionMin, regionMax);
    output.ring = draw.ring;
    output.layer = instance.layerFlags & 0xFFFF;
    output.dissolve = dissolve;
    output.colour = colour;
    output.glowColour = glowColour;
    output.glowShape = glowShape;
    return output;
}
