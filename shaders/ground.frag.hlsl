#include "ground.hlsli"

// The ground field (gfx::GroundTexels()), a texel per tile: the kind index in red, the shore distance in green. The same
// texture twice: point-sampled for the kind, as indices can't be filtered, and linearly for the shore distance. Both
// clamp at the map's edge.
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float2> kindField : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState kindSampler : register(s0, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] Texture2D<float2> shoreField : register(t1, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] SamplerState shoreSampler : register(s1, space2);

#define FOG_TEXTURE_SLOT 2
#include "fog.hlsli"

ConstantBuffer<Ground> ground : register(b1, space3);

/// A texel's byte range, and how the shore byte holds a distance in tiles (gfx::ShoreByte()).
static const float BYTE_RANGE = 255.0;
static const float SHORE_CENTRE = 128.0;
static const float SHORE_BYTES_PER_TILE = 32.0;
/// The last kind index the wash table holds (core::MAX_GROUND_KINDS - 1).
static const uint LAST_KIND = 15;

/// A noise octave is whole while its cells span this many screen pixels or more, and gone at the fewer: below a couple of
/// pixels a moving camera would make it crawl.
static const float OCTAVE_WHOLE_PIXELS = 4.0;
static const float OCTAVE_GONE_PIXELS = 2.0;
/// How far each octave's lattice turns (radians) and shifts (cells) from the one before, so no lattice lines up with
/// another or with the tile grid.
static const float OCTAVE_TURN = 0.83;
static const float2 OCTAVE_SHIFT = float2(17.31, 41.77);

/// The noises' first cells in tiles, and their octaves.
static const float WARP_CELL_TILES = 1.0;
static const uint WARP_OCTAVES = 3;
static const float BLOTCH_CELL_TILES = 2.0;
static const uint BLOTCH_OCTAVES = 4;
static const float GRAIN_CELL_TILES = 0.15;
static const uint GRAIN_OCTAVES = 2;
static const uint VEIL_OCTAVES = 4;
/// Each noise's own seed; an octave adds its index.
static const uint SEED_WARP_ACROSS = 11;
static const uint SEED_WARP_DOWN = 23;
static const uint SEED_BLOTCH = 37;
static const uint SEED_GRAIN = 53;
static const uint SEED_VEIL = 71;

/// The veil shows where its noise passes this, rising to full strength over a third of the noise's range past it.
static const float VEIL_THRESHOLD = 0.45;
static const float VEIL_GAIN = 3.0;
/// How much of the land's blotchiness pools in water, whose wash reads calmer.
static const float WATER_BLOTCH = 0.28;
/// The softest edge between kinds, in tiles, so a zero softness still blends over a sliver rather than dividing by it.
static const float LEAST_SOFT = 0.02;
/// A grid line's width in world units; at zooms where that is under a pixel it widens to one, fainter by as much.
static const float GRID_LINE_UNITS = 2.0;
/// The grid is whole while its tiles span this many screen pixels or more, and gone at the fewer.
static const float GRID_WHOLE_PIXELS = 24.0;
static const float GRID_GONE_PIXELS = 12.0;

/// 0 at `low`, 1 at `high`, eased between (smoothstep), and a step at `high` when the two meet.
float Ramp(float low, float high, float value) {
    const float along = saturate((value - low) / max(high - low, 1e-6));
    return along * along * (3.0 - 2.0 * along);
}

/// A number from 0 to 1 for the lattice point `cell` of the noise `seed`, the same wherever and whenever it is drawn (a
/// PCG hash, 2D).
float LatticeValue(int2 cell, uint seed) {
    uint2 v = uint2(cell) + uint2(seed * 0x9E3779B9u, seed * 0x85EBCA6Bu);
    v = v * 1664525u + 1013904223u;
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v ^= v >> 16u;
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v ^= v >> 16u;
    return (v.x >> 8) * (1.0 / 16777216.0);
}

/// Value noise at `place`, in lattice cells: the lattice's values eased between (quintic, so its slope is smooth too).
/// From 0 to 1.
float ValueNoise(float2 place, uint seed) {
    const float2 cellFloor = floor(place);
    const float2 inCell = place - cellFloor;
    const float2 eased = inCell * inCell * inCell * (inCell * (inCell * 6.0 - 15.0) + 10.0);
    const int2 cell = int2(cellFloor);
    const float top = lerp(LatticeValue(cell, seed), LatticeValue(cell + int2(1, 0), seed), eased.x);
    const float bottom = lerp(LatticeValue(cell + int2(0, 1), seed), LatticeValue(cell + int2(1, 1), seed), eased.x);
    return lerp(top, bottom, eased.y);
}

