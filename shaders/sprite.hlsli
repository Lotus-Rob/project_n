// Sprites drawn as instances from a vertex-stage storage buffer (design doc 0002 §8.5).
// Register spaces follow SDL_GPU's order (§8.8): vertex resources in space0, vertex uniforms in
// space1, fragment resources in space2, fragment uniforms in space3.

#include "field.hlsli"

struct VertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint layer : TEXCOORD1;
    float4 colour : TEXCOORD2; // premultiplied
    // The region on the page: glows treat what lies beyond it as transparent, and a dissolve's blocks start from its
    // corner. Whether the draw is a glow-only one (1) follows.
    nointerpolation float4 region : TEXCOORD3;
    nointerpolation uint glowOnly : TEXCOORD4;
    // How far the sprite has dissolved, from 0 (whole) to 1 (gone): core::SpriteDraw::dissolve.
    nointerpolation float dissolve : TEXCOORD5;
    // The glow's colour at full coverage, premultiplied, its alpha 0 for light; all zero without a glow.
    nointerpolation float4 glowColour : TEXCOORD6;
    // How far past the sprite's edge the glow reaches and where its fade begins, in the sprite's texels; the reach is 0
    // without a glow, which keeps the field unread.
    nointerpolation float2 glowShape : TEXCOORD7;
};
