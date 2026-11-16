#pragma once

/// @file
/// @ingroup app
/// The temporary HUD: the player's health and Qi as bars with their numbers written on them,
/// and each slot's control, ability and cooldown, at the bottom left. Pure: it lays out lines and bars in target pixels,
/// which the renderer draws (gfx::HudDraw), and tests read them without a device.

#include "app/bindings.hpp"
#include "core/effects.hpp"
#include "core/frame.hpp"
#include "core/rect.hpp"
#include "core/strings.hpp"
#include "core/types.hpp"
#include "core/ui_colours.hpp"
#include "core/vec2.hpp"
#include "gfx/hud_draw.hpp"
#include "gfx/renderer.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace app {

/// The action a press of `control` at window position `position` sends when it lands in the date line's `area`: the world's
/// pause toggled, and nothing else (the press is the HUD's, never the world's). None for another control, a position
/// outside the area (its top and left edges in, its bottom and right out) or no area (no date line showing).
[[nodiscard]] std::optional<core::Action> DateLineClick(
    Control control,
    core::Vec2 position,
    const std::optional<core::Rect>& area
);

/// Lays out the temporary HUD each frame from the player's casting state (core::CasterView). At the bottom left, top to
/// bottom: the health bar (core::UiColour::HealthBar on BarBackground) with "Health {value}/{max}" on it, the Qi bar (QiBar on
/// BarBackground) with "Qi {value}/{max}" on it, then a line per slot in core::Slot's order: its control, its ability's
/// name or "—", and while it cools down its seconds left, rounded up. Every value shows as a number as well as a length;
/// a slot cooling down is drawn in HudCooling and shows its seconds; for `notEnoughQiSeconds` after a NotEnoughQi event,
/// the Qi bar's text reads "Not enough Qi" in HudWarning. Every colour comes from the table, the text from the strings.
/// With the text's outline and shadow both off, the slots' lines, over the ground, take TextInk as the labels do, as opaque
/// as HudText is (a cooling one at the layout's `coolingInkOpacity` of that), so they read on the pale ground; the bars' words,
/// on their bars, keep theirs. Every size, gap and time is core::HudLayout's, the shipped one until SetEffects().
class Hud {
public:
    /// A HUD showing `strings` as `text` says, the renderer's text options: in its language, its sizes its scale times
    /// those at text scale 1 (the renderer draws the lines at that scale too), its slots' colours following its outline
    /// and shadow; in `colours`.
    Hud(std::shared_ptr<const core::Strings> strings, const gfx::TextOptions& text, const core::UiColours& colours);

    /// Draws in `colours` from the next Build() on.
    void SetColours(const core::UiColours& colours);

    /// Lays out as `effects`' HUD layout says from the next Build() on, with the warning playing as it was.
    void SetEffects(const core::Effects& effects);

    /// Lays out as `text` says from the next Build() on.
    void SetTextOptions(const gfx::TextOptions& text);

    /// Plays `event` at render tick `renderTick`: a NotEnoughQi makes the Qi bar read "Not enough Qi" for
    /// the layout's `notEnoughQiSeconds` from then. Any other event changes nothing.
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

    /// Appends to `lines` the date line for `calendar` on a target `width` pixels wide, and keeps its area (DateLineArea()):
    /// the month and day counted from 1 as StringId::DateLine says, and while the calendar is paused StringId::DatePaused
    /// after it, in the slots' text colour. The line ends at the box's right edge, `dateInset` in from the top right; the
    /// box is `dateWidth` wide and `lineHeight` tall, times the text scale. With no calendar (not the world map) it appends
    /// nothing and the area is none. The text lives in the HUD, valid until the next BuildDate().
    void BuildDate(const std::optional<core::CalendarView>& calendar, u32 width, std::vector<gfx::HudLine>& lines);

    /// The date line's box from the last BuildDate(), in target pixels: what a click pauses by (DateLineClick()). None
    /// while no date line shows.
    [[nodiscard]] std::optional<core::Rect> DateLineArea() const;

private:
    // The text of `string` in the HUD's language.
    [[nodiscard]] const std::string& Text(core::StringId string) const;

    std::shared_ptr<const core::Strings> m_strings;
    gfx::TextOptions m_text;
    core::UiColours m_colours;
    core::HudLayout m_layout{};                      // the shipped layout until SetEffects()
    std::optional<f64> m_warnedUntil = std::nullopt; // the render tick the warning ends at
    // Each line's text, the bars' first: kept from frame to frame, so a frame allocates nothing once they have grown.
    std::array<std::string, 2 + core::SLOTS> m_texts{};
    std::optional<core::Rect> m_dateArea = std::nullopt;
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::string m_date{}; // the date line's text, kept likewise
    std::string m_seconds{};
    // NOLINTEND(readability-redundant-member-init)
};

} // namespace app
