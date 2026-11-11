#pragma once

/// @file
/// @ingroup core
/// The game's strings (design doc 0002.4.2 §5): the IDs the code names, and their text in each language, as
/// `assets/data/strings.yaml` holds them.

#include "core/types.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace core {

/// A string the game shows, by the name STRING_NAMES gives it in strings.yaml.
enum class StringId : u8 {
    CultivatorName,             ///< The player's name label.
    TabletCaption,              ///< The vertical caption beside the stone tablet.
    FoxName,                    ///< The fox's name label.
    LabelStack,                 ///< A name label for several creatures in one place: `{name}` and `{count}` are filled in.
    ColourAreaCombatText,       ///< The combat text colours' area.
    ColourAreaEffects,          ///< The effect colours' area.
    ColourAreaNamesAndCaptions, ///< The names and captions colours' area.
    ColourAreaSelection,        ///< The selection colours' area.
    ColourAreaHud,              ///< The HUD colours' area.
    ColourDamageNumber,         ///< Damage numbers' colour.
    ColourHealingNumber,        ///< Healing numbers' colour.
    ColourPoisonNumber,         ///< Poison numbers' colour.
    ColourBurnNumber,           ///< Burn numbers' colour.
    ColourDarkDamageNumber,     ///< Damage numbers' colour without outline and shadow.
    ColourDarkHealingNumber,    ///< Healing numbers' colour without outline and shadow.
    ColourDarkPoisonNumber,     ///< Poison numbers' colour without outline and shadow.
    ColourDarkBurnNumber,       ///< Burn numbers' colour without outline and shadow.
    ColourDamageEffect,         ///< Damage effects' colour.
    ColourHealingEffect,        ///< Healing effects' colour.
    ColourPoisonEffect,         ///< Poison effects' colour.
    ColourBurnEffect,           ///< Burn effects' colour.
    ColourLabel,                ///< Name labels' colour.
    ColourCaption,              ///< The caption's colour.
    ColourTextInk,              ///< The ink of text outlines and shadows.
    ColourHover,                ///< The hover outline's colour.
    ColourSelected,             ///< The selection outline's colour.
    ColourHealthBar,            ///< The player's health bar.
    ColourQiBar,                ///< The Qi bar.
    ColourEnemyHealthBar,       ///< Enemies' health bars.
    ColourBarBackground,        ///< The bars' background.
    ColourHudText,              ///< The HUD's text.
    ColourHudCooling,           ///< The HUD's cooling slots.
    ColourHudWarning,           ///< The HUD's warnings.
};
/// How many strings StringId names.
inline constexpr std::size_t STRINGS = 33;
/// Each StringId's name in strings.yaml, in StringId's order.
inline constexpr std::array<std::string_view, STRINGS> STRING_NAMES{ { "cultivator_name",
                                                                       "tablet_caption",
                                                                       "fox_name",
                                                                       "label_stack",
                                                                       "colour_area_combat_text",
                                                                       "colour_area_effects",
                                                                       "colour_area_names_and_captions",
                                                                       "colour_area_selection",
                                                                       "colour_area_hud",
                                                                       "colour_damage_number",
                                                                       "colour_healing_number",
                                                                       "colour_poison_number",
                                                                       "colour_burn_number",
                                                                       "colour_dark_damage_number",
                                                                       "colour_dark_healing_number",
                                                                       "colour_dark_poison_number",
                                                                       "colour_dark_burn_number",
                                                                       "colour_damage_effect",
                                                                       "colour_healing_effect",
                                                                       "colour_poison_effect",
                                                                       "colour_burn_effect",
                                                                       "colour_label",
                                                                       "colour_caption",
                                                                       "colour_text_ink",
                                                                       "colour_hover",
                                                                       "colour_selected",
                                                                       "colour_health_bar",
                                                                       "colour_qi_bar",
                                                                       "colour_enemy_health_bar",
                                                                       "colour_bar_background",
                                                                       "colour_hud_text",
                                                                       "colour_hud_cooling",
                                                                       "colour_hud_warning" } };

/// A language the strings come in.
enum class Language : u8 {
    English, ///< `en`.
    Chinese, ///< `zh`: Simplified Chinese.
};
/// How many languages Language names.
inline constexpr std::size_t LANGUAGES = 2;
/// Each Language's code in strings.yaml and settings.yaml, in Language's order.
inline constexpr std::array<std::string_view, LANGUAGES> LANGUAGE_CODES{ { "en", "zh" } };

/// Every string in every language.
struct Strings {
    /// The text of each string (by StringId) in each language (by Language).
    std::array<std::array<std::string, LANGUAGES>, STRINGS> text{};

    /// The text of `string` in `language`.
    [[nodiscard]] const std::string& Text(StringId string, Language language) const {
        return text.at(std::to_underlying(string)).at(std::to_underlying(language));
    }
};

} // namespace core
