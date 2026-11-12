#pragma once

/// @file
/// @ingroup core
/// The effects' looks and the HUD's layout (`assets/data/effects.yaml`): every number that tunes how an ability, a health
/// bar or the HUD is drawn, none of which changes what happens. Presentation, not game data: it is not hashed or saved, and
/// the main thread reloads it as the file is edited, the renderer and the HUD drawing from the new table at the next frame.

#include "core/abilities.hpp"
#include "core/archive.hpp"
#include "core/glow.hpp"
#include "core/types.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace core {

/// The shipped numbers of the effects (assets/data/effects.yaml), which the structs below default to.
namespace shipped {

/// The shipped `swordScale` of SwordLook.
inline constexpr f32 SWORD_SCALE = 0.9F;
/// The shipped `trailTicks` of SwordLook.
inline constexpr f32 TRAIL_TICKS = 6.0F;
/// The shipped `trailWidth` of SwordLook.
inline constexpr f32 TRAIL_WIDTH = 5.0F;
/// The shipped `trailTaper` of SwordLook.
inline constexpr f32 TRAIL_TAPER = 0.6F;
/// The shipped `trailIntensity` of SwordLook: the element's colour at full strength.
inline constexpr f32 TRAIL_INTENSITY = 1.0F;
/// The shipped `intensity` of BurstLook.
inline constexpr f32 FLASH_INTENSITY = 1.0F;
/// The shipped `notEnoughQiSeconds` of HudLayout.
inline constexpr f64 HUD_NOT_ENOUGH_QI_SECONDS = 1.0;
/// The fire bolt's `swordScale`: its sprite at its own size.
inline constexpr f32 BOLT_SWORD_SCALE = 1.0F;
/// The shipped `startScale` of BurstLook.
inline constexpr f32 FLASH_START_SCALE = 0.5F;
/// The shipped `endScale` of BurstLook.
inline constexpr f32 FLASH_END_SCALE = 1.9F;
/// The shipped `rise` of HealthBarLook.
inline constexpr f32 HEALTH_BAR_RISE = 5.0F;
/// The shipped `width` of HealthBarLook.
inline constexpr f32 HEALTH_BAR_WIDTH = 44.0F;
/// The shipped `height` of HealthBarLook.
inline constexpr f32 HEALTH_BAR_HEIGHT = 6.0F;
/// The shipped `fillWidth` of HealthBarLook.
inline constexpr f32 HEALTH_BAR_FILL_WIDTH = 40.0F;
/// The shipped `fillHeight` of HealthBarLook.
inline constexpr f32 HEALTH_BAR_FILL_HEIGHT = 4.0F;
/// The shipped `margin` of HudLayout.
inline constexpr f32 HUD_MARGIN = 16.0F;
/// The shipped `barWidth` of HudLayout.
inline constexpr f32 HUD_BAR_WIDTH = 240.0F;
/// The shipped `barHeight` of HudLayout.
inline constexpr f32 HUD_BAR_HEIGHT = 18.0F;
/// The shipped `lineHeight` of HudLayout.
inline constexpr f32 HUD_LINE_HEIGHT = 24.0F;
/// The shipped `barGap` of HudLayout.
inline constexpr f32 HUD_BAR_GAP = 6.0F;
/// The shipped `barTextInset` of HudLayout.
inline constexpr f32 HUD_BAR_TEXT_INSET = 6.0F;
/// The shipped `barTextRise` of HudLayout.
inline constexpr f32 HUD_BAR_TEXT_RISE = 7.0F;
/// The shipped `coolingInkOpacity` of HudLayout.
inline constexpr f32 HUD_COOLING_INK_OPACITY = 0.55F;

} // namespace shipped

