// Sprites drawn as instances from a vertex-stage storage buffer (design doc 0002 §8.5).
// Register spaces follow SDL_GPU's order (§8.8): vertex resources in space0, vertex uniforms in
// space1, fragment resources in space2, fragment uniforms in space3.

struct VertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint layer : TEXCOORD1;
    float4 colour : TEXCOORD2; // premultiplied
    // An outline draw only: the region on the page, beyond which the sprite is transparent, and the
    // ring's width in UV (zero for an ordinary sprite).
    nointerpolation float4 region : TEXCOORD3;
    nointerpolation float ring : TEXCOORD4;
};