/// Fractal value noise at `place`, in its first octave's cells: `octaves` octaves, each half the size and weight of the
/// one before, turned and shifted. `cellPixels` is the first octave's cell in screen pixels: each octave fades to its
/// mean as its cells shrink from OCTAVE_WHOLE_PIXELS to OCTAVE_GONE_PIXELS, so the noise only loses contrast as the
/// camera zooms out, never its average. From 0 to 1, a half on average.
float Fbm(float2 place, float cellPixels, uint seed, uint octaves) {
    float sum = 0.0;
    float total = 0.0;
    float weight = 1.0;
    float scale = 1.0;
    for (uint octave = 0; octave < octaves; ++octave) {
        const float angle = OCTAVE_TURN * float(octave + 1);
        const float turnCos = cos(angle);
        const float turnSin = sin(angle);
        const float2 scaled = place * scale;
        const float2 turned = float2(scaled.x * turnCos - scaled.y * turnSin, scaled.x * turnSin + scaled.y * turnCos);
        const float fade = Ramp(OCTAVE_GONE_PIXELS, OCTAVE_WHOLE_PIXELS, cellPixels / scale);
        sum += weight * fade * (ValueNoise(turned + OCTAVE_SHIFT * float(octave + 1), seed + octave) - 0.5);
        total += weight;
        weight *= 0.5;
        scale *= 2.0;
    }
    return 0.5 + sum / total;
}

/// The kind index of the tile at `tile` (whole tiles; clamped to the map).
uint KindAt(float2 tile) {
    const float2 uv = (tile + 0.5) / ground.mapSize;
    return min(uint(round(kindField.SampleLevel(kindSampler, uv, 0.0).r * BYTE_RANGE)), LAST_KIND);
}

/// The shore field's linear sample at `texel` (in texels, their centres at whole numbers).
float ShoreTexel(float2 texel) {
    return shoreField.SampleLevel(shoreSampler, (texel + 0.5) / ground.mapSize, 0.0).g;
}

/// The signed shore distance at `at` (in tiles): negative in water, the shore at 0. Filtered by a cubic B-spline over the
/// 4 × 4 tile centres about it, from four linear samples weighed and placed so the filter's own blend does the rest: a
/// linear filter alone would bend the distance's slope at every row and column of centres, and water deepening by it
/// would show those lines.
float ShoreAt(float2 at) {
    const float2 texel = at - 0.5;
    const float2 index = floor(texel);
    const float2 inside = texel - index;
    const float2 outside = 1.0 - inside;
    const float2 weight0 = outside * outside * outside / 6.0;
    const float2 weight1 = (3.0 * inside * inside * inside - 6.0 * inside * inside + 4.0) / 6.0;
    const float2 weight3 = inside * inside * inside / 6.0;
    const float2 weight2 = 1.0 - weight0 - weight1 - weight3;
    const float2 near = weight0 + weight1;
    const float2 far = weight2 + weight3;
    const float2 nearAt = index - 1.0 + weight1 / near;
    const float2 farAt = index + 1.0 + weight3 / far;
    const float sample = near.y * (near.x * ShoreTexel(nearAt) + far.x * ShoreTexel(float2(farAt.x, nearAt.y))) +
                         far.y * (near.x * ShoreTexel(float2(nearAt.x, farAt.y)) + far.x * ShoreTexel(farAt));
    return (sample * BYTE_RANGE - SHORE_CENTRE) / SHORE_BYTES_PER_TILE;
}

/// The paper washed by `wash` (rgb and strength).
float3 Washed(float4 wash) {
    return lerp(ground.paper, wash.rgb, wash.a);
}

/// The land's wash at `at` (in tiles): the four tiles whose centres surround it, each weighed by how near it is, eased
/// over `soft` tiles about the edges between them and flat inside them, so kinds bleed into each other only near their
/// edges. Water tiles are left out (the water's colour comes from the shore distance), and the paper stands in where every
/// tile is water.
float3 LandWash(float2 at, float soft) {
    const float2 corner = floor(at - 0.5);
    const float2 towards = float2(
        Ramp(0.5 - 0.5 * soft, 0.5 + 0.5 * soft, at.x - 0.5 - corner.x),
        Ramp(0.5 - 0.5 * soft, 0.5 + 0.5 * soft, at.y - 0.5 - corner.y)
    );
    const float4 weights = float4(
        (1.0 - towards.x) * (1.0 - towards.y), towards.x * (1.0 - towards.y), (1.0 - towards.x) * towards.y,
        towards.x * towards.y
    );
    const float2 offsets[4] = { float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0), float2(1.0, 1.0) };
    float3 sum = 0.0;
    float total = 0.0;
    for (uint tap = 0; tap < 4; ++tap) {
        const uint kind = KindAt(corner + offsets[tap]);
        if (kind == ground.shallowKind || kind == ground.deepKind) {
            continue;
        }
        sum += weights[tap] * Washed(ground.washes[kind]);
        total += weights[tap];
    }
    return total > 0.0 ? sum / total : ground.paper;
}

