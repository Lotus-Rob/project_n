// Sprites drawn as instances from a vertex-stage storage buffer (design doc 0002 §8.5, spike S8).
// Register spaces follow SDL_GPU's order (§8.8): vertex resources in space0, vertex uniforms in
// space1, fragment resources in space2, fragment uniforms in space3.

struct VertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
