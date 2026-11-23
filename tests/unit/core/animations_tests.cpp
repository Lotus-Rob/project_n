#include "core/animations.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

#include <doctest/doctest.h>

namespace {

using core::atlas::SpriteId;

// The clip's standing sprite and its eight frames: any distinct sprites do, as only their order matters.
constexpr SpriteId STANDING = SpriteId::Cultivator;
constexpr std::array FRAMES = std::to_array(
    { SpriteId::Ground,
      SpriteId::RockLarge,
      SpriteId::RockSmall,
      SpriteId::Pine,
      SpriteId::Shrine,
      SpriteId::Fox,
      SpriteId::OrbBlue,
      SpriteId::OrbRed }
);
constexpr f32 STRIDE = 12.0F;

/// An eight-frame walk of STRIDE units a frame for STANDING.
core::WalkClip EightFrameClip() {
    return { .standing = STANDING, .frames = { FRAMES.begin(), FRAMES.end() }, .stride = STRIDE };
}

TEST_CASE("a walk clip shows the frame its stride has reached") {
    const core::WalkClip clip = EightFrameClip();
    CHECK(core::PoseSprite(&clip, STANDING, 0.0, true) == FRAMES[0]);
    CHECK(core::PoseSprite(&clip, STANDING, 11.99, true) == FRAMES[0]);
    CHECK(core::PoseSprite(&clip, STANDING, 12.0, true) == FRAMES[1]);
    CHECK(core::PoseSprite(&clip, STANDING, 95.99, true) == FRAMES[7]);
    // A whole cycle of eight strides starts it again.
    CHECK(core::PoseSprite(&clip, STANDING, 96.0, true) == FRAMES[0]);
    // Far along, the frame still follows the place in the cycle.
    CHECK(core::PoseSprite(&clip, STANDING, 1'000'000.0, true) == FRAMES[(1'000'000 % 96) / 12]);
}

TEST_CASE("a walk clip shows its first frame for a distance that is negative or not a number") {
    const core::WalkClip clip = EightFrameClip();
    CHECK(core::PoseSprite(&clip, STANDING, -1.0, true) == FRAMES[0]);
    // fmod keeps a negative distance's sign, so this one's frame index comes out near -8, not 0.
    CHECK(core::PoseSprite(&clip, STANDING, -95.0, true) == FRAMES[0]);
    CHECK(core::PoseSprite(&clip, STANDING, std::numeric_limits<f64>::quiet_NaN(), true) == FRAMES[0]);
    CHECK(core::PoseSprite(&clip, STANDING, std::numeric_limits<f64>::infinity(), true) == FRAMES[0]);
}

TEST_CASE("a walk clip's figure that isn't moving stands") {
    const core::WalkClip clip = EightFrameClip();
    CHECK(core::PoseSprite(&clip, STANDING, 30.0, false) == STANDING);
}

TEST_CASE("a figure without a walk clip stands while it walks") {
    CHECK(core::PoseSprite(nullptr, STANDING, 30.0, true) == STANDING);
}

TEST_CASE("the walks find a clip by its standing sprite") {
    core::Animations animations;
    animations.walks.push_back(EightFrameClip());
    animations.walks.push_back({ .standing = SpriteId::Crane, .frames = { SpriteId::Shadow }, .stride = 1.0F });
    const core::WalkClip* found = animations.WalkOf(SpriteId::Crane);
    REQUIRE(found != nullptr);
    CHECK(found->standing == SpriteId::Crane);
    CHECK(found == &animations.walks[1]);
    CHECK(animations.WalkOf(SpriteId::TreeDead) == nullptr);
}

} // namespace
