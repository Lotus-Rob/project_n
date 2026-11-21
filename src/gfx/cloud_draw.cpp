#include "gfx/cloud_draw.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/math.hpp"
#include "core/profile.hpp"
#include "core/random.hpp"
#include "core/vec2.hpp"

namespace gfx {

namespace {

/// A level-0 cell's side in tiles: about two clouds across a tile close up.
constexpr f32 FINEST_CELL = 0.5F;
/// Past this level (a zoom under 1/256) the clouds stop growing; no view reaches it.
constexpr f32 TOP_LEVEL = 8.0F;
/// Where between two levels their cross-fade starts, and over how much of the step it runs.
constexpr f32 FADE_START = 0.35F;
constexpr f32 FADE_SPAN = 0.3F;
/// Below one step of an 8-bit alpha nothing shows, so neither it nor anything it would hand on is laid out.
constexpr f32 LEAST_OPACITY = 1.0F / 255.0F;
constexpr f32 BYTE_RANGE = 255.0F;
constexpr u32 GREY = 0x0101'0101U; // a byte in every channel: premultiplied white at that alpha

/// Where a cell's two fixed clouds stand, as fractions of the cell: staggered like bricks, as a cloud is wider than tall.
constexpr std::array<core::Vec2, 2> ANCHORS{ { { .x = 0.32F, .y = 0.28F }, { .x = 0.68F, .y = 0.76F } } };
/// A third cloud, in half the cells, about the middle by up to this much either way.
constexpr core::Vec2 THIRD_CENTRE{ .x = 0.5F, .y = 0.5F };
constexpr core::Vec2 THIRD_SPREAD{ .x = 0.6F, .y = 0.4F };
constexpr f32 THIRD_CHANCE = 0.5F;
/// How far a cloud strays from its anchor, as the whole range of its jitter in cells.
constexpr core::Vec2 JITTER{ .x = 0.45F, .y = 0.35F };
/// A cloud's width over the look's base: 0.85 to 1.45, so neighbours always overlap and no gap opens.
constexpr f32 WIDTH_LEAST = 0.85F;
constexpr f32 WIDTH_SPREAD = 0.6F;
constexpr f32 FLIP_CHANCE = 0.5F;
constexpr f64 TURN = 2.0 * std::numbers::pi;
constexpr f32 TURN_F = 2.0F * std::numbers::pi_v<f32>;

/// The clouds' draws: their own seed, and a purpose for each thing drawn, so no two share numbers.
constexpr u64 CLOUD_SEED = 0xC10D'5EEDULL;
constexpr u32 THIRD = 1;
constexpr u32 THIRD_X = 2;
constexpr u32 THIRD_Y = 3;
constexpr u32 JITTER_X = 4;
constexpr u32 JITTER_Y = 5;
constexpr u32 WIDTH = 6;
constexpr u32 SPRITE = 7;
constexpr u32 FLIP = 8;
constexpr u32 PHASE = 9;
constexpr u32 DRIFT = 10;
constexpr u32 ORDER = 11;
constexpr u32 COLUMN_SHIFT = 32;

constexpr std::size_t LEFT = 0;
constexpr std::size_t RIGHT = 1;
constexpr std::size_t TOP = 2;
constexpr std::size_t BOTTOM = 3;
constexpr std::size_t SIDES = 4;

/// One cell of the lattice to visit, and how much of its clouds' opacity reaches it from above.
struct Cell {
    u32 column = 0;
    u32 row = 0;
    u32 level = 0;
    f32 reach = 0.0F;
};

/// A cell's tiles, first column and row included, last excluded.
struct Tiles {
    i64 column = 0;
    i64 row = 0;
    i64 endColumn = 0;
    i64 endRow = 0;
};

/// A cloud laid out, waiting to be ordered: where it rests (before its bob and drift) orders it.
struct Cloud {
    core::Instance instance{};
    core::Vec2 rest{};
    u64 order = 0;
};

/// A box in world units.
struct Box {
    f32 left = 0.0F;
    f32 right = 0.0F;
    f32 top = 0.0F;
    f32 bottom = 0.0F;
};

/// What laying the clouds out reads, and where it writes them.
struct Pass {
    const FogFrame* fog = nullptr;
    const core::FogLook* look = nullptr;
    const core::Manifest* manifest = nullptr;
    /// How far through its bob a cloud that starts at rest is, in turns.
    f64 bobTurn = 0.0;
    std::vector<Cloud>* clouds = nullptr;
};

/// The explored value of the tile at `column` and `row`, 0 to 1; tiles off the map count as explored.
f32 Explored(const FogFrame& fog, i64 column, i64 row) {
    if (column < 0 || row < 0 || column >= i64{ fog.columns } || row >= i64{ fog.rows }) {
        return 1.0F;
    }
    const std::size_t index =
        (((static_cast<std::size_t>(row) * fog.columns) + static_cast<std::size_t>(column)) * 2) + FOG_EXPLORED;
    return index < fog.values.size() ? static_cast<f32>(fog.values[index]) / BYTE_RANGE : 1.0F;
}

/// Whether any tile from `tiles`' first column and row up to (not including) its ends is explored at all or off the map.
bool AnyExplored(const FogFrame& fog, const Tiles& tiles) {
    for (i64 row = tiles.row; row < tiles.endRow; ++row) {
        for (i64 column = tiles.column; column < tiles.endColumn; ++column) {
            if (Explored(fog, column, row) > 0.0F) {
                return true;
            }
        }
    }
    return false;
}

/// The tiles a cell touches: a level-0 cell lies within one tile, a level-l cell above it covers 2^(l-1) tiles a side.
Tiles TilesOf(const Cell& cell) {
    if (cell.level == 0) {
        const i64 column = cell.column >> 1U;
        const i64 row = cell.row >> 1U;
        return { .column = column, .row = row, .endColumn = column + 1, .endRow = row + 1 };
    }
    const i64 side = i64{ 1 } << (cell.level - 1);
    return { .column = cell.column * side,
             .row = cell.row * side,
             .endColumn = (cell.column + i64{ 1 }) * side,
             .endRow = (cell.row + i64{ 1 }) * side };
}

/// The cloud's hash for `purpose`: of its cell's column, row and level and its anchor, whichever view finds the cell.
u64 Draw(const Cell& cell, u32 anchor, u32 purpose) {
    const u64 key = (u64{ cell.column } << COLUMN_SHIFT) | cell.row;
    return core::Random(CLOUD_SEED, cell.level, key, purpose, anchor);
}

/// Draw() as a number in [0, 1).
f32 Unit(const Cell& cell, u32 anchor, u32 purpose) {
    return static_cast<f32>(core::ToUnit(Draw(cell, anchor, purpose)));
}

/// The tiles beyond `side` of `own` that `box` reaches into.
Tiles Beyond(const Tiles& own, const Box& box, f32 tile, std::size_t side) {
    const auto floorTile = [tile](f32 edge) { return static_cast<i64>(std::floor(edge / tile)); };
    const auto ceilTile = [tile](f32 edge) { return static_cast<i64>(std::ceil(edge / tile)); };
    if (side == LEFT || side == RIGHT) {
        const i64 top = floorTile(box.top);
        const i64 bottom = ceilTile(box.bottom);
        return side == LEFT ? Tiles{ .column = floorTile(box.left), .row = top, .endColumn = own.column, .endRow = bottom }
                            : Tiles{ .column = own.endColumn, .row = top, .endColumn = ceilTile(box.right), .endRow = bottom };
    }
    const i64 left = floorTile(box.left);
    const i64 right = ceilTile(box.right);
    return side == TOP ? Tiles{ .column = left, .row = floorTile(box.top), .endColumn = right, .endRow = own.row }
                       : Tiles{ .column = left, .row = own.endRow, .endColumn = right, .endRow = ceilTile(box.bottom) };
}

/// Where a cloud stands and how large, and how far its sprite reaches from its foot each way at scale 1.
struct Fit {
    core::Vec2 foot{};
    f32 scale = 1.0F;
    Box reach{};
    /// How far it bobs either way, which its box keeps room for.
    f32 bob = 0.0F;
};

/// The box `fit`'s cloud covers, with room above and below for its bob.
Box BoxOf(const Fit& fit) {
    return { .left = fit.foot.x - (fit.reach.left * fit.scale),
             .right = fit.foot.x + (fit.reach.right * fit.scale),
             .top = fit.foot.y - (fit.reach.top * fit.scale) - fit.bob,
             .bottom = fit.foot.y + (fit.reach.bottom * fit.scale) + fit.bob };
}

/// The cloud's box with the sides in `closed` kept inside `own`: shrunk first about its foot only as far as two closed
/// sides facing each other need, then moved in.
Fit Settle(Fit fit, const Box& own, const std::array<bool, SIDES>& closed) {
    const f32 wide = fit.reach.left + fit.reach.right;
    const f32 high = fit.reach.top + fit.reach.bottom;
    if (closed[LEFT] && closed[RIGHT] && wide * fit.scale > own.right - own.left) {
        fit.scale = (own.right - own.left) / wide;
    }
    const f32 room = std::max(0.0F, own.bottom - own.top - (2.0F * fit.bob));
    if (closed[TOP] && closed[BOTTOM] && high * fit.scale > room) {
        fit.scale = std::min(fit.scale, room / high);
    }
    const Box box = BoxOf(fit);
    if (closed[LEFT] && box.left < own.left) {
        fit.foot.x += own.left - box.left;
    }
    else if (closed[RIGHT] && box.right > own.right) {
        fit.foot.x -= box.right - own.right;
    }
    if (closed[TOP] && box.top < own.top) {
        fit.foot.y += own.top - box.top;
    }
    else if (closed[BOTTOM] && box.bottom > own.bottom) {
        fit.foot.y -= box.bottom - own.bottom;
    }
    return fit;
}

/// The cloud of `fit` placed so its box, bob included, never reaches past its cell's tiles into a tile that is explored at
/// all or off the map. A side closes once its box would reach such a tile; settling again with it closed can push the box
/// further out on another side, so this repeats until no side closes, at most once for each.
Fit Clamp(const FogFrame& fog, const Tiles& own, const Fit& start) {
    const f32 tile = fog.tileSize;
    const Box ownBox{ .left = static_cast<f32>(own.column) * tile,
                      .right = static_cast<f32>(own.endColumn) * tile,
                      .top = static_cast<f32>(own.row) * tile,
                      .bottom = static_cast<f32>(own.endRow) * tile };
    std::array<bool, SIDES> closed{};
    Fit fit = start;
    for (std::size_t round = 0; round <= SIDES; ++round) {
        fit = Settle(start, ownBox, closed);
        const Box box = BoxOf(fit);
        const std::array<bool, SIDES> over{
            (box.left < ownBox.left), (box.right > ownBox.right), (box.top < ownBox.top), (box.bottom > ownBox.bottom)
        };
        bool closing = false;
        for (std::size_t side = 0; side < SIDES; ++side) {
            if (!closed.at(side) && over.at(side) && AnyExplored(fog, Beyond(own, box, tile, side))) {
                closed.at(side) = true;
                closing = true;
            }
        }
        if (!closing) {
            break;
        }
    }
    return fit;
}

/// Adds the clouds of `cell` at `opacity`, `explored` being the largest explored value of its tiles.
void PlaceClouds(const Pass& pass, const Cell& cell, f32 opacity, f32 explored) {
    const FogFrame& fog = *pass.fog;
    const core::FogLook& look = *pass.look;
    const f32 side = FINEST_CELL * std::ldexp(fog.tileSize, static_cast<i32>(cell.level));
    const core::Vec2 origin{ .x = static_cast<f32>(cell.column) * side, .y = static_cast<f32>(cell.row) * side };
    const Tiles own = TilesOf(cell);
    const u32 count = Unit(cell, 0, THIRD) < THIRD_CHANCE ? 3 : 2;
    const u32 colour = static_cast<u32>(std::lround(std::clamp(opacity, 0.0F, 1.0F) * BYTE_RANGE)) * GREY;
    for (u32 anchor = 0; anchor < count; ++anchor) {
        const core::Vec2 anchorAt =
            anchor < ANCHORS.size()
                ? ANCHORS.at(anchor)
                : core::Vec2{ .x = THIRD_CENTRE.x + ((Unit(cell, anchor, THIRD_X) - 0.5F) * THIRD_SPREAD.x),
                              .y = THIRD_CENTRE.y + ((Unit(cell, anchor, THIRD_Y) - 0.5F) * THIRD_SPREAD.y) };
        const core::Vec2 jitter{ .x = (Unit(cell, anchor, JITTER_X) - 0.5F) * JITTER.x,
                                 .y = (Unit(cell, anchor, JITTER_Y) - 0.5F) * JITTER.y };
        const f32 width = look.cloudWidth * side * (WIDTH_LEAST + (WIDTH_SPREAD * Unit(cell, anchor, WIDTH)));
        const core::atlas::SpriteId sprite =
            look.clouds[core::ToBelow(Draw(cell, anchor, SPRITE), static_cast<u32>(look.clouds.size()))];
        const bool flip = Unit(cell, anchor, FLIP) < FLIP_CHANCE;
        const core::SpriteInfo& info = pass.manifest->Sprite(sprite);
        if (info.width == 0) {
            continue;
        }
        const auto footX = static_cast<f32>(info.footX);
        const auto toRight = static_cast<f32>(info.width) - footX;
        const Fit start{ .foot =
                             origin + core::Vec2{ .x = (anchorAt.x + jitter.x) * side, .y = (anchorAt.y + jitter.y) * side },
                         .scale = width / static_cast<f32>(info.width),
                         .reach = { .left = flip ? toRight : footX,
                                    .right = flip ? footX : toRight,
                                    .top = static_cast<f32>(info.footY),
                                    .bottom = static_cast<f32>(info.height) - static_cast<f32>(info.footY) },
                         .bob = look.bobAmplitude * side };
        const Fit fit = Clamp(fog, own, start);
        const f64 turn = pass.bobTurn + static_cast<f64>(Unit(cell, anchor, PHASE));
        const f32 bob = fit.bob * static_cast<f32>(core::math::Sin(TURN * (turn - std::floor(turn))));
        const f32 heading = TURN_F * Unit(cell, anchor, DRIFT);
        const f32 drift = look.drift * explored * fog.tileSize;
        const core::Vec2 foot =
            fit.foot + core::Vec2{ .x = drift * core::math::Cos(heading), .y = (drift * core::math::Sin(heading)) + bob };
        pass.clouds->push_back(
            { .instance = core::MakeInstance(
                  *pass.manifest,
                  { .sprite = sprite,
                    .previous = foot,
                    .current = foot,
                    .scale = fit.scale,
                    .colour = colour,
                    .widthScale = flip ? -1.0F : 1.0F }
              ),
              .rest = fit.foot,
              .order = Draw(cell, anchor, ORDER) }
        );
    }
}

/// The least and the largest explored value of a cell's tiles.
struct Spread {
    f32 least = 1.0F;
    f32 most = 0.0F;
};

/// The spread of `tiles`' explored values, tiles off the map counting as explored.
Spread SpreadOf(const FogFrame& fog, const Tiles& tiles) {
    Spread spread;
    for (i64 row = tiles.row; row < tiles.endRow; ++row) {
        for (i64 column = tiles.column; column < tiles.endColumn; ++column) {
            const f32 explored = Explored(fog, column, row);
            spread.most = std::max(spread.most, explored);
            spread.least = std::min(spread.least, explored);
        }
    }
    return spread;
}

/// Adds the clouds `top` and the cells under it draw: each draws what its tiles leave covered and hands the rest of what
/// reaches it to its four children. `pending` is scratch space, empty before and after.
void Descend(const Pass& pass, const Cell& top, std::vector<Cell>& pending) {
    pending.push_back(top);
    while (!pending.empty()) {
        const Cell cell = pending.back();
        pending.pop_back();
        const Spread spread = SpreadOf(*pass.fog, TilesOf(cell));
        if (spread.least >= 1.0F) {
            continue; // every tile under it is explored, so no cell below draws
        }
        const f32 drawn = cell.reach * (1.0F - spread.most);
        if (drawn >= LEAST_OPACITY) {
            PlaceClouds(pass, cell, drawn, spread.most);
        }
        const f32 handed = cell.reach * spread.most;
        if (cell.level > 0 && handed >= LEAST_OPACITY) {
            for (u32 child = 0; child < 4; ++child) {
                pending.push_back(
                    { .column = (cell.column * 2) + (child & 1U),
                      .row = (cell.row * 2) + (child >> 1U),
                      .level = cell.level - 1,
                      .reach = handed }
                );
            }
        }
    }
}

/// Adds the clouds that the cells of `level` in `view` (and a cell round it) and their children draw, at `opacity`.
void Lay(const Pass& pass, core::Rect view, u32 level, f32 opacity) {
    if (opacity < LEAST_OPACITY) {
        return;
    }
    const FogFrame& fog = *pass.fog;
    const f32 side = FINEST_CELL * std::ldexp(fog.tileSize, static_cast<i32>(level));
    const f32 tilesPerCell = FINEST_CELL * std::ldexp(1.0F, static_cast<i32>(level));
    // Only cells on the map: one wholly off it touches no tile that isn't explored.
    const f32 lastColumn = std::ceil(static_cast<f32>(fog.columns) / tilesPerCell) - 1.0F;
    const f32 lastRow = std::ceil(static_cast<f32>(fog.rows) / tilesPerCell) - 1.0F;
    const f32 firstX = std::max(0.0F, std::floor(view.min.x / side) - 1.0F);
    const f32 firstY = std::max(0.0F, std::floor(view.min.y / side) - 1.0F);
    const f32 endX = std::min(lastColumn, std::floor(view.max.x / side) + 1.0F);
    const f32 endY = std::min(lastRow, std::floor(view.max.y / side) + 1.0F);
    if (firstX > endX || firstY > endY) {
        return;
    }
    std::vector<Cell> pending;
    for (auto row = static_cast<u32>(firstY); row <= static_cast<u32>(endY); ++row) {
        for (auto column = static_cast<u32>(firstX); column <= static_cast<u32>(endX); ++column) {
            Descend(pass, { .column = column, .row = row, .level = level, .reach = opacity }, pending);
        }
    }
}

} // namespace

f32 CloudLevel(f32 zoom) {
    return std::max(0.0F, std::log2(1.0F / zoom));
}

void DrawClouds(
    const FogFrame& fog,
    const core::FogLook& look,
    const core::Manifest& manifest,
    core::Rect view,
    f32 zoom,
    f64 seconds,
    CloudDrawing& into
) {
    PN_PROFILE_ZONE();
    into.instances.clear();
    into.batches.clear();
    if (fog.columns == 0 || fog.rows == 0 || !(fog.tileSize > 0.0F) || look.clouds.empty() || !(zoom > 0.0F)) {
        return;
    }
    const f32 level = std::min(CloudLevel(zoom), TOP_LEVEL);
    const f32 lower = std::floor(level);
    const f32 blend = std::clamp((level - lower - FADE_START) / FADE_SPAN, 0.0F, 1.0F);
    const f64 cycles = look.bobPeriod > 0.0F ? seconds / static_cast<f64>(look.bobPeriod) : 0.0;
    std::vector<Cloud> clouds;
    const Pass pass{
        .fog = &fog, .look = &look, .manifest = &manifest, .bobTurn = cycles - std::floor(cycles), .clouds = &clouds
    };
    const auto lowerLevel = static_cast<u32>(lower);
    Lay(pass, view, lowerLevel, 1.0F - blend);
    if (blend > 0.0F) {
        Lay(pass, view, lowerLevel + 1, blend);
    }
    // The hash breaks ties, so the order never hangs on which cell the view reached first.
    std::ranges::sort(clouds, [](const Cloud& lhs, const Cloud& rhs) {
        if (lhs.rest.y < rhs.rest.y || rhs.rest.y < lhs.rest.y) {
            return lhs.rest.y < rhs.rest.y;
        }
        if (lhs.rest.x < rhs.rest.x || rhs.rest.x < lhs.rest.x) {
            return lhs.rest.x < rhs.rest.x;
        }
        return lhs.order < rhs.order;
    });
    const std::size_t dropped = clouds.size() > MAX_CLOUD_INSTANCES ? clouds.size() - MAX_CLOUD_INSTANCES : 0;
    into.instances.reserve(clouds.size() - dropped);
    for (std::size_t cloud = dropped; cloud < clouds.size(); ++cloud) {
        into.instances.push_back(clouds[cloud].instance);
    }
    if (!into.instances.empty()) {
        into.batches.push_back(
            { .first = 0, .count = static_cast<u32>(into.instances.size()), .pipeline = core::Pipeline::Normal }
        );
    }
}

} // namespace gfx
