#include "seal.hlsli"

// How a seal's particles shine across their quads: each fades to nothing at its quad's edges, so none has a hard edge to
// crawl. The same shapes draw the particles' ink (normal blending) and then their light (additive). Their proportions are
// each look's (seals.yaml's light), handed over in `shape`.

// How much of a pillar's light shows at `corner` (across, along from its plane).
float Pillar(SealLightOut input) {
    const float fade = input.shape.x;
    const float rest = input.shape.y;
    const float band = input.shape.z;
    const float across = saturate(1.0 - (input.corner.x * input.corner.x));
    const float along = (input.corner.y * 0.5) + 0.5; // 0 at its plane, 1 at its far end
    // It begins softly over its own width from its plane, and fades out over its fade towards its far end.
    const float near = saturate(along * input.aspect);
    const float far = fade > 0.0 ? saturate((1.0 - along) / fade) : 1.0;
    // The falling light, wrapping round so it leaves the far end as it comes in at the plane.
    const float away = abs(frac(along - input.phase + 0.5) - 0.5) / band;
    const float falling = rest + ((1.0 - rest) * exp(-(away * away)));
    return across * across * near * far * falling;
}

float4 FragmentMain(SealLightOut input) : SV_Target {
    float shape;
    if (input.kind == SEAL_SPARK) {
        // A soft round spark, brightest at its middle, fading to nothing at its edge.
        shape = saturate(1.0 - dot(input.corner, input.corner));
    }
    else if (input.kind == SEAL_SHARD) {
        // A diamond sliver, its points along its length and across it: full inside, softening over its edge's share of
        // the way out to them.
        shape = saturate((1.0 - (abs(input.corner.x) + abs(input.corner.y))) / input.shape.w);
    }
    else {
        shape = Pillar(input);
    }
    return input.colour * shape;
}
