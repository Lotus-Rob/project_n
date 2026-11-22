#include "gfx/terrain_draw.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/profile.hpp"
#include "core/vec2.hpp"
#include "gfx/stamp_draw.hpp"

namespace gfx {

namespace {

using core::atlas::SpriteId;

constexpr u8 BRIDGE = std::to_underlying(core::TileFlag::Bridge);
constexpr u8 FORD = std::to_underlying(core::TileFlag::Ford);

/// Where the drawing is going and what it draws with, for the passes below.
struct Pass {
    const core::WorldMap* map = nullptr;
    const core::WorldLook* look = nullptr;
    const core::Manifest* manifest = nullptr;
    std::span<const std::vector<core::Vec2>> roadCurves;
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

/// Which way the road curve nearest `point` runs there: from the curve point before the nearest to the one after it. Zero
/// when no curve has a point.
core::Vec2 HeadingNear(std::span<const std::vector<core::Vec2>> curves, core::Vec2 point) {
    core::Vec2 heading{};
    f32 nearest = std::numeric_limits<f32>::infinity();
    for (const std::vector<core::Vec2>& curve : curves) {
        for (std::size_t index = 0; index < curve.size(); ++index) {
            const core::Vec2 offset = curve[index] - point;
            const f32 distance = (offset.x * offset.x) + (offset.y * offset.y);
            if (distance < nearest) {
                nearest = distance;
                heading = curve[std::min(index + 1, curve.size() - 1)] - curve[index > 0 ? index - 1 : 0];
            }
        }
    }
    return heading;
}

void AddCrossings(const Pass& pass) {
    pass.ForTiles([&](u32 column, u32 row, u8 flags) {
        if ((flags & BRIDGE) != 0) {
            // The ribbon decides, not the tiles: a road rasterised on a slant steps sideways onto a bridge it crosses
            // nearly straight on, and the deck must lie the way the drawn road does.
            const core::Vec2 heading =
                HeadingNear(pass.roadCurves, pass.map->TileCentre(pass.map->TileIndex({ .column = column, .row = row })));
            const bool down = std::abs(heading.x) < std::abs(heading.y);
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
    std::span<const std::vector<core::Vec2>> roadCurves,
    const core::Manifest& manifest,
    core::Rect view,
    f32 zoom,
    TerrainDrawing& into
) {
    PN_PROFILE_ZONE();
    into.instances.clear();
    into.batches.clear();
    into.markersFrom = 0;
    const core::TileSpan span = map.TilesIn(view);
    if (span.firstColumn > span.lastColumn || span.firstRow > span.lastRow) {
        return;
    }
    const Pass pass{ .map = &map, .look = &look, .manifest = &manifest, .roadCurves = roadCurves, .span = span, .into = &into };
    AddCrossings(pass);
    CloseBatch(into);
    DrawStamps(map, look, manifest, span, zoom, into);
    into.markersFrom = static_cast<u32>(into.batches.size());
    AddMarkers(pass);
    CloseBatch(into);
}

} // namespace gfx
