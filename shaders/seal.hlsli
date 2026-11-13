// Seals (the phase 6 chunk 4b spec, "Drawing"): each layer's band and each seal's wash, laid out on the CPU at the art's
// angle (gfx/seal_draw.hpp) and drawn here, the parts of a layer as distance fields across its band; and each seal's light,
// its particles placed by SealParticleAt below from their seal's record.
// Register spaces follow SDL_GPU's order (design doc 0002 §8.8): vertex uniforms in space1, fragment resources in space2.

#include "colour.hlsli"

// gfx::SealGpuLayer (src/gfx/seal_draw.hpp), 88 bytes: one layer of a seal, or its emblem, or (for its wash) its first.
struct SealLayer {
    float2 centre;       // its centre in the world
    float2 axisX;        // one seal size along its plane's x, in the world
    float2 axisDepth;    // one seal size along its plane's depth, in the world
    float angle;         // how far it has turned, in radians
    float opacity;       // its lines' strength: opacity, life, cap and brightness
    uint colour0;        // its seal's colours, slot 0 then 1: linear premultiplied RGBA8, red in the low byte
    uint colour1;
    uint firstPart;      // its first part in the parts
    uint partCount;      // how many, sorted by where their bands start
    float inner;         // its inner radius, in seal sizes (a frame's inner polygon's corners)
    float outer;         // its outer radius (a frame's corners)
    uint frameSides;     // 0 round, else its frame's sides
    float frameRotation; // its frame's turn from its zero, in radians
    uint washColour;     // its seal's wash's ink: linear premultiplied RGBA8
    float washOpacity;   // how strongly the wash shows
    float washReach;     // how far it reaches, in seal sizes
    float washSoftness;  // how soft its edge is, 0 (hard) to 1
    float spill;         // how far its flourishes reach past its outer radius, in seal sizes
    uint reserved;       // to 88 bytes, a multiple of 8 as the float2s need
};

// gfx::SealGpuPart, 64 bytes: one part of a layer. Radii, widths and sizes in seal sizes, angles in radians.
struct SealPart {
    uint kind;        // SEAL_* below
    uint slot;        // which of its layer's colours it takes
    float radius;     // where it sits
    float width;      // its line's width
    float size;       // a tick's length, a dot's diameter, a sprite's height
    uint count;       // its ticks, dots, glyphs, flourishes, star points or polygon sides
    uint step;        // a star's step
    float dash;       // a dashed circle's dash; 0 solid
    float gap;        // and its gap
    float rotation;   // where its first item or corner is from its layer's zero
    float spill;      // how far a flourish may reach past its layer
    uint inward;      // 1 when a script's glyphs face the centre
    uint firstSprite; // its first sprite in the sprites
    uint spriteCount; // how many, drawn in turn round the ring
    uint reserved0;   // to 64 bytes
    uint reserved1;
};

// gfx::SealGpuSprite, 32 bytes: one sprite a part draws, by its distance field's block.
struct SealSprite {
    float2 fieldMin; // the block's top left, in UV of its page
    float2 fieldMax; // its bottom right
    float width;     // the sprite's size in texels, without the field's reach round it
    float height;
    uint page;       // the field array's layer
    uint reserved;   // to 32 bytes
};

// core::SealPartKind's values, then gfx::SEAL_EMBLEM_KIND.
static const uint SEAL_CIRCLE = 0;
static const uint SEAL_TICKS = 1;
static const uint SEAL_DOTS = 2;
static const uint SEAL_SCRIPT = 3;
static const uint SEAL_STAR = 4;
static const uint SEAL_POLYGON = 5;
static const uint SEAL_FLOURISH = 6;
static const uint SEAL_EMBLEM = 7;

// What the vertex stage hands the fragment stage.
struct SealOut {
    float4 position : SV_Position;
    float2 local : TEXCOORD0;                  // on the seal's plane, in seal sizes from its centre, not turned
    nointerpolation uint layer : TEXCOORD1;    // its layer in the layers
    nointerpolation uint wash : TEXCOORD2;     // 1 for a wash, 0 for a band's lines
};

