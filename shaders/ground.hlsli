// The world map's painted ground and its rivers: what their stages hand on, and the ground's uniforms.
// Register spaces follow SDL_GPU's order: vertex uniforms in space1, fragment resources in space2, fragment uniforms in
// space3.

/// The ground's vertex stage's output: a pixel's place in the world.
struct GroundVertexOut {
    float4 position : SV_Position;
    float2 world : TEXCOORD0;
};

/// The river's vertex stage's output (gfx::RibbonVertex, placed in world units).
struct RiverVertexOut {
    float4 position : SV_Position;
    /// Along the river from 0 to 1, then across it from 0 on one edge to 1 on the other.
    float2 uv : TEXCOORD0;
    float4 colour : TEXCOORD1; // premultiplied
    float2 world : TEXCOORD2;
};

/// What places the ground's quad: the world-to-clip transform, then the quad's corners in world units.
struct GroundPlace {
    float4 worldToClip; // clip = world * xy + zw (gfx::ClipTransform)
    float4 quad;        // left, top, right, bottom
};

/// gfx::GroundUniforms in src/gfx/ground.hpp: colours linear, lengths in tiles.
struct Ground {
    float2 mapSize; // tiles across and down
    float tileSize; // world units
    uint mode;      // GROUND_MODE_*
    float3 paper;
    float grain;
    float edgeWarp;
    float edgeSoft;
    float blotch;
    float depthTiles;
    float3 shore;
    float shoreStrength;
    float3 grid;
    float gridStrength;
    float3 veil;
    float veilStrength;
    uint shallowKind;
    uint deepKind;
    float shoreWidth;
    float veilScale;
    float2 gridFade; // the zoom where the grid is whole, then where it is gone
    float zoom;
    float padding;
    float4 washes[16]; // by kind index: rgb, then strength (core::MAX_GROUND_KINDS)
};

/// gfx::GROUND_MODE_GROUND, GROUND_MODE_VEIL, GROUND_MODE_PAPER and GROUND_MODE_CLOUD_SHADE.
static const uint GROUND_MODE_GROUND = 0;
static const uint GROUND_MODE_VEIL = 1;
static const uint GROUND_MODE_PAPER = 2;
static const uint GROUND_MODE_CLOUD_SHADE = 3;
