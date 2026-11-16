// The mesh path: textured, coloured triangles the main thread places in target pixels.
// Register spaces follow SDL_GPU's order: vertex uniforms in space1, fragment resources in space2,
// fragment uniforms in space3.

struct VertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 colour : TEXCOORD1; // premultiplied
};