/// How one projectile ability's swords (or its bolt) and their trails are drawn; each blade's glow is its role's
/// (Effects::glows). Sizes are multiples of their sprites; the defaults are the flying swords'.
struct SwordLook {
    /// How much larger than its image (10 by 40) a sword is drawn, above 0: 9 by 36 world units, so on rings a ring step
    /// (38) apart one ring's tips nearly touch the next ring's hilts.
    f32 swordScale = shipped::SWORD_SCALE;
    /// How far back in time a trail reaches, in simulation ticks, above 0: its tail is where the sword was then. Chunk 11's
    /// reduce motion shortens it.
    f32 trailTicks = shipped::TRAIL_TICKS;
    /// A trail's width at the sword, in world units, above 0: a thin streak, narrower than the drawn blade.
    f32 trailWidth = shipped::TRAIL_WIDTH;
    /// How much of that width its tail loses, from 0 (as wide as the head) to 1 (a point): it narrows as it fades.
    f32 trailTaper = shipped::TRAIL_TAPER;
    /// How bright the trail is, as a fraction of its element's colour, 0 or more: additive light. Chunk 11's reduce
    /// flashing lowers it.
    f32 trailIntensity = shipped::TRAIL_INTENSITY;

    /// Whether every size, length and width is finite and above 0, the taper is from 0 to 1 and the intensity is finite and
    /// not negative; what a look read from the data must satisfy.
    [[nodiscard]] bool Valid() const {
        const auto positive = [](f32 value) { return std::isfinite(value) && value > 0.0F; };
        const auto fraction = [](f32 value) { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; };
        return positive(swordScale) && positive(trailTicks) && positive(trailWidth) && fraction(trailTaper) &&
               std::isfinite(trailIntensity) && trailIntensity >= 0.0F;
    }

    /// Lists its fields for an archive (core/archive.hpp), which hashes them (Effects::Hash()).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.swordScale, self.trailTicks, self.trailWidth, self.trailTaper, self.trailIntensity);
    }
};

/// How the poison lance's burst is drawn.
struct BurstLook {
    /// How large the burst is drawn at its start, as a multiple of its sprite (96 world units across), above 0.
    f32 startScale = shipped::FLASH_START_SCALE;
    /// How large it has grown at its end, above 0: about the lance's area across.
    f32 endScale = shipped::FLASH_END_SCALE;
    /// How bright it is, as a fraction of its element's colour, 0 or more: additive light, like the trails. Chunk 11's reduce
    /// flashing lowers it.
    f32 intensity = shipped::FLASH_INTENSITY;

    /// Whether both scales are finite and above 0 and the intensity is finite and not negative.
    [[nodiscard]] bool Valid() const {
        const auto positive = [](f32 value) { return std::isfinite(value) && value > 0.0F; };
        return positive(startScale) && positive(endScale) && std::isfinite(intensity) && intensity >= 0.0F;
    }

    /// Lists its fields for an archive (core/archive.hpp), which hashes them (Effects::Hash()).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.startScale, self.endScale, self.intensity);
    }
};

/// How an enemy's health bar is drawn, in world units.
struct HealthBarLook {
    /// How far over a creature's head the bar's middle sits, 0 or more.
    f32 rise = shipped::HEALTH_BAR_RISE;
    /// The bar's background width, above 0: the fill sits centred inside it.
    f32 width = shipped::HEALTH_BAR_WIDTH;
    /// The background's height, above 0.
    f32 height = shipped::HEALTH_BAR_HEIGHT;
    /// The fill's length at full health, above 0.
    f32 fillWidth = shipped::HEALTH_BAR_FILL_WIDTH;
    /// The fill's height, above 0.
    f32 fillHeight = shipped::HEALTH_BAR_FILL_HEIGHT;

    /// Whether the rise is finite and not negative and every size is finite and above 0.
    [[nodiscard]] bool Valid() const {
        const auto positive = [](f32 value) { return std::isfinite(value) && value > 0.0F; };
        return std::isfinite(rise) && rise >= 0.0F && positive(width) && positive(height) && positive(fillWidth) &&
               positive(fillHeight);
    }

    /// Lists its fields for an archive (core/archive.hpp), which hashes them (Effects::Hash()).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.rise, self.width, self.height, self.fillWidth, self.fillHeight);
    }
};

