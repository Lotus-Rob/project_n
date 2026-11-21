#include "gfx/terrain_draw.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/profile.hpp"
#include "core/vec2.hpp"
#include "gfx/stamp_draw.hpp"

namespace gfx {

namespace {

using core::atlas::SpriteId;

constexpr u32 NORTH = 1;
constexpr u32 EAST = 2;
constexpr u32 SOUTH = 4;
constexpr u32 WEST = 8;

constexpr u8 ROAD = std::to_underlying(core::TileFlag::Road);
constexpr u8 BRIDGE = std::to_underlying(core::TileFlag::Bridge);
constexpr u8 FORD = std::to_underlying(core::TileFlag::Ford);

/// Where the drawing is going and what it draws with, for the passes below.
struct Pass {
    const core::WorldMap* map = nullptr;
    const core::WorldLook* look = nullptr;
    const core::Manifest* manifest = nullptr;
    core::TileSpan span{};
    TerrainDrawing* into = nullptr;

    /// `sprite` with its foot at `foot`, standing still, with the fog flags `flags`.
    void Add(SpriteId sprite, core::Vec2 foot, u32 flags = core::INSTANCE_FOGGED_LAND) const {
        into->instances.push_back(
            core::MakeInstance(*manifest, { .sprite = sprite, .previous = foot, .current = foot, .flags = flags })
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

/// Closes a batch of the instances added since the last batch ended, if there are any.
void CloseBatch(TerrainDrawing& into) {
    const u32 first = into.batches.empty() ? 0 : into.batches.back().first + into.batches.back().count;
    const auto end = static_cast<u32>(into.instances.size());
    if (end > first) {
        into.batches.push_back({ .first = first, .count = end - first, .pipeline = core::Pipeline::Normal });
    }
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
            pass.Add(*marker, pass.map->TileCentre(pass.map->TileIndex(place.tile)), core::INSTANCE_FOG_SHADE);
        }
    }
}

} // namespace

void DrawTerrain(
    const core::WorldMap& map,
    const core::WorldLook& look,
    const core::Manifest& manifest,
    core::Rect view,
    f32 zoom,
    TerrainDrawing& into
) {
    PN_PROFILE_ZONE();
    into.instances.clear();
    into.batches.clear();
    into.roadBatches = 0;
    into.markersFrom = 0;
    const core::TileSpan span = map.TilesIn(view);
    if (span.firstColumn > span.lastColumn || span.firstRow > span.lastRow) {
        return;
    }
    const Pass pass{ .map = &map, .look = &look, .manifest = &manifest, .span = span, .into = &into };
    AddLines(pass, ROAD, look.roads);
    CloseBatch(into);
    into.roadBatches = static_cast<u32>(into.batches.size());
    AddCrossings(pass);
    CloseBatch(into);
    DrawStamps(map, look, manifest, span, zoom, into);
    into.markersFrom = static_cast<u32>(into.batches.size());
    AddMarkers(pass);
    CloseBatch(into);
}

} // namespace gfx
