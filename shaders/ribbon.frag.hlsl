#include "mesh.hlsli"

// A sword's trail (gfx::RibbonVertex, through mesh.vert.hlsl in world units), drawn additively: its colour, its element's
// from the colour table, fading as (1 - u)^2 from the sword (u = 0) to the tail (u = 1), and softly across it from its
// middle (v = 1/2) to nothing at its edges, so a trail is a streak of light with no hard rim.
float4 FragmentMain(VertexOut input) : SV_Target0 {
    const float along = 1.0 - input.uv.x;
    const float across = 1.0 - abs(input.uv.y * 2.0 - 1.0);
    return input.colour * (along * along) * smoothstep(0.0, 1.0, across);
}
