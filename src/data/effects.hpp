#pragma once

/// @file
/// @ingroup data
/// The effects table: `assets/data/effects.yaml` into core::Effects, the looks of the abilities, the health bars and the
/// layout of the HUD.

#include "core/effects.hpp"
#include "core/error.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace data {

/// The effects `effects` (effects.yaml's text) holds: an `abilities:` map (`fire_bolt` and `flying_swords`: sword_scale,
/// halo_scale, core_scale, trail in seconds, trail_width, trail_taper, halo_intensity, core_opacity; `poison_lance`:
/// flash_start_scale, flash_end_scale, flash_intensity), `health_bar:` (rise, width, height, fill_width, fill_height) and
/// `hud:` (margin, bar_width, bar_height, line_height, bar_gap, bar_text_inset, bar_text_rise, not_enough_qi in seconds,
/// cooling_ink_opacity), `glows:` (named looks: colour as a palette `area.key` or `element`, blend, size, feather, opacity,
/// zoom_shrink and a drive of kind steady, pulse, charge or progress; a glow named like a shipped one changes only the keys
/// it gives, a new one starts from the sword aura's) and `glow_roles:` (which glow each role wears, all or
/// none; a pulse faster than three a second is refused). A section or key left out keeps the shipped number (core::Effects).
/// Fails, naming the line where it can, on YAML that doesn't parse or a value that isn't a number or is out of range (a scale,
/// a width or a time of 0 or less, an opacity above 1). A key it doesn't know is ignored, with a line naming it added to
/// `warnings` when given.
[[nodiscard]] std::expected<core::Effects, core::Error> ParseEffects(
    std::string_view effects,
    std::vector<std::string>* warnings = nullptr
);

/// The effects in `folder` (`assets/data/`), or why they couldn't be read; an unknown key (a typo) is logged as a warning.
[[nodiscard]] std::expected<core::Effects, core::Error> LoadEffects(const std::filesystem::path& folder);

} // namespace data
