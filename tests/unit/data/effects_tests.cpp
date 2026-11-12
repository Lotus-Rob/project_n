#include "core/cxx23.hpp" // IWYU pragma: keep
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
}

TEST_CASE("each section sets its values, a trail in seconds as ticks, and leaves the others") {
    constexpr f32 HALF_A_SECOND_OF_TICKS = 30.0F;
    constexpr f32 SOME = 7.0F;
    constexpr f32 QUARTER = 0.25F;
    constexpr f32 HALF = 0.5F;
    constexpr f32 THREE_QUARTERS = 0.75F;
    constexpr f32 TWO = 2.0F;
    constexpr f32 THREE = 3.0F;
    constexpr f32 FOUR = 4.0F;
    constexpr f32 NINE = 9.0F;
    constexpr f32 FIVE = 5.0F;
    constexpr f32 TWENTY = 20.0F;
    constexpr f32 THIRTY = 30.0F;
    constexpr f64 TWO_AND_A_HALF = 2.5;
    const std::expected<core::Effects, core::Error> effects = data::ParseEffects(
        "abilities:\n"
        "  fire_bolt: { sword_scale: 2, halo_scale: 3, core_scale: 4, trail: 0.5, trail_width: 7, trail_taper: 0.25,\n"
        "               halo_intensity: 0.5, core_opacity: 0.75 }\n"
        "  poison_lance: { flash_start_scale: 1, flash_end_scale: 2, flash_intensity: 0.25 }\n"
        "health_bar: { rise: 9, width: 50, height: 8, fill_width: 46, fill_height: 5 }\n"
        "hud: { margin: 20, bar_width: 300, bar_height: 20, line_height: 30, bar_gap: 8, bar_text_inset: 4,\n"
        "       bar_text_rise: 3, not_enough_qi: 2.5, cooling_ink_opacity: 0.5 }\n"
    );
    REQUIRE(effects.has_value());
    const core::SwordLook& bolt = effects->fireBolt;
    CHECK(bolt.swordScale == TWO);
    CHECK(bolt.haloScale == THREE);
    CHECK(bolt.coreScale == FOUR);
    CHECK(bolt.trailTicks == HALF_A_SECOND_OF_TICKS);
    CHECK(bolt.trailWidth == SOME);
    CHECK(bolt.trailTaper == QUARTER);
    CHECK(bolt.haloIntensity == HALF);
    CHECK(bolt.coreOpacity == THREE_QUARTERS);
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
        Broken{ .text = "abilities:\n  flying_swords: { halo_scale: -1 }", .says = "flying_swords's halo_scale" },
        Broken{ .text = "abilities:\n  flying_swords: { trail: 0 }", .says = "flying_swords's trail" },
        Broken{ .text = "abilities:\n  flying_swords: { trail_taper: 1.5 }", .says = "flying_swords's trail_taper" },
        Broken{ .text = "abilities:\n  flying_swords: { core_opacity: 2 }", .says = "flying_swords's core_opacity" },
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
