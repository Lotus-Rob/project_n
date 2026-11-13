// Seals (the phase 6 chunk 4b spec, "Drawing"): each layer's band and each seal's wash, laid out on the CPU at the art's
// angle (gfx/seal_draw.hpp) and drawn here, the parts of a layer as distance fields across its band.
// Register spaces follow SDL_GPU's order (design doc 0002 §8.8): vertex uniforms in space1, fragment resources in space2.

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
    uint partCount;      // how many, sorted by radius
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
