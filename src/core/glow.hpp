#pragma once

/// @file
/// @ingroup core
/// Glows (`assets/data/effects.yaml`'s `glows:` and `glow_roles:`): the look of the light or ink a sprite wears round its
/// edge, and the drive that moves it over time. Presentation only: the logic thread never reads a look. Code knows a closed
/// set of roles (GlowRole); the data maps each role to a named look. A drive's strength is a pure function (GlowStrength()) of
/// the inputs the game supplies and the frame's time, so nothing is kept between frames; the shader mirrors its maths.

#include "core/archive.hpp"
#include "core/hit.hpp"
#include "core/manifest.hpp"
#include "core/types.hpp"
#include "core/ui_colours.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <utility>

namespace core {

/// What a sprite's glow is for, the closed set code names; `effects.yaml`'s `glow_roles:` maps each to a glow by name.
enum class GlowRole : u8 {
    None,         ///< No glow.
    Selected,     ///< The selection outline.
    Hover,        ///< The hover outline.
    FlyingSwords, ///< A flying sword's aura.
    FireBolt,     ///< The fire bolt's aura.
    DemoPulse,    ///< The demo's pulsing glow.
    DemoCharge,   ///< The demo's charging glow.
    DemoProgress, ///< The demo's progress glow.
};
/// How many roles GlowRole names, None included.
inline constexpr std::size_t GLOW_ROLES = 8;

/// Each role's key in `glow_roles:`, in GlowRole's order; None has none (empty).
inline constexpr std::array<std::string_view, GLOW_ROLES> GLOW_ROLE_KEYS{
    { "", "selected", "hover", "flying_swords", "fire_bolt", "demo_pulse", "demo_charge", "demo_progress" }
};

/// How a glow meets what is under it.
enum class GlowBlend : u8 {
    Light, ///< Adds light: best on dark ground.
    Ink,   ///< Paints over: reads on the pale paper.
};

/// How a drive shapes a number from 0 to 1.
enum class GlowCurve : u8 {
    Linear,  ///< t.
    EaseIn,  ///< t squared.
    EaseOut, ///< 1 - (1 - t) squared.
    Smooth,  ///< Smoothstep.
    Sine,    ///< Half a cosine.
};

/// What moves a glow over time.
enum class DriveKind : u8 {
    Steady,   ///< Full strength.
    Pulse,    ///< Dips and returns every beat.
    Charge,   ///< Follows a value while charging, fades after release.
    Progress, ///< Follows a value, then shows a result's colour.
};

/// How many result colours a progress drive holds, at most.
inline constexpr std::size_t MAX_GLOW_RESULTS = 8;
/// The shortest beat of a pulse in seconds: nothing flashes more than three times a second.
inline constexpr f32 MIN_PULSE_SECONDS = 1.0F / 3.0F;
/// How far reduce flashing scales the opacity of light glows (the hook's value until chunk 11's setting).
inline constexpr f32 REDUCED_LIGHT_OPACITY = 0.5F;
/// The most a glow's `size` may be: the distance field's reach, in texels.
inline constexpr f32 MAX_GLOW_SIZE = static_cast<f32>(FIELD_REACH_TEXELS);

/// A glow's colour: a palette colour, or the element colour of the ability that draws it.
struct GlowColour {
    /// Whether it is the ability's element colour (then `colour` is unused).
    bool element = false;
    /// The palette colour otherwise.
    UiColour colour = UiColour::DamageEffect;

    /// Lists its fields for an archive (core/archive.hpp), which hashes them.
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.element, self.colour);
    }
};

/// What moves a glow: the kind picks which of the other fields count.
struct GlowDrive {
    /// Which drive.
    DriveKind kind = DriveKind::Steady;
    /// A pulse's beat in seconds, at least MIN_PULSE_SECONDS.
    f32 periodSeconds = 1.6F;
    /// How far a pulse falls below full strength, 0 to 1.
    f32 depth = 0.9F;
    /// A pulse's shape of each half beat, or the curve of a charge's or a progress's value.
    GlowCurve curve = GlowCurve::Sine;
    /// How long a charge takes to fade after its release, in seconds, above 0.
    f32 fadeSeconds = 0.4F;
    /// The shape of that fade.
    GlowCurve fadeCurve = GlowCurve::EaseOut;
    /// A progress's result colours: result n (from 1) shows `results[n - 1]`.
    std::array<UiColour, MAX_GLOW_RESULTS> results{};
    /// How many of `results` are used, at most MAX_GLOW_RESULTS.
    u8 resultCount = 0;

