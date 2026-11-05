#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"

#include <doctest/doctest.h>

namespace {

struct HalfCase {
    f32 value = 0.0F;
    u16 bits = 0;
};

// Exact values first, then rounding to nearest with ties to even, then the ends of the range.
constexpr std::array HALVES{
    HalfCase{ .value = 0.0F, .bits = 0x0000 },
    HalfCase{ .value = -0.0F, .bits = 0x8000 },
    HalfCase{ .value = 1.0F, .bits = 0x3C00 },
    HalfCase{ .value = -2.0F, .bits = 0xC000 },
    HalfCase{ .value = 0.5F, .bits = 0x3800 },
    HalfCase{ .value = 65504.0F, .bits = 0x7BFF }, // the largest half
    HalfCase{ .value = 0x1p-14F, .bits = 0x0400 }, // the smallest normal half
    HalfCase{ .value = 0x1p-24F, .bits = 0x0001 }, // the smallest subnormal half
    HalfCase{ .value = 1.0F / 3.0F, .bits = 0x3555 },
    HalfCase{ .value = 1.0F + 0x1p-11F, .bits = 0x3C00 }, // halfway: down to the even 1.0
    HalfCase{ .value = 1.0F + 0x3p-11F, .bits = 0x3C02 }, // halfway: up to the even neighbour
    HalfCase{ .value = 0x7FFp-10F, .bits = 0x3FFF },
    HalfCase{ .value = 0xFFFp-11F, .bits = 0x4000 }, // rounding carries into the exponent
    HalfCase{ .value = 0x1p-25F, .bits = 0x0000 },   // halfway to the smallest subnormal: to even
    HalfCase{ .value = 0x3p-26F, .bits = 0x0001 },   // three quarters of it: up
    HalfCase{ .value = 0xFFFp-26F, .bits = 0x0400 }, // past the largest subnormal: up to the smallest normal
    HalfCase{ .value = 65519.0F, .bits = 0x7BFF },
    HalfCase{ .value = 65520.0F, .bits = 0x7C00 }, // rounds past the largest half: infinity
    HalfCase{ .value = -1.0e9F, .bits = 0xFC00 },
    HalfCase{ .value = 0x1p-30F, .bits = 0x0000 },
};

TEST_CASE("halves round to nearest, ties to even, and overflow to infinity") {
    for (const HalfCase& half : HALVES) {
        CAPTURE(half.value);
        CHECK(core::PackHalf(half.value) == half.bits);
    }
}

TEST_CASE("infinity stays infinity, and NaN stays NaN") {
    CHECK(core::PackHalf(std::numeric_limits<f32>::infinity()) == 0x7C00);
    CHECK(core::PackHalf(std::numeric_limits<f32>::quiet_NaN()) == 0x7E00);
}

constexpr f32 HALF = 0.5F;
constexpr f32 DOUBLE = 2.0F;

TEST_CASE("unorm16 clamps and rounds") {
    CHECK(core::PackUnorm16(0.0F) == 0);
    CHECK(core::PackUnorm16(1.0F) == 0xFFFF);
    CHECK(core::PackUnorm16(-1.0F) == 0);
    CHECK(core::PackUnorm16(DOUBLE) == 0xFFFF);
    CHECK(core::PackUnorm16(HALF) == 0x8000);
}

TEST_CASE("a pair packs its first value in the low half") {
    CHECK(core::PackPair(0x1234, 0xABCD) == 0xABCD'1234U);
}

TEST_CASE("an instance puts its sprite's foot point on its position") {
    using core::atlas::SpriteId;
    const core::atlas::Sprite& sprite = core::atlas::SPRITES.at(static_cast<std::size_t>(SpriteId::Fox));
    constexpr core::Vec2 FOOT{ .x = 500.0F, .y = 300.0F };
    const core::Instance instance = core::MakeInstance({ .sprite = SpriteId::Fox, .previous = FOOT, .current = FOOT });
    const core::Vec2 centre{ .x = FOOT.x + (static_cast<f32>(sprite.width) * HALF) - static_cast<f32>(sprite.footX),
                             .y = FOOT.y + (static_cast<f32>(sprite.height) * HALF) - static_cast<f32>(sprite.footY) };
    CHECK(instance.current.x == doctest::Approx(centre.x));
    CHECK(instance.current.y == doctest::Approx(centre.y));
    CHECK(instance.previous.x == doctest::Approx(centre.x));
    CHECK(instance.previous.y == doctest::Approx(centre.y));
    CHECK(instance.size == core::PackPair(core::PackHalf(sprite.width), core::PackHalf(sprite.height)));
    CHECK((instance.layerFlags & 0xFFFFU) == sprite.page);
    CHECK((instance.regionMin & 0xFFFFU) == core::PackUnorm16(static_cast<f32>(sprite.x) / core::atlas::PAGE_SIZE));
    CHECK(instance.colour == core::OPAQUE_WHITE);
}

TEST_CASE("a scaled instance grows about its foot point") {
    using core::atlas::SpriteId;
    const core::atlas::Sprite& sprite = core::atlas::SPRITES.at(static_cast<std::size_t>(SpriteId::Pine));
    const core::Instance instance = core::MakeInstance({ .sprite = SpriteId::Pine, .scale = DOUBLE });
    CHECK(instance.current.y == doctest::Approx(static_cast<f32>(sprite.height) - (DOUBLE * static_cast<f32>(sprite.footY))));
    CHECK(instance.size == core::PackPair(core::PackHalf(DOUBLE * sprite.width), core::PackHalf(DOUBLE * sprite.height)));
}

} // namespace
