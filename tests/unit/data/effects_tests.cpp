#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/glow.hpp"
#include "data/effects.hpp"

#include <doctest/doctest.h>

namespace {

core::Error ErrorFor(std::string_view text) {
    const std::expected<core::Effects, core::Error> effects = data::ParseEffects(text);
    REQUIRE_FALSE(effects.has_value());
    return effects.error();
}

TEST_CASE("an empty table of effects is the shipped looks and layout") {
    const std::expected<core::Effects, core::Error> effects = data::ParseEffects("{}");
    REQUIRE(effects.has_value());
    CHECK(effects->Hash() == core::Effects{}.Hash());
    CHECK(effects->Valid());
    CHECK(effects->glows.at(std::to_underlying(core::GlowRole::FlyingSwords)).opacity == core::shipped::SWORD_AURA.opacity);
}

TEST_CASE("glows are read and mapped to their roles") {
    constexpr f32 SIZE = 6.0F;
    constexpr f32 OPACITY = 0.8F;
    constexpr f32 PERIOD = 0.34F;
    const std::expected<core::Effects, core::Error> effects = data::ParseEffects(
        "glows:\n"
        "  mine: { colour: selection.hover, blend: ink, size: 6, feather: 0.5, opacity: 0.8, zoom_shrink: 0,\n"
        "          drive: { kind: pulse, period: 0.34, depth: 0.5, curve: smooth } }\n"
        "  tally: { colour: element, drive: { kind: progress, curve: ease_in,\n"
        "                                     results: [effects.healing, names_and_captions.caption] } }\n"
        "glow_roles: { selected: mine, hover: hover, flying_swords: tally, fire_bolt: bolt_aura, demo_pulse: demo_pulse,\n"
        "              demo_charge: demo_charge, demo_progress: demo_progress }\n"
    );
    REQUIRE(effects.has_value());
    const core::GlowLook& selected = effects->glows.at(std::to_underlying(core::GlowRole::Selected));
    CHECK_FALSE(selected.colour.element);
    CHECK(selected.colour.colour == core::UiColour::HoverOutline);
    CHECK(selected.blend == core::GlowBlend::Ink);
    CHECK(selected.size == SIZE);
    CHECK(selected.opacity == OPACITY);
    CHECK(selected.drive.kind == core::DriveKind::Pulse);
    CHECK(selected.drive.periodSeconds == PERIOD);
    CHECK(selected.drive.curve == core::GlowCurve::Smooth);
    const core::GlowLook& tally = effects->glows.at(std::to_underlying(core::GlowRole::FlyingSwords));
    CHECK(tally.colour.element);
    CHECK(tally.drive.kind == core::DriveKind::Progress);
    CHECK(tally.drive.curve == core::GlowCurve::EaseIn);
    CHECK(tally.drive.resultCount == 2);
    CHECK(tally.drive.results.at(1) == core::UiColour::Caption);
}

TEST_CASE("glows alone keep the shipped roles, and a redefined shipped glow follows its role") {
    const std::expected<core::Effects, core::Error> effects =
        data::ParseEffects("glows:\n  hover: { colour: selection.selected, size: 5 }\n");
    REQUIRE(effects.has_value());
    CHECK(effects->glows.at(std::to_underlying(core::GlowRole::Hover)).colour.colour == core::UiColour::SelectedOutline);
    CHECK(effects->glows.at(std::to_underlying(core::GlowRole::Selected)).colour.colour == core::UiColour::SelectedOutline);
    const core::GlowLook& hover = effects->glows.at(std::to_underlying(core::GlowRole::Hover));
    CHECK(hover.blend == core::GlowBlend::Ink);
    CHECK(hover.feather == 0.0F);
    CHECK(hover.drive.kind == core::DriveKind::Steady);
}

TEST_CASE("the hash covers the glows' opacity and results") {
    constexpr f32 DIMMER = 0.5F;
    const core::Effects shipped{};
    core::Effects dimmer{};
    dimmer.glows.at(std::to_underlying(core::GlowRole::Hover)).opacity = DIMMER;
    CHECK(dimmer.Hash() != shipped.Hash());
    core::Effects recoloured{};
    recoloured.glows.at(std::to_underlying(core::GlowRole::DemoProgress)).drive.results.at(1) = core::UiColour::PoisonEffect;
    CHECK(recoloured.Hash() != shipped.Hash());
}

TEST_CASE(
    "a missing role, an unknown glow, colour or curve, and a pulse faster than three a second are refused with their lines"
) {
    struct Broken {
        std::string_view text;
        std::string_view says;
    };
    constexpr std::array<Broken, 9> CASES{ {
        Broken{ .text = "glow_roles: { selected: selected }\n", .says = "hover" },
        Broken{ .text = "glow_roles: { selected: nothing, hover: hover }\n", .says = "nothing" },
        Broken{ .text = "glows:\n  a: { colour: effects.shiny }\n", .says = "effects.shiny" },
        Broken{ .text = "glows:\n  a: { colour: wobble }\n", .says = "wobble" },
        Broken{ .text = "glows:\n  a: { drive: { kind: pulse, curve: bumpy } }\n", .says = "bumpy" },
        Broken{ .text = "glows:\n  a: { drive: { kind: wobble } }\n", .says = "wobble" },
        Broken{ .text = "glows:\n  a: { blend: gloss }\n", .says = "gloss" },
        Broken{ .text = "glows:\n  a: { drive: { kind: pulse, period: 0.2 } }\n", .says = "a's drive's period" },
        Broken{ .text = "glows:\n  a: { size: 17 }\n", .says = "a's size" },
    } };
    for (const Broken& broken : CASES) {
        CAPTURE(broken.text);
        const core::Error error = ErrorFor(broken.text);
        CHECK(error.message.contains(broken.says));
        CHECK(error.message.contains("effects line"));
    }
    const core::Error nine = ErrorFor(
        "glows:\n  a: { drive: { kind: progress, results: [effects.damage, effects.damage, effects.damage, effects.damage,\n"
        "    effects.damage, effects.damage, effects.damage, effects.damage, effects.damage] } }\n"
    );
    CHECK(nine.message.contains("results"));
    const std::expected<core::Effects, core::Error> slow =
        data::ParseEffects("glows:\n  a: { drive: { kind: pulse, period: 0.34 } }\n");
    CHECK(slow.has_value());
}

TEST_CASE("a hex colour is refused") {
    const core::Error quoted = ErrorFor("glows:\n  a: { colour: \"#ff0000\" }\n");
    const core::Error plain = ErrorFor("glows:\n  a: { colour: 0xff0000 }\n");
    CHECK(quoted.message.contains("#ff0000"));
    CHECK(plain.message.contains("0xff0000"));
}

TEST_CASE("an unknown key in a glow, its drive or the roles is a warning with its line") {
    std::vector<std::string> warnings;
    const std::expected<core::Effects, core::Error> effects = data::ParseEffects(
        "glows:\n"
        "  a: { sparkle: 1, drive: { kind: steady, wobble: 2 } }\n"
        "glow_roles: { selected: a, hover: hover, flying_swords: a, fire_bolt: a, demo_pulse: a, demo_charge: a,\n"
        "              demo_progress: a, dance: a }\n",
        &warnings
    );
    REQUIRE(effects.has_value());
    REQUIRE(warnings.size() == 3);
    CHECK(warnings[0].contains("sparkle"));
    CHECK(warnings[1].contains("wobble"));
    CHECK(warnings[2].contains("dance"));
}

TEST_CASE("each section sets its values, a trail in seconds as ticks, and leaves the others") {
    constexpr f32 HALF_A_SECOND_OF_TICKS = 30.0F;
    constexpr f32 SOME = 7.0F;
    constexpr f32 QUARTER = 0.25F;
    constexpr f32 HALF = 0.5F;
    constexpr f32 TWO = 2.0F;
    constexpr f32 NINE = 9.0F;
    constexpr f32 FIVE = 5.0F;
    constexpr f32 TWENTY = 20.0F;
    constexpr f32 THIRTY = 30.0F;
    constexpr f64 TWO_AND_A_HALF = 2.5;
    const std::expected<core::Effects, core::Error> effects = data::ParseEffects(
        "abilities:\n"
        "  fire_bolt: { sword_scale: 2, trail: 0.5, trail_width: 7, trail_taper: 0.25, trail_intensity: 0.5 }\n"
        "  poison_lance: { flash_start_scale: 1, flash_end_scale: 2, flash_intensity: 0.25 }\n"
        "health_bar: { rise: 9, width: 50, height: 8, fill_width: 46, fill_height: 5 }\n"
        "hud: { margin: 20, bar_width: 300, bar_height: 20, line_height: 30, bar_gap: 8, bar_text_inset: 4,\n"
        "       bar_text_rise: 3, not_enough_qi: 2.5, cooling_ink_opacity: 0.5 }\n"
    );
    REQUIRE(effects.has_value());
    const core::SwordLook& bolt = effects->fireBolt;
    CHECK(bolt.swordScale == TWO);
    CHECK(bolt.trailTicks == HALF_A_SECOND_OF_TICKS);
    CHECK(bolt.trailWidth == SOME);
    CHECK(bolt.trailTaper == QUARTER);
    CHECK(bolt.trailIntensity == HALF);
    CHECK(effects->poisonLance.endScale == TWO);
    CHECK(effects->poisonLance.intensity == QUARTER);
    CHECK(effects->healthBar.rise == NINE);
    CHECK(effects->healthBar.fillHeight == FIVE);
    CHECK(effects->hud.margin == TWENTY);
    CHECK(effects->hud.lineHeight == THIRTY);
    CHECK(effects->hud.notEnoughQiSeconds == TWO_AND_A_HALF);
    CHECK(effects->hud.coolingInkOpacity == HALF);
    CHECK(effects->flyingSwords.swordScale == core::SwordLook{}.swordScale); // not mentioned
}

TEST_CASE("a value out of range is refused, naming the key and its line") {
    struct Broken {
        std::string_view text;
        std::string_view says;
    };
    constexpr std::array<Broken, 10> CASES{ {
        Broken{ .text = "abilities:\n  fire_bolt: { sword_scale: 0 }", .says = "fire_bolt's sword_scale" },
        Broken{ .text = "abilities:\n  flying_swords: { trail_intensity: -1 }", .says = "flying_swords's trail_intensity" },
        Broken{ .text = "abilities:\n  flying_swords: { trail: 0 }", .says = "flying_swords's trail" },
        Broken{ .text = "abilities:\n  flying_swords: { trail_taper: 1.5 }", .says = "flying_swords's trail_taper" },
        Broken{ .text = "abilities:\n  flying_swords: { trail_width: 0 }", .says = "flying_swords's trail_width" },
        Broken{ .text = "abilities:\n  poison_lance: { flash_end_scale: 0 }", .says = "poison_lance's flash_end_scale" },
        Broken{ .text = "health_bar: { width: 0 }", .says = "health_bar's width" },
        Broken{ .text = "hud: { not_enough_qi: -1 }", .says = "hud's not_enough_qi" },
        Broken{ .text = "hud: { bar_width: .nan }", .says = "hud's bar_width" },
        Broken{ .text = "hud: { margin: wide }", .says = "hud's margin isn't a number" },
    } };
    for (const Broken& broken : CASES) {
        CAPTURE(broken.text);
        const core::Error error = ErrorFor(broken.text);
        CHECK(error.message.contains(broken.says));
        CHECK(error.message.contains("effects line"));
    }
}

TEST_CASE("a section that isn't a map, or a file that isn't, is refused") {
    const core::Error hud = ErrorFor("hud: 3");
    const core::Error abilities = ErrorFor("abilities: [1]");
    const core::Error file = ErrorFor("[1, 2]");
    const core::Error broken = ErrorFor("hud: {");
    CHECK(hud.message.contains("hud isn't a map"));
    CHECK(abilities.message.contains("abilities isn't a map"));
    CHECK(file.message.contains("not a map of effects"));
    CHECK_FALSE(broken.message.empty());
}

TEST_CASE("an unknown key is a warning with its line, in each place") {
    std::vector<std::string> warnings;
    const std::expected<core::Effects, core::Error> effects = data::ParseEffects(
        "wobble: 1\n"
        "abilities:\n"
        "  fire_bolt: { glitter: 3 }\n"
        "  dash: {}\n"
        "health_bar: { colour: 3 }\n"
        "hud: { fancy: 1 }\n",
        &warnings
    );
    REQUIRE(effects.has_value());
    REQUIRE(warnings.size() == 5);
    CHECK(warnings[0].contains("no section wobble"));
    CHECK(warnings[1].contains("no ability dash"));
    CHECK(warnings[2].contains("fire_bolt has no key glitter"));
    CHECK(warnings[3].contains("health_bar has no key colour"));
    CHECK(warnings[4].contains("hud has no key fancy"));
}

TEST_CASE("the halo's and the core's keys are gone: each is an unknown key's warning, and the look keeps its numbers") {
    std::vector<std::string> warnings;
    const std::expected<core::Effects, core::Error> effects = data::ParseEffects(
        "abilities:\n"
        "  flying_swords: { halo_scale: 3, core_scale: 4, halo_intensity: 0.5, core_opacity: 0.75 }\n",
        &warnings
    );
    REQUIRE(effects.has_value());
    REQUIRE(warnings.size() == 4);
    CHECK(warnings[0].contains("flying_swords has no key halo_scale"));
    CHECK(warnings[1].contains("flying_swords has no key core_scale"));
    CHECK(warnings[2].contains("flying_swords has no key halo_intensity"));
    CHECK(warnings[3].contains("flying_swords has no key core_opacity"));
    CHECK(effects->Hash() == core::Effects{}.Hash());
}

TEST_CASE("effects are loaded from a folder's effects.yaml, and a missing file is refused") {
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "project_n_effects_tests_load";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directory(folder);
    const std::expected<core::Effects, core::Error> missing = data::LoadEffects(folder);
    CHECK_FALSE(missing.has_value());
    std::ofstream{ folder / "effects.yaml" } << "hud: { margin: 33 }\n";
    const std::expected<core::Effects, core::Error> loaded = data::LoadEffects(folder);
    REQUIRE(loaded.has_value());
    constexpr f32 SET = 33.0F;
    CHECK(loaded->hud.margin == SET);
    std::ofstream{ folder / "effects.yaml" } << "hud: { margin: -1 }\n";
    CHECK_FALSE(data::LoadEffects(folder).has_value());
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
}

} // namespace