    /// Whether every number is in its range.
    [[nodiscard]] bool Valid() const {
        const auto fraction = [](f32 value) { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; };
        return std::isfinite(periodSeconds) && periodSeconds >= MIN_PULSE_SECONDS && fraction(depth) &&
               std::isfinite(fadeSeconds) && fadeSeconds > 0.0F && resultCount <= MAX_GLOW_RESULTS;
    }

    /// Lists its fields for an archive (core/archive.hpp), which hashes them.
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.kind, self.periodSeconds, self.depth, self.curve, self.fadeSeconds, self.fadeCurve, self.resultCount);
        for (auto& result : self.results) {
            archive(result);
        }
    }
};

/// How one glow looks. The defaults are the user's tuned flying swords' aura, without a drive.
struct GlowLook {
    /// Its colour.
    GlowColour colour{ .element = true };
    /// How it blends.
    GlowBlend blend = GlowBlend::Light;
    /// How far past the sprite's edge it reaches at zoom 1, in the sprite's texels: above 0, at most MAX_GLOW_SIZE.
    f32 size = 8.5F;
    /// 0 a hard ring, 1 a soft fade from the edge out.
    f32 feather = 0.75F;
    /// Its strength at full drive, 0 to 1.
    f32 opacity = 0.15F;
    /// 0 grows with the sprite as the camera zooms in, 1 keeps its width on screen; 0 to 1.
    f32 zoomShrink = 0.6F;
    /// What moves it.
    GlowDrive drive{};

    /// Whether every number is in its range.
    [[nodiscard]] bool Valid() const {
        const auto fraction = [](f32 value) { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; };
        return std::isfinite(size) && size > 0.0F && size <= MAX_GLOW_SIZE && fraction(feather) && fraction(opacity) &&
               fraction(zoomShrink) && drive.Valid();
    }

    /// Lists its fields for an archive (core/archive.hpp), which hashes them (Effects::Hash()).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.colour, self.blend, self.size, self.feather, self.opacity, self.zoomShrink, self.drive);
    }
};

/// The inputs the game gives a sprite's glow; the drive's kind picks the ones it reads.
struct GlowInput {
    /// Which glow.
    GlowRole role = GlowRole::None;
    /// The value before the latest tick, from 0 to 1.
    f32 previousValue = 0.0F;
    /// The value now, from 0 to 1.
    f32 currentValue = 0.0F;
    /// The tick a pulse counts from, or a charge's release.
    u64 startTick = 0;
    /// A progress's result: 0 none yet, n for `results[n - 1]`.
    u8 result = 0;
    /// The ability's element, for a glow of the element's colour.
    HitKind element = HitKind::Damage;
    /// Whether a charge was released (then `startTick` is the release).
    bool released = false;
};

/// `fraction` (held to 0..1) through `curve`: linear t; ease in t squared; ease out 1 - (1 - t) squared; smooth t squared times
/// (3 - 2t); sine (1 - cos(pi t)) / 2.
[[nodiscard]] f32 CurveAt(GlowCurve curve, f32 fraction);

/// A drive's strength from 0 to 1, which multiplies the glow's opacity. `value` is the game's value from 0 to 1, `ageTicks`
/// the ticks since the start (a pulse's beat count, a charge's release), `result` a progress's result (0 none), `released`
/// whether a charge was released and `reduceFlashing` the accessibility setting: it holds a pulse at its average strength.
/// Steady 1; pulse `1 - depth * (1 - curve(t))` with `t = |2 * frac(age / period) - 1|`, or `1 - depth * (1 - mean)` when
/// reducing flashing, `mean` the curve's over 0..1 (linear, smooth and sine 1/2, ease in 1/3, ease out 2/3); charge
/// `curve(value)` until released, then times `1 - fadeCurve(min(age / fade, 1))`; progress `curve(value)` until a result,
/// then 1.
[[nodiscard]] f32 GlowStrength(const GlowDrive& drive, f32 value, f64 ageTicks, u8 result, bool released, bool reduceFlashing);

/// A glow of the shipped table: its name in `effects.yaml` and its look.
struct NamedGlow {
    /// The name.
    std::string_view name;
    /// The look.
    GlowLook look;
};

