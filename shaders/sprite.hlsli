// Sprites drawn as instances from a vertex-stage storage buffer (design doc 0002 §8.5).
// Register spaces follow SDL_GPU's order (§8.8): vertex resources in space0, vertex uniforms in
// space1, fragment resources in space2, fragment uniforms in space3.

struct VertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint layer : TEXCOORD1;
    float4 colour : TEXCOORD2; // premultiplied
    // The region on the page: outlines treat what lies beyond it as transparent, and a dissolve's blocks
    // start from its corner. The ring's width in UV is separate (zero for an ordinary sprite).
    nointerpolation float4 region : TEXCOORD3;
    nointerpolation float ring : TEXCOORD4;
    // How far the sprite has dissolved, from 0 (whole) to 1 (gone): core::SpriteDraw::dissolve.
    nointerpolation float dissolve : TEXCOORD5;
};