// gfx::SealGpuParticles (src/gfx/seal_draw.hpp), 160 bytes: one seal's light. Each stream's age is already wrapped by its
// period on the CPU, so only small times reach here.
struct SealParticles {
    float2 centre;       // its plane's centre in the world
    float2 axisX;        // one seal size along its plane's x, in the world
    float2 axisDepth;    // one seal size along its plane's depth, in the world
    uint seed;           // a hash of its seal's key, start tick and place
    uint colour;         // its light's colour: linear premultiplied RGBA8
    float opacity;       // its light's strength: life, cap and brightness
    float size;          // its seal's size, in world units
    uint firstParticle;  // its plane's list's index of its first particle
    uint pillars;        // how many of each, in this order
    uint sparks;
    uint shards;
    float pillarAge;     // below pillarCycle, in seconds
    float pillarCycle;   // how long a pillar's light takes to run its length once
    float pillarHeight;  // world units
    float pillarWidth;   // world units
    float pillarFade;    // how much of its length a pillar fades out over towards its far end
    float sparkAge;      // below sparkLife
    float sparkLife;
    float sparkRise;     // world units a second
    float shardAge;      // below shardCycle
    float shardCycle;    // 0 when a shard neither drifts nor turns
    float shardDrift;    // world units a second
    float shardSpin;     // radians a second
    float pillarFall;    // 1: its pillars run down the screen; -1: up it
    float sparkSize;     // across, world units
    uint sparkLap;       // how many times the sparks' age has come round
    float shardSize;     // a shard's length, world units
    uint shardLap;       // likewise the shards'
    uint washColour;     // its seal's ink, under each particle's light
    float washOpacity;   // how strongly the ink shows
    float pillarRest;    // how bright a pillar is between its falling light's passes
    float pillarBand;    // how far the falling light reaches each way, a share of the pillar's length
    float shardThin;     // a shard's width over its length
    float shardEdge;     // how much of the way from a shard's middle to its points its light softens over
    uint reserved0;      // to 160 bytes
    uint reserved1;
    uint reserved2;
};

// gfx::SEAL_PILLAR, SEAL_SPARK and SEAL_SHARD.
static const uint SEAL_PILLAR = 0;
static const uint SEAL_SPARK = 1;
static const uint SEAL_SHARD = 2;

// The placement's constants: gfx::SealParticleAt() in src/gfx/seal_draw.cpp holds the same, and the two change together.
static const float SEAL_TAU = 6.28318531;
static const float SEAL_UNIT_SCALE = 1.0 / 16777216.0; // 2^-24: a unit number takes the hash's top 24 bits
static const uint SEAL_DRAW_RADIUS = 1;                // which of a particle's numbers each draw of its hash takes
static const uint SEAL_DRAW_ANGLE = 2;
static const uint SEAL_DRAW_TURN = 3;

// One particle where SealParticleAt places it (gfx::SealParticle): a quad round its centre, in world units.
struct SealParticle {
    float2 centre;
    float2 along;   // to the middle of its far edge: half its length
    float2 across;  // to the middle of its side: half its width
    float strength; // 0 to 1, before its seal's opacity
    float phase;    // a pillar's falling light, 0 at its plane to below 1 at its far end
    uint kind;      // SEAL_PILLAR, SEAL_SPARK or SEAL_SHARD
};

// A particle's own age in its stream, and the life it is in.
struct SealOwnAge {
    float age;
    uint lap;
};

// A 32-bit integer hash (lowbias32), gfx's SealHash.
uint SealHash(uint value) {
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}

// A number from 0 to below 1, draw `draw` of `hash`.
float SealUnit(uint hash, uint draw) {
    return float(SealHash(hash + draw) >> 8) * SEAL_UNIT_SCALE;
}

SealOwnAge SealStaggered(float age, float period, uint lap, uint hash) {
    SealOwnAge own;
    own.age = age;
    own.lap = lap;
    if (period > 0.0) {
        own.age = age + (SealUnit(hash, 0) * period);
        if (own.age >= period) {
            own.age -= period;
            own.lap = lap + 1;
        }
    }
    return own;
}

float2 SealOnPlane(SealParticles light, float2 local) {
    return light.centre + (light.axisX * local.x) + (light.axisDepth * local.y);
}

float SealRiseAndFall(float through) {
    return 4.0 * through * (1.0 - through);
}