/// How the temporary HUD is laid out, in target pixels at text scale 1 (the text scale multiplies every one but the margin).
struct HudLayout {
    /// How far the HUD sits in from the target's left and bottom edges, whatever the text size; 0 or more.
    f32 margin = shipped::HUD_MARGIN;
    /// A bar's width, above 0.
    f32 barWidth = shipped::HUD_BAR_WIDTH;
    /// A bar's height, above 0.
    f32 barHeight = shipped::HUD_BAR_HEIGHT;
    /// How far apart the HUD's lines of slots are, top to top, above 0: a line of text and the gap under it.
    f32 lineHeight = shipped::HUD_LINE_HEIGHT;
    /// The gap between the bars and between the bars and the slots, 0 or more.
    f32 barGap = shipped::HUD_BAR_GAP;
    /// How far in from a bar's left its words start, 0 or more.
    f32 barTextInset = shipped::HUD_BAR_TEXT_INSET;
    /// How far a bar's words are raised, 0 or more: the text is taller than its letters, which this centres on the bar.
    f32 barTextRise = shipped::HUD_BAR_TEXT_RISE;
    /// How long the Qi bar reads "Not enough Qi" after a cast was refused for want of it, in seconds, above 0.
    f64 notEnoughQiSeconds = shipped::HUD_NOT_ENOUGH_QI_SECONDS;
    /// How opaque a cooling slot's line is, from 0 to 1, in the text ink, when the text has neither outline nor shadow:
    /// fainter than the other slots' ink, beside the seconds it shows.
    f32 coolingInkOpacity = shipped::HUD_COOLING_INK_OPACITY;

    /// Whether every distance is finite and not negative (the sizes and the line height above 0), the warning's time is
    /// finite and above 0 and the opacity is from 0 to 1.
    [[nodiscard]] bool Valid() const {
        const auto positive = [](f32 value) { return std::isfinite(value) && value > 0.0F; };
        const auto distance = [](f32 value) { return std::isfinite(value) && value >= 0.0F; };
        return distance(margin) && positive(barWidth) && positive(barHeight) && positive(lineHeight) && distance(barGap) &&
               distance(barTextInset) && distance(barTextRise) && std::isfinite(notEnoughQiSeconds) &&
               notEnoughQiSeconds > 0.0 && distance(coolingInkOpacity) && coolingInkOpacity <= 1.0F;
    }

    /// Lists its fields for an archive (core/archive.hpp), which hashes them (Effects::Hash()).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(
            self.margin,
            self.barWidth,
            self.barHeight,
            self.lineHeight,
            self.barGap,
            self.barTextInset,
            self.barTextRise,
            self.notEnoughQiSeconds,
            self.coolingInkOpacity
        );
    }
};

/// Every presentation number of the abilities, the health bars and the HUD: `effects.yaml`'s table, the defaults its
/// shipped numbers.
struct Effects {
    /// The fire bolt's look: its sprite at its own size.
    SwordLook fireBolt{ .swordScale = shipped::BOLT_SWORD_SCALE };
    /// The flying swords' look.
    SwordLook flyingSwords{};
    /// The poison lance's burst.
    BurstLook poisonLance{};
    /// The enemies' health bars.
    HealthBarLook healthBar{};
    /// The HUD's layout.
    HudLayout hud{};
    /// Each glow role's look, indexed by GlowRole (None's is unused): `glow_roles:` resolved against `glows:`.
    std::array<GlowLook, GLOW_ROLES> glows = ShippedGlowTable();

    /// The look of `kind`'s projectiles: the bolt's for the fire bolt, else the swords'.
    [[nodiscard]] const SwordLook& LookOf(AbilityKind kind) const {
        return kind == AbilityKind::FireBolt ? fireBolt : flyingSwords;
    }

    /// Whether every part is valid.
    [[nodiscard]] bool Valid() const {
        return fireBolt.Valid() && flyingSwords.Valid() && poisonLance.Valid() && healthBar.Valid() && hud.Valid() &&
               std::ranges::all_of(glows, [](const GlowLook& glow) { return glow.Valid(); });
    }

    /// Lists its fields for an archive (core/archive.hpp), which hashes them (Hash()).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.fireBolt, self.flyingSwords, self.poisonLance, self.healthBar, self.hud);
        for (auto& glow : self.glows) {
            archive(glow);
        }
    }

    /// A hash of every value: two tables hash alike when every value has the same bits. Not part of the game data's hash:
    /// effects change nothing in a replay.
    [[nodiscard]] u64 Hash() const {
        Hasher hasher;
        hasher(*this);
        return hasher.Value();
    }
};

} // namespace core
