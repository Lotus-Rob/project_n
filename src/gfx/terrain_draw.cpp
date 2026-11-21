#include "gfx/terrain_draw.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/profile.hpp"
#include "core/random.hpp"
#include "core/vec2.hpp"

namespace gfx {

namespace {

using core::atlas::SpriteId;

/// The key of the map's tile variants: a hash of column and row, whichever view finds the tile. Its seed and purpose are
/// those of game/scenes/world.cpp's old noise (game can't be seen from gfx; game/random_purposes.hpp keeps the purpose, 9,
/// retired). Reduced by multiplying the hash's top half by the variant count, a two-variant kind keeps the old noise's pick
/// (the top bit).
constexpr u64 TERRAIN_SEED = 0x5eed;
constexpr u32 TERRAIN_PURPOSE = 9;
constexpr u32 VARIANT_DRAW = 2;
constexpr u32 COLUMN_SHIFT = 32;
constexpr u32 VARIANT_SHIFT = 32; // a variant comes from a random number's top half, scaled to the variant count

constexpr u32 NORTH = 1;
constexpr u32 EAST = 2;
constexpr u32 SOUTH = 4;
constexpr u32 WEST = 8;

constexpr u8 ROAD = std::to_underlying(core::TileFlag::Road);
constexpr u8 RIVER = std::to_underlying(core::TileFlag::River);
constexpr u8 BRIDGE = std::to_underlying(core::TileFlag::Bridge);
constexpr u8 FORD = std::to_underlying(core::TileFlag::Ford);

/// Where the drawing is going and what it draws with, for the passes below.
struct Pass {
    const core::WorldMap* map = nullptr;
    const core::WorldLook* look = nullptr;
    const core::Manifest* manifest = nullptr;
    core::TileSpan span{};
    TerrainDrawing* into = nullptr;