/// The water's wash at a shore distance of `shore` tiles: the shallow kind's at the shore, deepening smoothly to the deep
/// kind's `depthTiles` further out.
float3 WaterWash(float shore) {
    const float depth = Ramp(0.0, max(ground.depthTiles, 1e-3), -shore - 0.5);
    return Washed(lerp(ground.washes[ground.shallowKind], ground.washes[ground.deepKind], depth));
}

/// The painted ground at `world`, one pixel spanning `pixel` world units: washes over paper, bled across noisy edges and
/// pooled unevenly, water deepening from an inked shore, the paper's fibre and the faint grid.
float3 Painted(float2 world, float pixel) {
    const float2 tile = world / ground.tileSize;
    const float tilePixels = ground.tileSize / pixel;
    const float2 warp = (float2(
                             Fbm(tile / WARP_CELL_TILES, tilePixels * WARP_CELL_TILES, SEED_WARP_ACROSS, WARP_OCTAVES),
                             Fbm(tile / WARP_CELL_TILES, tilePixels * WARP_CELL_TILES, SEED_WARP_DOWN, WARP_OCTAVES)
                         ) - 0.5) * (2.0 * ground.edgeWarp);
    const float2 at = tile + warp;
    const float soft = clamp(ground.edgeSoft, LEAST_SOFT, 1.0);
    const float shore = ShoreAt(at);
    // Half a tile from the shore the distance is past either end of the blend, so open water and open land are whole.
    const float wet = 1.0 - Ramp(-0.5 * soft, 0.5 * soft, shore);
    float3 rgb = lerp(LandWash(at, soft), WaterWash(shore), wet);

    const float blotch = Fbm(tile / BLOTCH_CELL_TILES, tilePixels * BLOTCH_CELL_TILES, SEED_BLOTCH, BLOTCH_OCTAVES);
    rgb = ground.paper + (rgb - ground.paper) * (1.0 + ground.blotch * (blotch - 0.5) * lerp(1.0, WATER_BLOTCH, wet));

    // Never thinner than a pixel: a narrower line spreads to one, fainter by as much, so its ink holds as it moves.
    const float halfShore = 0.5 * ground.shoreWidth;
    const float spread = max(halfShore, pixel / ground.tileSize);
    const float fromShore = abs(shore) / spread;
    rgb = lerp(rgb, ground.shore, ground.shoreStrength * (halfShore / spread) * exp(-fromShore * fromShore));

    const float grain = Fbm(tile / GRAIN_CELL_TILES, tilePixels * GRAIN_CELL_TILES, SEED_GRAIN, GRAIN_OCTAVES);
    rgb *= 1.0 + 2.0 * ground.grain * (grain - 0.5);

    const float2 fromLine = abs(frac(tile + 0.5) - 0.5) * ground.tileSize;
    const float lineWidth = max(GRID_LINE_UNITS, pixel);
    const float2 cover = saturate((0.5 * lineWidth - fromLine) / pixel + 0.5);
    const float gridFade = Ramp(ground.gridFade.y, ground.gridFade.x, ground.zoom) *
                           Ramp(GRID_GONE_PIXELS, GRID_WHOLE_PIXELS, tilePixels);
    const float grid = max(cover.x, cover.y) * (GRID_LINE_UNITS / lineWidth) * gridFade;
    return lerp(rgb, ground.grid, ground.gridStrength * grid);
}

/// The veil at `world`, one pixel spanning `pixel` world units: broad pale patches where its noise is high, premultiplied.
float4 Veil(float2 world, float pixel) {
    const float cellTiles = max(ground.veilScale, 1e-3);
    const float cellPixels = cellTiles * ground.tileSize / pixel;
    const float noise = Fbm(world / (cellTiles * ground.tileSize), cellPixels, SEED_VEIL, VEIL_OCTAVES);
    const float alpha = saturate((noise - VEIL_THRESHOLD) * VEIL_GAIN) * ground.veilStrength;
    return float4(ground.veil * alpha, alpha);
}

float4 FragmentMain(GroundVertexOut input) : SV_Target0 {
    // The camera never turns, so a pixel spans the same world units both ways; taken before the mode's branch, which is
    // the same for the whole draw.
    const float pixel = max(max(fwidth(input.world.x), fwidth(input.world.y)), 1e-6);
    if (ground.mode == GROUND_MODE_PAPER) {
        return FoggedLand(ground.paper, 1.0, input.world);
    }
    if (ground.mode == GROUND_MODE_VEIL) {
        const float4 veil = Veil(input.world, pixel);
        return FoggedLand(veil.rgb, veil.a, input.world);
    }
    return FoggedLand(Painted(input.world, pixel), 1.0, input.world);
}