/// How many glows ship.
inline constexpr std::size_t SHIPPED_GLOW_COUNT = 7;

namespace shipped {

/// The width of a shipped outline, in texels.
inline constexpr f32 OUTLINE_SIZE = 3.0F;
/// The reach of a shipped demo glow, in texels.
inline constexpr f32 DEMO_SIZE = 10.0F;
/// The feather of a shipped demo glow.
inline constexpr f32 DEMO_FEATHER = 0.8F;
/// The zoom shrink of a shipped demo glow.
inline constexpr f32 DEMO_ZOOM_SHRINK = 0.6F;

/// The shipped `sword_aura` and `bolt_aura` look: the user's tuned values.
inline constexpr GlowLook SWORD_AURA{
    .drive = { .kind = DriveKind::Pulse, .periodSeconds = 1.6F, .depth = 0.9F, .curve = GlowCurve::Sine }
};

/// A shipped outline: a hard ink ring 3 texels wide in `colour`, not thinning with zoom.
[[nodiscard]] constexpr GlowLook Outline(UiColour colour) {
    return { .colour = { .colour = colour },
             .blend = GlowBlend::Ink,
             .size = OUTLINE_SIZE,
             .feather = 0.0F,
             .opacity = 1.0F,
             .zoomShrink = 0.0F,
             .drive = { .kind = DriveKind::Steady } };
}

/// A shipped demo glow: ink on the paper, 10 texels, in `colour` with `drive`.
[[nodiscard]] constexpr GlowLook Demo(UiColour colour, f32 opacity, const GlowDrive& drive) {
    return { .colour = { .colour = colour },
             .blend = GlowBlend::Ink,
             .size = DEMO_SIZE,
             .feather = DEMO_FEATHER,
             .opacity = opacity,
             .zoomShrink = DEMO_ZOOM_SHRINK,
             .drive = drive };
}

} // namespace shipped

/// The shipped glows (`assets/data/effects.yaml`'s `glows:`), which a file's `glows:` adds to or replaces by name.
inline constexpr std::array<NamedGlow, SHIPPED_GLOW_COUNT> SHIPPED_GLOWS{ {
    { .name = "sword_aura", .look = shipped::SWORD_AURA },
    { .name = "bolt_aura", .look = shipped::SWORD_AURA },
    { .name = "selected", .look = shipped::Outline(UiColour::SelectedOutline) },
    { .name = "hover", .look = shipped::Outline(UiColour::HoverOutline) },
    { .name = "demo_pulse",
      .look = { .colour = { .colour = UiColour::DamageEffect },
                .blend = GlowBlend::Ink,
                .opacity = 0.6F,
                .drive = shipped::SWORD_AURA.drive } },
    { .name = "demo_charge",
      .look = shipped::Demo(
          UiColour::BurnEffect,
          0.7F,
          { .kind = DriveKind::Charge, .curve = GlowCurve::EaseIn, .fadeSeconds = 0.4F, .fadeCurve = GlowCurve::EaseOut }
      ) },
    { .name = "demo_progress",
      .look = shipped::Demo(
          UiColour::Caption,
          0.7F,
          { .kind = DriveKind::Progress,
            .curve = GlowCurve::Linear,
            .results = { UiColour::HealingEffect, UiColour::DamageEffect, UiColour::PoisonEffect, UiColour::BurnEffect },
            .resultCount = 4 }
      ) },
} };

/// The name of the shipped glow each role maps to, in GlowRole's order (None has none).
inline constexpr std::array<std::string_view, GLOW_ROLES> SHIPPED_GLOW_ROLES{
    { "", "selected", "hover", "sword_aura", "bolt_aura", "demo_pulse", "demo_charge", "demo_progress" }
};

/// The shipped look of every role, in GlowRole's order (None's is a default look).
[[nodiscard]] constexpr std::array<GlowLook, GLOW_ROLES> ShippedGlowTable() {
    std::array<GlowLook, GLOW_ROLES> table{};
    for (std::size_t role = 1; role < GLOW_ROLES; ++role) {
        for (const NamedGlow& glow : SHIPPED_GLOWS) {
            if (glow.name == SHIPPED_GLOW_ROLES.at(role)) {
                table.at(role) = glow.look;
            }
        }
    }
    return table;
}

} // namespace core
