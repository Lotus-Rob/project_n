#include "core/cxx23.hpp" // IWYU pragma: keep
#include "gfx/glow_table.hpp"

#include "core/effects.hpp"
#include "core/glow.hpp"
#include "core/ui_colours.hpp"

#include <doctest/doctest.h>

namespace {

constexpr std::size_t FIRE_BOLT = std::to_underlying(core::GlowRole::FireBolt);
constexpr std::size_t SELECTED = std::to_underlying(core::GlowRole::Selected);
constexpr std::size_t PROGRESS = std::to_underlying(core::GlowRole::DemoProgress);
constexpr f32 BOLT_SIZE = 8.5F;
constexpr f32 BOLT_PERIOD_TICKS = 96.0F; // 1.6 s at 60 ticks a second
constexpr u32 PROGRESS_RESULTS = 4;
constexpr u32 CHANGED = 0xFF12'3456U;

TEST_CASE("the shipped table's fire bolt is a pulse of the element's colour") {
    const auto table = gfx::BuildGlowTable(core::Effects{}, core::UiColours{});
    const gfx::GlowRecord& bolt = table.at(FIRE_BOLT);
    CHECK(bolt.size == doctest::Approx(BOLT_SIZE));
    CHECK(bolt.periodTicks == doctest::Approx(BOLT_PERIOD_TICKS));
    CHECK(bolt.colour == 0);
    CHECK(bolt.driveKind == std::to_underlying(core::DriveKind::Pulse));
}

TEST_CASE("a glow's colour is the palette's, and follows it") {
    core::UiColours colours;
    const auto before = gfx::BuildGlowTable(core::Effects{}, colours);
    CHECK(before.at(SELECTED).colour == colours.Of(core::UiColour::SelectedOutline));
    colours.Set(core::UiColour::SelectedOutline, CHANGED);
    const auto after = gfx::BuildGlowTable(core::Effects{}, colours);
    CHECK(after.at(SELECTED).colour == CHANGED);
}

TEST_CASE("a progress glow lists its results' colours, the palette's healing first") {
    const core::UiColours colours;
    const auto table = gfx::BuildGlowTable(core::Effects{}, colours);
    const gfx::GlowRecord& progress = table.at(PROGRESS);
    CHECK(progress.resultCount == PROGRESS_RESULTS);
    CHECK(progress.results.at(0) == colours.Of(core::UiColour::HealingEffect));
}

TEST_CASE("a charge's fade is in ticks, not seconds") {
    constexpr std::size_t CHARGE = std::to_underlying(core::GlowRole::DemoCharge);
    constexpr f32 FADE_TICKS = 24.0F; // 0.4 s at 60 ticks a second
    const auto table = gfx::BuildGlowTable(core::Effects{}, core::UiColours{});
    CHECK(table.at(CHARGE).fadeTicks == doctest::Approx(FADE_TICKS));
}

TEST_CASE("the none role's record stays zero") {
    const auto table = gfx::BuildGlowTable(core::Effects{}, core::UiColours{});
    const gfx::GlowRecord& none = table.at(0);
    CHECK(none.size == 0.0F);
    CHECK(none.opacity == 0.0F);
    CHECK(none.colour == 0);
    CHECK(none.resultCount == 0);
}

TEST_CASE("a progress glow's later results follow in order") {
    constexpr std::size_t SECOND = 1;
    const core::UiColours colours;
    const auto table = gfx::BuildGlowTable(core::Effects{}, colours);
    CHECK(table.at(PROGRESS).results.at(SECOND) == colours.Of(core::UiColour::DamageEffect));
}

} // namespace
