#pragma once

/// @file
/// @ingroup data
/// The settings file (design doc 0002.3 §7): `settings.yaml` in the per-user data folder.

#include "core/error.hpp"
#include "core/settings.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace data {

/// The settings `yaml` holds; a setting it leaves out keeps its default. Fails on YAML that doesn't
/// parse, a document that isn't a map of settings, a window setting of the wrong type, or a window size outside
/// 320x200 to 16384x16384, naming the line.
/// A setting it doesn't know is ignored, and a language or damage number motion it doesn't know, or a
/// `combined_damage_numbers`, `text_outline` or `text_shadow` that isn't a boolean, keeps its default (English, bounce,
/// off, on, on), each with a line naming it added to `warnings` when given.
[[nodiscard]] std::expected<core::Settings, core::Error> ParseSettings(
    std::string_view yaml,
    std::vector<std::string>* warnings = nullptr
);

/// Writes `settings` to `path` as YAML that ParseSettings() reads back, with a comment on each setting.
[[nodiscard]] std::expected<void, core::Error> SaveSettings(const std::filesystem::path& path, const core::Settings& settings);

/// The settings in `path`. A missing file gives the defaults, written out so the player finds a file
/// to edit; a file that can't be read or parsed gives the defaults with a warning, and is left alone.
/// An unknown setting (a typo) is logged as a warning.
[[nodiscard]] core::Settings LoadSettings(const std::filesystem::path& path);

} // namespace data
