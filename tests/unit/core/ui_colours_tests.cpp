#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/ui_colours.hpp"

#include <doctest/doctest.h>

namespace {

// How many colours each area holds, in the menu's order.
constexpr std::array<std::size_t, core::COLOUR_AREAS> PER_AREA{ { 8, 4, 3, 2, 7 } };

u32 Default(core::UiColour colour) {
    return core::DEFAULT_UI_COLOURS.at(std::to_underlying(colour));
}

TEST_CASE("every UI colour belongs to an area, with a key unique in its area and a name") {
    for (std::size_t colour = 0; colour < core::UI_COLOURS; ++colour) {
        const core::UiColourInfo& info = core::UI_COLOUR_INFO.at(colour);
        INFO("colour ", colour);
        CHECK_FALSE(info.key.empty());
        CHECK(std::to_underlying(info.area) < core::COLOUR_AREAS);
        CHECK(std::to_underlying(info.name) < core::STRINGS);
        for (std::size_t other = colour + 1; other < core::UI_COLOURS; ++other) {
            const core::UiColourInfo& next = core::UI_COLOUR_INFO.at(other);
            CHECK_FALSE((info.area == next.area && info.key == next.key));
            CHECK(info.name != next.name);
        }
    }
    for (const core::ColourAreaInfo& area : core::COLOUR_AREA_INFO) {
        CHECK_FALSE(area.key.empty());
    }
}

TEST_CASE("each area's colours sit together, in the menu's order, and no area is empty") {
    std::array<std::size_t, core::COLOUR_AREAS> counts{};
    for (std::size_t colour = 0; colour < core::UI_COLOURS; ++colour) {
        const auto area = std::to_underlying(core::UI_COLOUR_INFO.at(colour).area);
        ++counts.at(area);
        if (colour > 0) {
            CHECK(area >= std::to_underlying(core::UI_COLOUR_INFO.at(colour - 1).area));
        }
    }
    CHECK(counts == PER_AREA);
}

TEST_CASE("the defaults are today's colours") {
    constexpr u32 LABEL = 0xFFCA'DCE7U;
    constexpr u32 CAPTION = 0xFF5A'B1E2U;
    constexpr u32 INK = 0xFF06'0404U;
    constexpr u32 HOVER = 0xFFF0'F4F4U;
    constexpr u32 SELECTED = 0xFF30'C0FFU;
    constexpr std::array<u32, core::HIT_KINDS> KINDS{ { 0xFFFF'FFFFU, 0xFF25'B533U, 0xFFCE'336FU, 0xFF0C'52FFU } };
    constexpr std::array<u32, core::HIT_KINDS> DARK_KINDS{ { 0xFF06'0404U, 0xFF04'1A02U, 0xFF57'061DU, 0xFF00'0839U } };
    const core::UiColours colours;
    CHECK(colours.Of(core::UiColour::Label) == LABEL);
    CHECK(colours.Of(core::UiColour::Caption) == CAPTION);
    CHECK(colours.Of(core::UiColour::TextInk) == INK);
    CHECK(colours.Of(core::UiColour::HoverOutline) == HOVER);
    CHECK(colours.Of(core::UiColour::SelectedOutline) == SELECTED);
    for (std::size_t kind = 0; kind < core::HIT_KINDS; ++kind) {
        const auto hit = static_cast<core::HitKind>(kind);
        CHECK(colours.Of(core::NumberColour(hit, false)) == KINDS.at(kind));
        CHECK(colours.Of(core::NumberColour(hit, true)) == DARK_KINDS.at(kind));
    }
}

TEST_CASE("an effect takes its element's colour") {
    CHECK(core::EffectColour(core::HitKind::Damage) == core::UiColour::DamageEffect);
    CHECK(core::EffectColour(core::HitKind::Healing) == core::UiColour::HealingEffect);
    CHECK(core::EffectColour(core::HitKind::Poison) == core::UiColour::PoisonEffect);
    CHECK(core::EffectColour(core::HitKind::Burn) == core::UiColour::BurnEffect);
}

TEST_CASE("a colour set in the table reads back, and the others stay") {
    constexpr u32 GARISH = 0xFF12'3456U;
    core::UiColours colours;
    colours.Set(core::UiColour::QiBar, GARISH);
    CHECK(colours.Of(core::UiColour::QiBar) == GARISH);
    CHECK(colours.Of(core::UiColour::HealthBar) == Default(core::UiColour::HealthBar));
}

TEST_CASE("the new elements' defaults are the sRGB colours they are named by") {
    CHECK(core::FromSrgb(0xCF, 0xE3, 0xFF, 0xFF) == Default(core::UiColour::DamageEffect));
    CHECK(core::FromSrgb(0x8F, 0xE3, 0xA8, 0xFF) == Default(core::UiColour::HealingEffect));
    CHECK(core::FromSrgb(0x7C, 0xD9, 0x57, 0xFF) == Default(core::UiColour::PoisonEffect));
    CHECK(core::FromSrgb(0xFF, 0x8A, 0x3D, 0xFF) == Default(core::UiColour::BurnEffect));
    CHECK(core::FromSrgb(0xD8, 0x57, 0x4A, 0xFF) == Default(core::UiColour::HealthBar));
    CHECK(core::FromSrgb(0x5A, 0xA9, 0xE6, 0xFF) == Default(core::UiColour::QiBar));
    CHECK(core::FromSrgb(0x20, 0x24, 0x2A, 0xFF) == Default(core::UiColour::BarBackground));
    CHECK(core::FromSrgb(0xF4, 0xEF, 0xE6, 0xFF) == Default(core::UiColour::HudText));
    CHECK(core::FromSrgb(0x8A, 0x8F, 0x96, 0xFF) == Default(core::UiColour::HudCooling));
    CHECK(core::FromSrgb(0xF0, 0x8A, 0x5D, 0xFF) == Default(core::UiColour::HudWarning));
}

TEST_CASE("sRGB round-trips through the table on every default") {
    // The table stores 8-bit linear, which sRGB's 8 bits can't all reach in the bright range (a hover outline's 0xF0 blue
    // lies between two of them) and which several sRGB bytes share in the dark: a default comes back within one linear step
    // of itself, and a colour named in sRGB goes in exactly (above).
    constexpr u32 BITS = 8;
    constexpr i32 MASK = 0xFF;
    for (std::size_t colour = 0; colour < core::UI_COLOURS; ++colour) {
        const u32 packed = core::DEFAULT_UI_COLOURS.at(colour);
        const core::Srgb srgb = core::ToSrgb(packed);
        const u32 back = core::FromSrgb(srgb.red, srgb.green, srgb.blue, srgb.alpha);
        for (u32 channel = 0; channel < 4; ++channel) {
            const i32 was = static_cast<i32>(packed >> (channel * BITS)) & MASK;
            const i32 now = static_cast<i32>(back >> (channel * BITS)) & MASK;
            INFO("colour ", colour, " channel ", channel);
            CHECK(std::abs(was - now) <= 1);
        }
    }
}

TEST_CASE("a half-transparent sRGB colour is premultiplied in linear space") {
    // sRGB 255 is linear 1, so half opacity halves it: 128 of 255.
    CHECK(core::FromSrgb(0xFF, 0, 0, 0x80) == 0x8000'0080U);
    CHECK(core::ToSrgb(0x8000'0080U).red == 0xFF);
}

TEST_CASE("a scaled colour multiplies every channel, alpha too, rounded to nearest and kept within a byte") {
    constexpr u32 COLOUR = 0xFF80'4001U; // alpha 255, blue 128, green 64, red 1
    constexpr f32 HALF = 0.5F;
    constexpr f32 TWICE = 2.0F;
    constexpr f32 BELOW_NOTHING = -1.0F;
    CHECK(core::ScaledColour(COLOUR, 1.0F) == COLOUR);
    CHECK(core::ScaledColour(COLOUR, HALF) == 0x8040'2001U); // 127.5 and 0.5 round away from zero
    CHECK(core::ScaledColour(COLOUR, 0.0F) == 0U);
    CHECK(core::ScaledColour(COLOUR, BELOW_NOTHING) == 0U);
    CHECK(core::ScaledColour(COLOUR, TWICE) == 0xFFFF'8002U); // each channel saturates on its own
}

TEST_CASE("a colour's opacity is its alpha byte over 255") {
    constexpr f32 FIFTH = 0.2F; // 51 of 255
    CHECK(core::OpacityOf(0xFF12'3456U) == 1.0F);
    CHECK(core::OpacityOf(0x0012'3456U) == 0.0F);
    CHECK(core::OpacityOf(0x3300'0000U) == FIFTH);
}

} // namespace
