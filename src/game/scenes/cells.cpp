#include "game/scenes/cells.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace game {

namespace {

using core::atlas::SpriteId;

// A grid around the reference area's middle, wider than the view at the smallest zoom (gfx::MIN_ZOOM).
constexpr u32 CELL_COLUMNS = 32;
constexpr u32 CELL_ROWS = 20;
constexpr f32 GRID_CELL_SIZE = 256.0F;
constexpr core::Vec2 CELLS_MIN{ .x = 960.0F - (CELL_COLUMNS * GRID_CELL_SIZE / 2.0F),
                                .y = 540.0F - (CELL_ROWS * GRID_CELL_SIZE / 2.0F) };
constexpr std::array<SpriteId, 5> CELL_SPRITES{
    { SpriteId::CellPlain, SpriteId::CellForest, SpriteId::CellMountain, SpriteId::CellSnow, SpriteId::CellCity }
};

constexpr SceneTraits CELLS_TRAITS{
    .name = "cells",
    .view = {
        .min = CELLS_MIN,
        .max = { .x = CELLS_MIN.x + (CELL_COLUMNS * GRID_CELL_SIZE), .y = CELLS_MIN.y + (CELL_ROWS * GRID_CELL_SIZE) },
    },
};

} // namespace

const SceneTraits& CellsScene::Traits() const {
    return CELLS_TRAITS;
}

void CellsScene::Build(SceneContext& context) const {
    for (u32 row = 0; row < CELL_ROWS; ++row) {
        for (u32 column = 0; column < CELL_COLUMNS; ++column) {
            const core::Vec2 corner{ .x = CELLS_MIN.x + (static_cast<f32>(column) * GRID_CELL_SIZE),
                                     .y = CELLS_MIN.y + (static_cast<f32>(row) * GRID_CELL_SIZE) };
            // Steps of 2 by row keep equal cells from lining up in columns.
            const SpriteId sprite = CELL_SPRITES.at((column + (2 * row)) % CELL_SPRITES.size());
            context.Spawn(corner, { .sprite = sprite, .layer = core::Layer::Ground });
        }
    }
}

} // namespace game
