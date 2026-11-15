#pragma once

/// @file
/// @ingroup core
/// The game's strings (design doc 0002.4.2 §5): the IDs the code names, and their text in each language, as
/// `assets/data/strings.yaml` holds them.

#include "core/assert.hpp"
#include "core/types.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core {

/// A string the game shows: one the code names, by the name STRING_NAMES gives it in strings.yaml, or a data string past
/// them (values from STRINGS), by its key in strings.yaml (Strings::Find).
enum class StringId : u16 {     // NOLINT(performance-enum-size): data strings number past the named ones, so it needs 16 bits
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
    ColourSealGold,             ///< Seals' gold accent.
    ColourSealViolet,           ///< Seals' violet accent.
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
    Health,                     ///< The HUD's health bar: `{value}` and `{max}` are filled in (FillValue()).
    Qi,                         ///< The HUD's Qi bar: `{value}` and `{max}` are filled in.
    NotEnoughQi,                ///< The Qi bar's warning after a cast was refused for want of Qi.
    Cooldown,                   ///< A slot's seconds left on cooldown: `{value}` is filled in.
    SlotEmpty,                  ///< An empty slot in the HUD.
    KeyLeftClick,               ///< The left mouse button, the generator's control.
    KeyRightClick,              ///< The right mouse button, the spender's control.
    KeySpace,                   ///< The space bar, the movement's control.
    FireBolt,                   ///< The fire bolt's name.
    PoisonLance,                ///< The poison lance's name.
    Dash,                       ///< The dash's name.
    FlyingSwords,               ///< The flying swords' name.
    WalkerName,                 ///< The name label of a walker on the world map.
    PortalLabel,                ///< The label over a portal.
};
/// How many strings StringId names.
inline constexpr std::size_t STRINGS = 49;
/// The most strings a table holds, named and data together: StringId is 16 bits.
inline constexpr std::size_t MOST_STRINGS = 0xFFFF;
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
                                                                       "colour_seal_gold",
                                                                       "colour_seal_violet",
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
                                                                       "colour_hud_warning",
                                                                       "health",
                                                                       "qi",
                                                                       "not_enough_qi",
                                                                       "cooldown",
                                                                       "slot_empty",
                                                                       "key_left_click",
                                                                       "key_right_click",
                                                                       "key_space",
                                                                       "fire_bolt",
                                                                       "poison_lance",
                                                                       "dash",
                                                                       "flying_swords",
                                                                       "walker_name",
                                                                       "portal_label" } };

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
class Strings {
public:
    /// The text of each string (by StringId) in each language (by Language): the STRINGS the code names first, in
    /// StringId's order, then the data strings.
    std::vector<std::array<std::string, LANGUAGES>> text = std::vector<std::array<std::string, LANGUAGES>>(STRINGS);

    /// The text of `string` in `language`.
    [[nodiscard]] const std::string& Text(StringId string, Language language) const {
        return text.at(std::to_underlying(string)).at(std::to_underlying(language));
    }

    /// The string `key` names in strings.yaml, whether the code names it or it is a data string; empty if none.
    [[nodiscard]] std::optional<StringId> Find(std::string_view key) const {
        const std::span names{ STRING_NAMES }; // a span's iterator is a class everywhere, unlike std::array's
        if (const auto named = std::ranges::find(names, key); named != names.end()) {
            return StringId{ static_cast<u16>(named - names.begin()) };
        }
        if (const auto data = m_index.find(key); data != m_index.end()) {
            return data->second;
        }
        return std::nullopt;
    }

    /// Adds a data string under `key` with its `texts` (by Language), and returns its ID, the next after `text`'s last.
    /// `key` is neither one the code names nor one already added, and the table holds fewer than MOST_STRINGS (the reader
    /// checks both).
    StringId Add(std::string key, std::array<std::string, LANGUAGES> texts) {
        PN_ASSERT(!Find(key).has_value(), "a data string's key is new");
        PN_ASSERT(text.size() < MOST_STRINGS, "the table has room for another string");
        const StringId added{ static_cast<u16>(text.size()) };
        m_index.emplace(std::move(key), added);
        text.push_back(std::move(texts));
        return added;
    }

private:
    // Hashes a key as text, so Find looks a std::string_view up without building a std::string.
    struct KeyHash {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(std::string_view key) const {
            return std::hash<std::string_view>{}(key);
        }
    };

    // Each data string's ID by its key: Add keeps it, Find reads it.
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::unordered_map<std::string, StringId, KeyHash, std::equal_to<>> m_index{};
};

/// Writes `text` into `into` (replacing what it held, keeping its capacity) with `value` in place of each `{value}` and
/// `most` in place of each `{max}`, both as plain decimal numbers: the HUD's strings (StringId::Health, Qi, Cooldown).
void FillValue(std::string& into, std::string_view text, i64 value, i64 most);

} // namespace core