    /// `sprite` with its foot at `foot`, standing still, on the land the fog covers.
    void Add(SpriteId sprite, core::Vec2 foot) const {
        into->instances.push_back(
            core::MakeInstance(
                *manifest, { .sprite = sprite, .previous = foot, .current = foot, .flags = core::INSTANCE_FOGGED_LAND }
            )
        );
    }
    [[nodiscard]] core::Vec2 Corner(u32 column, u32 row) const {
        const f32 side = map->TileSize();
        return { .x = static_cast<f32>(column) * side, .y = static_cast<f32>(row) * side };
    }
    /// The flags of the tile at `column` and `row`, or 0 past the map: `column` and `row` may lie one outside it.
    [[nodiscard]] u8 FlagsAt(i64 column, i64 row) const {
        if (column < 0 || row < 0 || std::cmp_greater_equal(column, map->Columns()) ||
            std::cmp_greater_equal(row, map->Rows())) {
            return 0;
        }
        return map->FlagsAt(map->TileIndex({ .column = static_cast<u32>(column), .row = static_cast<u32>(row) }));
    }
    /// Which neighbours of the tile at `column` and `row` carry any of `flag`'s bits, as bits 1, 2, 4 and 8.
    [[nodiscard]] u32 Neighbours(u32 column, u32 row, u8 flag) const {
        const auto across = static_cast<i64>(column);
        const auto down = static_cast<i64>(row);
        u32 mask = 0;
        mask |= (FlagsAt(across, down - 1) & flag) != 0 ? NORTH : 0;
        mask |= (FlagsAt(across + 1, down) & flag) != 0 ? EAST : 0;
        mask |= (FlagsAt(across, down + 1) & flag) != 0 ? SOUTH : 0;
        mask |= (FlagsAt(across - 1, down) & flag) != 0 ? WEST : 0;
        return mask;
    }
    /// Calls `visit(column, row, flags)` for each tile of the span, row by row.
    template<typename Visit>
    void ForTiles(const Visit& visit) const {
        for (u32 row = span.firstRow; row <= span.lastRow; ++row) {
            for (u32 column = span.firstColumn; column <= span.lastColumn; ++column) {
                visit(column, row, map->FlagsAt(map->TileIndex({ .column = column, .row = row })));
            }
        }
    }
};

/// The variant of `variants` that the tile at `column` and `row` wears.
SpriteId VariantOf(const std::vector<core::atlas::SpriteId>& variants, u32 column, u32 row) {
    const u64 key = (u64{ column } << COLUMN_SHIFT) | row;
    const u64 pick = core::Random(TERRAIN_SEED, 0, key, TERRAIN_PURPOSE, VARIANT_DRAW) >> VARIANT_SHIFT;
    return variants[(pick * variants.size()) >> VARIANT_SHIFT];
}

void AddTiles(const Pass& pass) {
    pass.ForTiles([&](u32 column, u32 row, u8 /*flags*/) {
        const u8 kind = pass.map->KindAt(pass.map->TileIndex({ .column = column, .row = row }));
        if (kind < pass.look->kinds.size() && !pass.look->kinds[kind].variants.empty()) {
            pass.Add(VariantOf(pass.look->kinds[kind].variants, column, row), pass.Corner(column, row));
        }
    });
}

void AddLines(const Pass& pass, u8 flag, const std::array<SpriteId, core::LINE_MASKS>& sprites) {
    pass.ForTiles([&](u32 column, u32 row, u8 flags) {
        if ((flags & flag) != 0) {
            pass.Add(sprites.at(pass.Neighbours(column, row, flag)), pass.Corner(column, row));
        }
    });
}

void AddCrossings(const Pass& pass) {
    pass.ForTiles([&](u32 column, u32 row, u8 flags) {
        if ((flags & BRIDGE) != 0) {
            // Down only where the road runs north to south alone; across wherever it runs east to west, or both or neither.
            const u32 road = pass.Neighbours(column, row, ROAD);
            const bool down = (road & (NORTH | SOUTH)) != 0 && (road & (EAST | WEST)) == 0;
            pass.Add(down ? pass.look->bridgeDown : pass.look->bridgeAcross, pass.Corner(column, row));
        }
        else if ((flags & FORD) != 0) {
            pass.Add(pass.look->ford, pass.Corner(column, row));
        }
    });
}

void AddMarkers(const Pass& pass) {
    for (const core::Place& place : pass.map->Places()) {
        if (place.tile.column < pass.span.firstColumn || place.tile.column > pass.span.lastColumn ||
            place.tile.row < pass.span.firstRow || place.tile.row > pass.span.lastRow) {
            continue;
        }
        const auto* const marker = [&]() -> const SpriteId* {
            if (place.kind == core::PlaceKind::City) {
                return &pass.look->cityMarker;
            }
            const auto found =
                std::ranges::find(pass.look->dangerMarkers, place.sort, &std::pair<std::string, SpriteId>::first);
            return found == pass.look->dangerMarkers.end() ? nullptr : &found->second;
        }();
        if (marker != nullptr) {
            pass.Add(*marker, pass.map->TileCentre(pass.map->TileIndex(place.tile)));
        }
    }
}

} // namespace

void DrawTerrain(
    const core::WorldMap& map,
    const core::WorldLook& look,
    const core::Manifest& manifest,
    core::Rect view,
    TerrainDrawing& into
) {
    PN_PROFILE_ZONE();
    into.instances.clear();
    into.batches.clear();
    const core::TileSpan span = map.TilesIn(view);
    if (span.firstColumn > span.lastColumn || span.firstRow > span.lastRow) {
        return;
    }
    const Pass pass{ .map = &map, .look = &look, .manifest = &manifest, .span = span, .into = &into };
    AddTiles(pass);
    AddLines(pass, RIVER, look.rivers);
    AddLines(pass, ROAD, look.roads);
    AddCrossings(pass);
    AddMarkers(pass);
    if (!into.instances.empty()) {
        into.batches.push_back(
            { .first = 0, .count = static_cast<u32>(into.instances.size()), .pipeline = core::Pipeline::Normal }
        );
    }
}

} // namespace gfx
