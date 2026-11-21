#include "gfx/stamp_draw.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/profile.hpp"
#include "core/rect.hpp"
#include "core/vec2.hpp"
#include "gfx/tile_hash.hpp"

namespace gfx {

namespace {

using core::atlas::SpriteId;

constexpr u32 DENSITY_DRAW = 3;
constexpr u32 SPRITE_DRAW = 4;
constexpr u32 MIRROR_DRAW = 5;
constexpr u32 WIDTH_DRAW = 6;
constexpr u32 JITTER_X_DRAW = 7;
constexpr u32 JITTER_Y_DRAW = 8;
constexpr u32 RANK_DRAW = 9;
constexpr u32 MIST_DRAW = 10;
constexpr u32 PUFF_WIDTH_DRAW = 11;
constexpr u32 PUFF_SPRITE_DRAW = 12;

constexpr f32 HALF = 0.5F;
constexpr f32 MIRROR_CHANCE = 0.5F;
/// How far down its tile a stamp stands before its jitter: three quarters.
constexpr f32 FOOT_DOWN = 0.75F;
/// The share of `shown` one stamp's fade spans, the ranks spreading over the rest: a wide fade keeps a 1% zoom step under 0.1
/// of opacity, as smoothstep's steepest slope is 1.5 over the width and `shown` moves by up to 0.02 per 1% of zoom.
constexpr f32 FADE_SHARE = 0.3F;
/// The smoothstep polynomial 3t^2 - 2t^3.
constexpr f32 SMOOTH_CUBIC = 3.0F;
constexpr f32 SMOOTH_LINEAR = 2.0F;
constexpr f32 BYTE_RANGE = 255.0F;
constexpr u32 BYTE_BITS = 8;
constexpr u32 ALPHA_SHIFT = 3 * BYTE_BITS;

/// One thing of the first batch, with what orders it.
struct Laid {
    core::Vec2 foot{};
    u32 tile = 0;
    core::Instance instance{};
};

/// A painting: what is drawn and the box it covers.
struct Painting {
    core::Rect footprint{};
    Laid laid{};
};

/// Where the drawing is going and what it draws with.
struct Pass {
    const core::WorldMap* map = nullptr;
    const core::WorldLook* look = nullptr;
    const core::Manifest* manifest = nullptr;
    core::TileSpan span{};
    f32 zoom = 1.0F;
};

/// A sprite's width and height in world units at its own size.
core::Vec2 SizeOf(const core::Manifest& manifest, SpriteId sprite) {
    const core::SpriteInfo& info = manifest.Sprite(sprite);
    return { .x = static_cast<f32>(info.width), .y = static_cast<f32>(info.height) };
}

f32 SmoothStep(f32 low, f32 high, f32 value) {
    const f32 along = std::clamp((value - low) / (high - low), 0.0F, 1.0F);
    return along * along * (SMOOTH_CUBIC - (SMOOTH_LINEAR * along));
}

/// The premultiplied colour of `rgb` (sRGB bytes) at `alpha` from 0 to 1, red in the low byte.
u32 Premultiplied(const std::array<u8, 3>& rgb, f32 alpha) {
    const auto byte = [alpha](u8 channel) { return static_cast<u32>(std::lround(static_cast<f32>(channel) * alpha)); };
    const auto opacity = static_cast<u32>(std::lround(alpha * BYTE_RANGE));
    return byte(rgb[0]) | (byte(rgb[1]) << BYTE_BITS) | (byte(rgb[2]) << (2 * BYTE_BITS)) | (opacity << ALPHA_SHIFT);
}

/// How much of a stamp shows at the pass's zoom, 0 to 1: its own fixed rank decides when it fades as the zoom falls below
/// `thinBelow`.
f32 Opacity(const Pass& pass, const core::StampLook& stamps, u32 column, u32 row) {
    if (stamps.thinBelow <= 0.0F) {
        return 1.0F;
    }
    const f32 half = HALF * stamps.thinBelow;
    const f32 shown = std::clamp((pass.zoom - half) / half, 0.0F, 1.0F);
    const f32 rank = TileUnit(column, row, RANK_DRAW) * (1.0F - FADE_SHARE);
    return SmoothStep(rank, rank + FADE_SHARE, shown);
}

/// The sprite of `sprites` that `unit` (0 up to 1) picks.
SpriteId Pick(const std::vector<SpriteId>& sprites, f32 unit) {
    return sprites[std::min(static_cast<std::size_t>(unit * static_cast<f32>(sprites.size())), sprites.size() - 1)];
}

/// Half the longer side of the largest painting the look names, in world units: how far one reaches from its tile.
f32 LargestHalf(const Pass& pass) {
    f32 largest = 0.0F;
    const auto consider = [&](SpriteId sprite) {
        const core::Vec2 size = SizeOf(*pass.manifest, sprite);
        largest = std::max({ largest, size.x, size.y });
    };
    consider(pass.look->places.city);
    for (const auto& [key, sprite] : pass.look->places.dangers) {
        consider(sprite);
    }
    for (const auto& [key, sprite] : pass.look->places.byKey) {
        consider(sprite);
    }
    return HALF * largest;
}

/// The painting of `place`: its own by key, else its sort's, else the city's.
SpriteId PaintingOf(const core::PlaceLook& places, const core::Place& place) {
    if (const auto own = std::ranges::find(places.byKey, place.key, &std::pair<std::string, SpriteId>::first);
        own != places.byKey.end()) {
        return own->second;
    }
    if (place.kind == core::PlaceKind::Danger) {
        if (const auto sort = std::ranges::find(places.dangers, place.sort, &std::pair<std::string, SpriteId>::first);
            sort != places.dangers.end()) {
            return sort->second;
        }
    }
    return places.city;
}

/// How far, in tiles, what the tiles of a span draw can reach past them: a stamp stands up from its foot (the tallest and the
/// widest the look can make one, strayed by the jitter) and a puff round it reaches further. A stamp or puff on a tile this
/// far outside a view may still show inside it, so those tiles are laid out too.
u32 StampReach(const Pass& pass) {
    f32 tallest = 0.0F;
    f32 mostHalfWidth = 0.0F;
    f32 jitter = 0.0F;
    f32 mistWidth = 0.0F;
    for (const core::KindLook& kind : pass.look->kinds) {
        const core::StampLook& stamps = kind.stamps;
        if (stamps.sprites.empty()) {
            continue;
        }
        const f32 widest = std::max(stamps.size[0], stamps.size[1]);
        for (const SpriteId sprite : stamps.sprites) {
            const core::Vec2 size = SizeOf(*pass.manifest, sprite);
            tallest = std::max(tallest, widest * size.y / size.x);
        }
        mostHalfWidth = std::max(mostHalfWidth, HALF * widest);
        jitter = std::max(jitter, stamps.jitter);
        if (stamps.mist) {
            mistWidth = std::max(mistWidth, widest);
        }
    }
    f32 puffHalf = 0.0F;
    const core::MistLook& mist = pass.look->mist;
    for (const SpriteId sprite : mist.sprites) {
        const core::Vec2 size = SizeOf(*pass.manifest, sprite);
        const f32 width = std::max(mist.width[0], mist.width[1]) * mistWidth;
        puffHalf = std::max(puffHalf, HALF * width * std::max(1.0F, size.y / size.x));
    }
    return static_cast<u32>(std::ceil(std::max(tallest, mostHalfWidth) + jitter + puffHalf));
}

/// `span` widened by `reach` tiles on every side, kept on the map.
core::TileSpan Widened(const core::WorldMap& map, core::TileSpan span, u32 reach) {
    return { .firstColumn = span.firstColumn - std::min(span.firstColumn, reach),
             .lastColumn = std::min(span.lastColumn + reach, map.Columns() - 1),
             .firstRow = span.firstRow - std::min(span.firstRow, reach),
             .lastRow = std::min(span.lastRow + reach, map.Rows() - 1) };
}

/// The paintings of the places whose tiles lie within the largest painting's reach of the span, each centred on its tile.
std::vector<Painting> Paintings(const Pass& pass) {
    // A painting centred on its tile spans at most this many tiles each way, and a stamp's foot strays under half a tile out of
    // its own tile (the jitter is below 1 and a draw below 1), so no place past the margin reaches a foot of the span.
    const auto margin = static_cast<i64>(std::ceil(LargestHalf(pass) / pass.map->TileSize()));
    std::vector<Painting> paintings;
    for (const core::Place& place : pass.map->Places()) {
        const auto column = static_cast<i64>(place.tile.column);
        const auto row = static_cast<i64>(place.tile.row);
        if (column < i64{ pass.span.firstColumn } - margin || column > i64{ pass.span.lastColumn } + margin ||
            row < i64{ pass.span.firstRow } - margin || row > i64{ pass.span.lastRow } + margin) {
            continue;
        }
        const SpriteId sprite = PaintingOf(pass.look->places, place);
        const core::SpriteInfo& info = pass.manifest->Sprite(sprite);
        const core::Vec2 size = SizeOf(*pass.manifest, sprite);
        const u32 tile = pass.map->TileIndex(place.tile);
        const core::Vec2 centre = pass.map->TileCentre(tile);
        const core::Vec2 min{ .x = centre.x - (HALF * size.x), .y = centre.y - (HALF * size.y) };
        const core::Vec2 foot{ .x = min.x + static_cast<f32>(info.footX), .y = min.y + static_cast<f32>(info.footY) };
        paintings.push_back(
            { .footprint = { .min = min, .max = { .x = min.x + size.x, .y = min.y + size.y } },
              .laid = { .foot = foot,
                        .tile = tile,
                        .instance = core::MakeInstance(
                            *pass.manifest,
                            { .sprite = sprite, .previous = foot, .current = foot, .flags = core::INSTANCE_FOGGED_LAND }
                        ) } }
        );
    }
    return paintings;
}

/// Whether `point` lies on one of `paintings`.
bool Covered(const std::vector<Painting>& paintings, core::Vec2 point) {
    return std::ranges::any_of(paintings, [point](const Painting& painting) {
        return core::Contains(painting.footprint, point);
    });
}

/// Whether `lhs` stands behind `rhs`: farther up, then farther left, then on an earlier tile.
bool Behind(const Laid& lhs, const Laid& rhs) {
    if (const auto down = lhs.foot.y <=> rhs.foot.y; down != 0) {
        return down < 0;
    }
    if (const auto across = lhs.foot.x <=> rhs.foot.x; across != 0) {
        return across < 0;
    }
    return lhs.tile < rhs.tile;
}

/// Lays out the stamp of the tile at `column` and `row` if it has one that shows, and its mist puff if it gathers one.
void LayStamp(
    const Pass& pass,
    u32 column,
    u32 row,
    const std::vector<Painting>& paintings,
    std::vector<Laid>& things,
    std::vector<core::Instance>& puffs
) {
    const core::WorldMap& map = *pass.map;
    const u32 index = map.TileIndex({ .column = column, .row = row });
    const u8 kind = map.KindAt(index);
    if (kind >= pass.look->kinds.size()) {
        return;
    }
    const core::StampLook& stamps = pass.look->kinds[kind].stamps;
    if (stamps.sprites.empty() || TileUnit(column, row, DENSITY_DRAW) >= stamps.density) {
        return;
    }
    const f32 tile = map.TileSize();
    const f32 jitterX = stamps.jitter * ((2.0F * TileUnit(column, row, JITTER_X_DRAW)) - 1.0F);
    const f32 jitterY = HALF * stamps.jitter * ((2.0F * TileUnit(column, row, JITTER_Y_DRAW)) - 1.0F);
    const core::Vec2 foot{ .x = (static_cast<f32>(column) + HALF + jitterX) * tile,
                           .y = (static_cast<f32>(row) + FOOT_DOWN + jitterY) * tile };
    if (Covered(paintings, foot)) {
        return;
    }
    const f32 opacity = Opacity(pass, stamps, column, row);
    const u32 colour = opacity >= 1.0F ? core::OPAQUE_WHITE : Premultiplied({ { 255, 255, 255 } }, opacity);
    if ((colour >> ALPHA_SHIFT) == 0) {
        return;
    }
    const core::Manifest& manifest = *pass.manifest;
    const SpriteId sprite = Pick(stamps.sprites, TileUnit(column, row, SPRITE_DRAW));
    const f32 width = std::lerp(stamps.size[0], stamps.size[1], TileUnit(column, row, WIDTH_DRAW)) * tile;
    const bool mirrored = TileUnit(column, row, MIRROR_DRAW) < MIRROR_CHANCE;
    things.push_back(
        { .foot = foot,
          .tile = index,
          .instance = core::MakeInstance(
              manifest,
              { .sprite = sprite,
                .previous = foot,
                .current = foot,
                .scale = width / SizeOf(manifest, sprite).x,
                .colour = colour,
                .widthScale = mirrored ? -1.0F : 1.0F,
                .flags = core::INSTANCE_FOGGED_LAND }
          ) }
    );
    const core::MistLook& mist = pass.look->mist;
    if (stamps.mist && !mist.sprites.empty() && TileUnit(column, row, MIST_DRAW) < mist.chance) {
        const SpriteId puff = Pick(mist.sprites, TileUnit(column, row, PUFF_SPRITE_DRAW));
        const f32 puffWidth = std::lerp(mist.width[0], mist.width[1], TileUnit(column, row, PUFF_WIDTH_DRAW)) * width;
        puffs.push_back(
            core::MakeInstance(
                manifest,
                { .sprite = puff,
                  .previous = foot,
                  .current = foot,
                  .scale = puffWidth / SizeOf(manifest, puff).x,
                  .colour = Premultiplied(mist.colour, mist.strength * opacity),
                  .flags = core::INSTANCE_FOGGED_LAND }
            )
        );
    }
}

} // namespace

void DrawStamps(
    const core::WorldMap& map,
    const core::WorldLook& look,
    const core::Manifest& manifest,
    core::TileSpan span,
    f32 zoom,
    TerrainDrawing& into
) {
    PN_PROFILE_ZONE();
    if (span.firstColumn > span.lastColumn || span.firstRow > span.lastRow) {
        return;
    }
    const Pass reached{ .map = &map, .look = &look, .manifest = &manifest, .span = span, .zoom = zoom };
    span = Widened(map, span, StampReach(reached));
    const Pass pass{ .map = &map, .look = &look, .manifest = &manifest, .span = span, .zoom = zoom };
    const std::vector<Painting> paintings = Paintings(pass);
    std::vector<Laid> things;
    std::vector<core::Instance> puffs;
    things.reserve(
        paintings.size() + (std::size_t{ span.lastColumn - span.firstColumn + 1 } * (span.lastRow - span.firstRow + 1))
    );
    for (const Painting& painting : paintings) {
        things.push_back(painting.laid);
    }
    for (u32 row = span.firstRow; row <= span.lastRow; ++row) {
        for (u32 column = span.firstColumn; column <= span.lastColumn; ++column) {
            LayStamp(pass, column, row, paintings, things, puffs);
        }
    }
    std::ranges::sort(things, Behind);
    const auto addBatch = [&into](u32 first, std::size_t count) {
        if (count > 0) {
            into.batches.push_back({ .first = first, .count = static_cast<u32>(count), .pipeline = core::Pipeline::Normal });
        }
    };
    addBatch(static_cast<u32>(into.instances.size()), things.size());
    for (const Laid& laid : things) {
        into.instances.push_back(laid.instance);
    }
    addBatch(static_cast<u32>(into.instances.size()), puffs.size());
    into.instances.insert(into.instances.end(), puffs.begin(), puffs.end());
}

} // namespace gfx
