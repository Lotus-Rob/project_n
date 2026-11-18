#include "game/scenes/painted.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace game {

namespace {

using core::atlas::SpriteId;

constexpr SceneTraits PAINTED_TRAITS{ .name = "painted", .view = GROUND, .devScene = true };

struct PaintedSprite {
    SpriteId sprite = SpriteId{};
    core::Vec2 foot{};
};

constexpr std::array PAINTED_SPRITES{
    PaintedSprite{ .sprite = SpriteId::TreeDead, .foot = { .x = 260.0F, .y = 460.0F } },
    PaintedSprite{ .sprite = SpriteId::Crane, .foot = { .x = 620.0F, .y = 280.0F } },
    PaintedSprite{ .sprite = SpriteId::Hut, .foot = { .x = 1640.0F, .y = 420.0F } },
    PaintedSprite{ .sprite = SpriteId::Stele, .foot = { .x = 500.0F, .y = 720.0F } },
    PaintedSprite{ .sprite = SpriteId::TreantBlighted, .foot = { .x = 960.0F, .y = 640.0F } },
    PaintedSprite{ .sprite = SpriteId::CrystalIce, .foot = { .x = 1380.0F, .y = 680.0F } },
    PaintedSprite{ .sprite = SpriteId::FoxSpirit, .foot = { .x = 1640.0F, .y = 900.0F } },
    PaintedSprite{ .sprite = SpriteId::WispRed, .foot = { .x = 240.0F, .y = 960.0F } },
    PaintedSprite{ .sprite = SpriteId::WispRed,
                   .foot = { .x = 300.0F, .y = 980.0F } }, // overlaps: semi-transparent ink over ink
    PaintedSprite{ .sprite = SpriteId::FoxYellow, .foot = { .x = 740.0F, .y = 930.0F } },
};
constexpr std::array PAINTED_GLOWS{
    PaintedSprite{ .sprite = SpriteId::OrbGold, .foot = { .x = 960.0F, .y = 430.0F } }, // over the treant's dark branches
    PaintedSprite{ .sprite = SpriteId::OrbBlue, .foot = { .x = 1120.0F, .y = 900.0F } },
    PaintedSprite{ .sprite = SpriteId::OrbRed, .foot = { .x = 1160.0F, .y = 900.0F } }, // overlaps: glows add
    PaintedSprite{ .sprite = SpriteId::OrbGold, .foot = { .x = 1140.0F, .y = 930.0F } },
    PaintedSprite{ .sprite = SpriteId::OrbBlue, .foot = { .x = 1380.0F, .y = 600.0F } }, // over the ice
};

} // namespace

const SceneTraits& PaintedScene::Traits() const {
    return PAINTED_TRAITS;
}

void PaintedScene::Build(SceneContext& context) const {
    for (const PaintedSprite& prop : PAINTED_SPRITES) {
        context.Spawn(prop.foot, { .sprite = prop.sprite, .shadow = true });
    }
    for (const PaintedSprite& glow : PAINTED_GLOWS) {
        context.Spawn(
            glow.foot, { .sprite = glow.sprite, .layer = core::Layer::Effects, .pipeline = core::Pipeline::Additive }
        );
    }
}

} // namespace game
