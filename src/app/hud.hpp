#pragma once

/// @file
/// @ingroup app
/// The temporary HUD (the phase 6 chunk 4 spec): the player's health and Qi as bars with their numbers written on them,
/// and each slot's control, ability and cooldown, at the bottom left. Pure: it lays out lines and bars in target pixels,
/// which the renderer draws (gfx::HudDraw), and tests read them without a device.

#include "core/frame.hpp"
#include "core/strings.hpp"
#include "core/types.hpp"
#include "core/ui_colours.hpp"
#include "gfx/renderer.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace app {

/// How far the HUD sits in from the target's left and bottom edges, in target pixels, whatever the text size.
inline constexpr f32 HUD_MARGIN = 16.0F;
/// A bar's width in target pixels at text scale 1.
inline constexpr f32 HUD_BAR_WIDTH = 240.0F;
/// A bar's height in target pixels at text scale 1.
inline constexpr f32 HUD_BAR_HEIGHT = 18.0F;
/// How far apart the HUD's lines of slots are, top to top, in target pixels at text scale 1: a line of
/// gfx::HUD_PIXELS text and the gap under it.
inline constexpr f32 HUD_LINE_HEIGHT = 24.0F;
/// How long the Qi bar reads "Not enough Qi" after a cast was refused for want of it, in seconds.
inline constexpr f64 NOT_ENOUGH_QI_SECONDS = 1.0;

/// Lays out the temporary HUD each frame from the player's casting state (core::CasterView). At the bottom left, top to
/// bottom: the health bar (core::UiColour::HealthBar on BarBackground) with "Health {value}/{max}" on it, the Qi bar (QiBar on
/// BarBackground) with "Qi {value}/{max}" on it, then a line per slot in core::Slot's order: its control, its ability's
/// name or "—", and while it cools down its seconds left, rounded up. Every value shows as a number as well as a length;
/// a slot cooling down is drawn in HudCooling and shows its seconds; for NOT_ENOUGH_QI_SECONDS after a NotEnoughQi event,
/// the Qi bar's text reads "Not enough Qi" in HudWarning. Every colour comes from the table, the text from the strings.
class Hud {
public:
    /// A HUD showing `strings` in `language`, in `colours`, its sizes `scale` times those at text scale 1 (the text size
    /// setting, gfx::TextOptions::scale, which the renderer draws its lines at).
    Hud(std::shared_ptr<const core::Strings> strings,
        core::Language language,
        const core::UiColours& colours,
        f32 scale = 1.0F);

    /// Draws in `colours` from the next Build() on.
    void SetColours(const core::UiColours& colours);

    /// Plays `event` at render tick `renderTick`: a NotEnoughQi makes the Qi bar read "Not enough Qi" for
    /// NOT_ENOUGH_QI_SECONDS from then. Any other event changes nothing.
    void Play(const core::Event& event, f64 renderTick);

    /// Forgets the warning playing, for a new timeline, where it never happened.
    void Clear();

    /// Appends to `lines` and `bars` the HUD for `caster` at render tick `renderTick` on a target `height` pixels tall:
    /// the health line, the Qi line, then a line per slot, and the health bar, then the Qi bar. A slot cools down while
    /// its ready tick is past `renderTick`. A bar's fraction is its value over its most, within 0 to 1, and 0 with no most.
    /// The lines' texts live in the HUD, valid until the next Build().
    void Build(
        const core::CasterView& caster,
        f64 renderTick,
        u32 height,
        std::vector<gfx::HudLine>& lines,
        std::vector<gfx::HudBar>& bars
    );

private:
    // The text of `string` in the HUD's language.
    [[nodiscard]] const std::string& Text(core::StringId string) const;

    std::shared_ptr<const core::Strings> m_strings;
    core::Language m_language;
    core::UiColours m_colours;
    f32 m_scale;
    std::optional<f64> m_warnedUntil = std::nullopt; // the render tick the warning ends at
    // Each line's text, the bars' first: kept from frame to frame, so a frame allocates nothing once they have grown.
    std::array<std::string, 2 + core::SLOTS> m_texts{};
    std::string m_seconds{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
};

} // namespace app
