#include "gfx/ground_field.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace gfx {

namespace {

constexpr f32 BYTE_CENTRE = 128.0F;
constexpr f32 BYTES_PER_TILE = 32.0F;
constexpr f32 BYTE_MAX = 255.0F;
constexpr f32 HALF_TILE = 0.5F;
/// How far the search looks, in tiles: past the clamp and its half tile, so a nearer tile is never missed.
constexpr i32 WINDOW = 5;
/// Every value a kind index can take, one flag each.
constexpr std::size_t KIND_VALUES = 256;
using WaterTable = std::array<bool, KIND_VALUES>;

/// The shore distance of the tile at `column`, `row` in tiles, unsigned: to the nearest tile whose wetness differs from
/// `water`, less half a tile, at most MAX_SHORE_TILES.
f32 ShoreTiles(const core::WorldMap& map, const WaterTable& isWater, u32 column, u32 row, bool water) {
    const f32 farthest = MAX_SHORE_TILES + HALF_TILE;
    f32 nearestSquared = farthest * farthest;
    const i32 lastRow = std::min(static_cast<i32>(row) + WINDOW, static_cast<i32>(map.Rows()) - 1);
    const i32 lastColumn = std::min(static_cast<i32>(column) + WINDOW, static_cast<i32>(map.Columns()) - 1);
    for (i32 otherRow = std::max(static_cast<i32>(row) - WINDOW, 0); otherRow <= lastRow; ++otherRow) {
        for (i32 otherColumn = std::max(static_cast<i32>(column) - WINDOW, 0); otherColumn <= lastColumn; ++otherColumn) {
            const u32 other = (static_cast<u32>(otherRow) * map.Columns()) + static_cast<u32>(otherColumn);
            if (isWater.at(map.KindAt(other)) == water) {
                continue;
            }
            const f32 across = static_cast<f32>(otherColumn) - static_cast<f32>(column);
            const f32 down = static_cast<f32>(otherRow) - static_cast<f32>(row);
            nearestSquared = std::min(nearestSquared, (across * across) + (down * down));
        }
    }
    return std::min(std::sqrt(nearestSquared) - HALF_TILE, MAX_SHORE_TILES);
}

} // namespace

u8 ShoreByte(f32 tiles) {
    return static_cast<u8>(std::clamp(std::round(BYTE_CENTRE + (BYTES_PER_TILE * tiles)), 0.0F, BYTE_MAX));
}

void GroundTexels(const core::WorldMap& map, std::span<const u8> waterKinds, std::vector<u8>& into) {
    WaterTable isWater{};
    for (const u8 kind : waterKinds) {
        isWater.at(kind) = true;
    }
    into.assign(std::size_t{ map.Columns() } * map.Rows() * 2, 0);
    for (u32 row = 0; row < map.Rows(); ++row) {
        for (u32 column = 0; column < map.Columns(); ++column) {
            const u32 tile = (row * map.Columns()) + column;
            const bool water = isWater.at(map.KindAt(tile));
            const f32 shore = ShoreTiles(map, isWater, column, row, water);
            const std::size_t offset = std::size_t{ tile } * 2;
            into.at(offset) = map.KindAt(tile);
            into.at(offset + 1) = ShoreByte(water ? -shore : shore);
        }
    }
}

} // namespace gfx