float2 SealDirection(float angle) {
    float sine, cosine;
    sincos(angle, sine, cosine);
    return float2(cosine, sine);
}

// Particle `index` of `light` (its pillars, then sparks, then shards): gfx::SealParticleAt(), which documents it.
SealParticle SealParticleAt(SealParticles light, uint index) {
    const uint hash = SealHash(light.seed ^ SealHash(index));
    SealParticle particle;
    particle.phase = 0.0;
    if (index < light.pillars) {
        const float radius = sqrt(SealUnit(hash, SEAL_DRAW_RADIUS));
        const float angle = SEAL_TAU * (float(index) + SealUnit(hash, SEAL_DRAW_ANGLE)) / float(light.pillars);
        const float2 foot = SealOnPlane(light, radius * SealDirection(angle));
        const SealOwnAge own = SealStaggered(light.pillarAge, light.pillarCycle, 0, hash);
        particle.along = float2(0.0, light.pillarFall * light.pillarHeight * 0.5);
        particle.centre = foot + particle.along;
        particle.across = float2(light.pillarWidth * 0.5, 0.0);
        particle.strength = 1.0;
        particle.phase = light.pillarCycle > 0.0 ? own.age / light.pillarCycle : 0.0;
        particle.kind = SEAL_PILLAR;
    }
    else if (index < light.pillars + light.sparks) {
        const SealOwnAge own = SealStaggered(light.sparkAge, light.sparkLife, light.sparkLap, hash);
        const uint life = SealHash(hash ^ SealHash(own.lap));
        const float radius = sqrt(SealUnit(life, SEAL_DRAW_RADIUS));
        const float2 born = SealOnPlane(light, radius * SealDirection(SEAL_TAU * SealUnit(life, SEAL_DRAW_ANGLE)));
        const float halfSize = light.sparkSize * 0.5;
        particle.centre = born - float2(0.0, light.sparkRise * own.age);
        particle.along = float2(0.0, halfSize);
        particle.across = float2(halfSize, 0.0);
        particle.strength = light.sparkLife > 0.0 ? SealRiseAndFall(own.age / light.sparkLife) : 1.0;
        particle.kind = SEAL_SPARK;
    }
    else {
        const SealOwnAge own = SealStaggered(light.shardAge, light.shardCycle, light.shardLap, hash);
        const uint life = SealHash(hash ^ SealHash(own.lap));
        const float angle = SEAL_TAU * SealUnit(life, SEAL_DRAW_ANGLE);
        const float outward = sqrt(SealUnit(life, SEAL_DRAW_RADIUS)) + (light.shardDrift * own.age / light.size);
        const float turn = (SEAL_TAU * SealUnit(life, SEAL_DRAW_TURN)) + (light.shardSpin * own.age);
        const float halfSize = light.shardSize * 0.5 / light.size;
        const float2 way = SealDirection(turn);
        const bool drifts = light.shardDrift > 0.0 && light.shardCycle > 0.0;
        particle.centre = SealOnPlane(light, outward * SealDirection(angle));
        particle.along = (light.axisX * (way.x * halfSize)) + (light.axisDepth * (way.y * halfSize));
        const float thin = halfSize * light.shardThin;
        particle.across = (light.axisX * (-way.y * thin)) + (light.axisDepth * (way.x * thin));
        particle.strength = drifts ? SealRiseAndFall(own.age / light.shardCycle) : 1.0;
        particle.kind = SEAL_SHARD;
    }
    return particle;
}

// What the light's vertex stage hands its fragment stage.
struct SealLightOut {
    float4 position : SV_Position;
    float2 corner : TEXCOORD0;                 // across, then along: -1 to 1 (along: -1 at a pillar's plane)
    float4 colour : TEXCOORD1;                 // premultiplied, its strength and its seal's opacity in it
    nointerpolation uint kind : TEXCOORD2;     // SEAL_PILLAR, SEAL_SPARK or SEAL_SHARD
    nointerpolation float phase : TEXCOORD3;   // a pillar's falling light
    nointerpolation float aspect : TEXCOORD4;  // a pillar's length over its width
    nointerpolation float4 shape : TEXCOORD5;  // a pillar's fade, rest and band, a shard's edge (SealParticles)
};
