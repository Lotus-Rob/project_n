#pragma once

/// @file
/// @ingroup core
/// Every colour of the user interface in one table of named colours, in the player's settings: the accessibility menu
/// (chunk 11) lists them as Colours, then an area, then an element, and lets the player change each. The main thread reads
/// the table where each element is drawn, so a change shows from the next frame; the simulation never colours anything.
/// Effects take the colour of their element (damage, healing, poison, burn), never one of their ability's, so a new ability
/// adds no colour.

#include "core/hit.hpp"
#include "core/strings.hpp"
#include "core/types.hpp"

#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

namespace core {

/// A group of colours the accessibility menu shows together, in the menu's order.
enum class ColourArea : u8 {
    CombatText,       ///< Damage numbers.
    Effects,          ///< An ability's halo, trail, glow and burst, by element.
    NamesAndCaptions, ///< Labels, the caption and the ink of text's outline and shadow.
    Selection,        ///< The hover and selection outlines.
    Hud,              ///< The health and Qi bars and the HUD's text.
};
/// How many areas ColourArea names.
inline constexpr std::size_t COLOUR_AREAS = 5;

/// A colour of the interface, grouped by area in the menu's order.
enum class UiColour : u8 {
    DamageNumber,      ///< A damage number.
    HealingNumber,     ///< A healing number.
    PoisonNumber,      ///< A poison number.
    BurnNumber,        ///< A burn number.
    DarkDamageNumber,  ///< A damage number with the outline and the shadow off.
    DarkHealingNumber, ///< A healing number with the outline and the shadow off.
    DarkPoisonNumber,  ///< A poison number with the outline and the shadow off.
    DarkBurnNumber,    ///< A burn number with the outline and the shadow off.
    DamageEffect,      ///< The effects of damage-element abilities.
    HealingEffect,     ///< The effects of healing-element abilities.
    PoisonEffect,      ///< The effects of poison-element abilities.
    BurnEffect,        ///< The effects of burn-element abilities.
    Label,             ///< A name label.
    Caption,           ///< The caption beside the tablet.
    TextInk,           ///< The ink of text's outline and shadow, and of labels and captions when both are off.
    HoverOutline,      ///< The outline of the hovered creature.
    SelectedOutline,   ///< The outline of the selected creature.
    HealthBar,         ///< The player's health bar.
    QiBar,             ///< The Qi bar.
    EnemyHealthBar,    ///< An enemy's health bar.
    BarBackground,     ///< Behind every bar.
    HudText,           ///< The HUD's text.
    HudCooling,        ///< The HUD's slot on cooldown.
    HudWarning,        ///< The HUD's warning.
};
/// How many colours UiColour names.
inline constexpr std::size_t UI_COLOURS = 24;

/// What the menu and the settings file call an area.
struct ColourAreaInfo {
    /// Its key in the settings file (`colours: { combat_text: ... }`).
    std::string_view key;
    /// The string that names it in the menu.
    StringId name;
};

/// Each ColourArea's info, in ColourArea's order.
inline constexpr std::array<ColourAreaInfo, COLOUR_AREAS> COLOUR_AREA_INFO{ {
    { .key = "combat_text", .name = StringId::ColourAreaCombatText },
    { .key = "effects", .name = StringId::ColourAreaEffects },
    { .key = "names_and_captions", .name = StringId::ColourAreaNamesAndCaptions },
    { .key = "selection", .name = StringId::ColourAreaSelection },
    { .key = "hud", .name = StringId::ColourAreaHud },
} };

/// Where the menu and the settings file put a colour.
struct UiColourInfo {
    /// The area it sits in.
    ColourArea area;
    /// Its key within its area in the settings file.
    std::string_view key;
    /// The string that names it in the menu, under its area's.
    StringId name;
};

/// Each UiColour's info, in UiColour's order: grouped by area, in the menu's order.
inline constexpr std::array<UiColourInfo, UI_COLOURS> UI_COLOUR_INFO{ {
    { .area = ColourArea::CombatText, .key = "damage", .name = StringId::ColourDamageNumber },
    { .area = ColourArea::CombatText, .key = "healing", .name = StringId::ColourHealingNumber },
    { .area = ColourArea::CombatText, .key = "poison", .name = StringId::ColourPoisonNumber },
    { .area = ColourArea::CombatText, .key = "burn", .name = StringId::ColourBurnNumber },
    { .area = ColourArea::CombatText, .key = "damage_no_outline", .name = StringId::ColourDarkDamageNumber },
    { .area = ColourArea::CombatText, .key = "healing_no_outline", .name = StringId::ColourDarkHealingNumber },
    { .area = ColourArea::CombatText, .key = "poison_no_outline", .name = StringId::ColourDarkPoisonNumber },
    { .area = ColourArea::CombatText, .key = "burn_no_outline", .name = StringId::ColourDarkBurnNumber },
    { .area = ColourArea::Effects, .key = "damage", .name = StringId::ColourDamageEffect },
    { .area = ColourArea::Effects, .key = "healing", .name = StringId::ColourHealingEffect },
    { .area = ColourArea::Effects, .key = "poison", .name = StringId::ColourPoisonEffect },
    { .area = ColourArea::Effects, .key = "burn", .name = StringId::ColourBurnEffect },
    { .area = ColourArea::NamesAndCaptions, .key = "label", .name = StringId::ColourLabel },
    { .area = ColourArea::NamesAndCaptions, .key = "caption", .name = StringId::ColourCaption },
    { .area = ColourArea::NamesAndCaptions, .key = "text_ink", .name = StringId::ColourTextInk },
    { .area = ColourArea::Selection, .key = "hover", .name = StringId::ColourHover },
    { .area = ColourArea::Selection, .key = "selected", .name = StringId::ColourSelected },
    { .area = ColourArea::Hud, .key = "health_bar", .name = StringId::ColourHealthBar },
    { .area = ColourArea::Hud, .key = "qi_bar", .name = StringId::ColourQiBar },
    { .area = ColourArea::Hud, .key = "enemy_health_bar", .name = StringId::ColourEnemyHealthBar },
    { .area = ColourArea::Hud, .key = "bar_background", .name = StringId::ColourBarBackground },
    { .area = ColourArea::Hud, .key = "text", .name = StringId::ColourHudText },
    { .area = ColourArea::Hud, .key = "cooling", .name = StringId::ColourHudCooling },
    { .area = ColourArea::Hud, .key = "warning", .name = StringId::ColourHudWarning },
} };

/// The colour of the effects of an ability whose element is `element`: by element, never by ability.
[[nodiscard]] constexpr UiColour EffectColour(HitKind element) {
    return static_cast<UiColour>(std::to_underlying(UiColour::DamageEffect) + std::to_underlying(element));
}

/// The colour of a number of `kind`: its dark variant, for text with the outline and the shadow both off, when `dark`.
[[nodiscard]] constexpr UiColour NumberColour(HitKind kind, bool dark) {
    const u8 first = std::to_underlying(dark ? UiColour::DarkDamageNumber : UiColour::DamageNumber);
    return static_cast<UiColour>(first + std::to_underlying(kind));
}

/// The colours as drawn, in UiColour's order: linear (the scene is drawn in linear space) and premultiplied, packed with
/// red in the low byte, as TextItem::colour is. Those that existed before the table are what they were, bit for bit; the
/// new ones are their sRGB colours (health bar and enemy health bar `#D8574A`, Qi bar `#5AA9E6`, bar background `#20242A`, HUD
/// text `#F4EFE6`, cooling `#8A8F96`, warning `#F08A5D`; effects: damage `#CFE3FF`, healing `#8FE3A8`, poison `#7CD957`, burn
/// `#FF8A3D`).
inline constexpr std::array<u32, UI_COLOURS> DEFAULT_UI_COLOURS{ {
    0xFFFF'FFFFU, // damage number: linear (1, 1, 1)
    0xFF25'B533U, // healing number: sRGB #7CDB6A
    0xFFCE'336FU, // poison number: sRGB #B07CE8
    0xFF0C'52FFU, // burn number: sRGB #FF9A3C
    0xFF06'0404U, // damage number, no outline: sRGB #20242A
    0xFF04'1A02U, // healing number, no outline: sRGB #145A22
    0xFF57'061DU, // poison number, no outline: sRGB #5E2A9E
    0xFF00'0839U, // burn number, no outline: sRGB #823200
    0xFFFF'C49FU, // damage effect: sRGB #CFE3FF
    0xFF64'C446U, // healing effect: sRGB #8FE3A8
    0xFF18'B133U, // poison effect: sRGB #7CD957
    0xFF0C'41FFU, // burn effect: sRGB #FF8A3D
    0xFFCA'DCE7U, // label: sRGB #F4EFE6
    0xFF5A'B1E2U, // caption: sRGB #F2D9A0
    0xFF06'0404U, // text ink: sRGB #20242A
    0xFFF0'F4F4U, // hover outline
    0xFF30'C0FFU, // selected outline
    0xFF11'18AFU, // health bar: sRGB #D8574A
    0xFFCA'651AU, // Qi bar: sRGB #5AA9E6
    0xFF11'18AFU, // enemy health bar: sRGB #D8574A
    0xFF06'0404U, // bar background: sRGB #20242A
    0xFFCA'DCE7U, // HUD text: sRGB #F4EFE6
    0xFF4E'4641U, // HUD cooling: sRGB #8A8F96
    0xFF1C'41DEU, // HUD warning: sRGB #F08A5D
} };

/// The table of UI colours the player has set; default-built, DEFAULT_UI_COLOURS.
struct UiColours {
    /// Each colour, in UiColour's order, packed as DEFAULT_UI_COLOURS' are.
    std::array<u32, UI_COLOURS> colours = DEFAULT_UI_COLOURS;

    /// `colour`'s value.
    [[nodiscard]] u32 Of(UiColour colour) const {
        return colours.at(std::to_underlying(colour));
    }

    /// Sets `colour` to `value`, packed as DEFAULT_UI_COLOURS' are.
    void Set(UiColour colour, u32 value) {
        colours.at(std::to_underlying(colour)) = value;
    }
};

/// The alpha of a fully opaque colour.
inline constexpr u8 OPAQUE_ALPHA = 255;

/// A colour in sRGB, as the menu shows it: straight (not premultiplied) 8-bit channels.
struct Srgb {
    /// Red.
    u8 red = 0;
    /// Green.
    u8 green = 0;
    /// Blue.
    u8 blue = 0;
    /// Opacity.
    u8 alpha = OPAQUE_ALPHA;
};

/// The table's packed value (linear, premultiplied) of the sRGB colour (`red`, `green`, `blue`) at opacity `alpha`.
[[nodiscard]] u32 FromSrgb(u8 red, u8 green, u8 blue, u8 alpha);

/// The sRGB colour of a packed table value: FromSrgb()'s inverse, exact on every default.
[[nodiscard]] Srgb ToSrgb(u32 packed);

} // namespace core
