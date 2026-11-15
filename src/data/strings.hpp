#pragma once

/// @file
/// @ingroup data
/// The strings table (design doc 0002.4.2 §5): `assets/data/strings.yaml` into core::Strings.

#include "core/error.hpp"
#include "core/strings.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace data {

/// The strings `yaml` (strings.yaml's text) holds. Fails, naming the line, on YAML that doesn't parse, a string of
/// core::STRING_NAMES missing, a string there twice, a language missing from one, or one's text empty or not text. An
/// entry the code doesn't name is a data string (core::Strings::Find); a language it doesn't know is ignored, with a
/// line naming it added to `warnings` when given.
[[nodiscard]] std::expected<core::Strings, core::Error> ParseStrings(
    std::string_view yaml,
    std::vector<std::string>* warnings = nullptr
);

/// ParseStrings() for a table that holds at most `most` strings, named and data together (core::MOST_STRINGS when larger):
/// a data string past it is refused, naming its line. The overload above caps at core::MOST_STRINGS; a smaller cap lets a
/// small table reach the refusal.
[[nodiscard]] std::expected<core::Strings, core::Error> ParseStrings(
    std::string_view yaml,
    std::size_t most,
    std::vector<std::string>* warnings = nullptr
);

/// The strings in `folder` (`assets/data/`), or why they couldn't be read; an unknown language (a typo) is logged
/// as a warning.
[[nodiscard]] std::expected<core::Strings, core::Error> LoadStrings(const std::filesystem::path& folder);

} // namespace data
