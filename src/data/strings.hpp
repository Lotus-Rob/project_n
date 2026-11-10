#pragma once

/// @file
/// @ingroup data
/// The strings table (design doc 0002.4.2 §5): `assets/data/strings.yaml` into core::Strings.

#include "core/error.hpp"
#include "core/strings.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace data {

/// The strings `yaml` (strings.yaml's text) holds. Fails, naming the line, on YAML that doesn't parse, a string of
/// core::STRING_NAMES missing or there twice, a language missing from one, or one's text empty or not text. A string or
/// language it doesn't know is ignored, with a line naming it added to `warnings` when given.
[[nodiscard]] std::expected<core::Strings, core::Error> ParseStrings(
    std::string_view yaml,
    std::vector<std::string>* warnings = nullptr
);

/// The strings in `folder` (`assets/data/`), or why they couldn't be read; an unknown string or language (a typo) is
/// logged as a warning.
[[nodiscard]] std::expected<core::Strings, core::Error> LoadStrings(const std::filesystem::path& folder);

} // namespace data
