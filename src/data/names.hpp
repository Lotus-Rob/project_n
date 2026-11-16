#pragma once

/// @file
/// @ingroup data
/// The names table (phase 6 chunk 5b2): `assets/data/names.yaml` into core::Names.

#include "core/error.hpp"
#include "core/names.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace data {

/// The names `yaml` (names.yaml's text) holds: `surnames:` and `given:`, each a list of `{ zh, pinyin, weight }` (`weight`,
/// a whole number from 1, on surnames only; 1 when absent). Fails, naming the line, on YAML that doesn't parse, a list
/// missing, empty or past core::MOST_NAMES, an entry without text in `zh` or `pinyin`, a pinyin that isn't lower-case ASCII
/// letters, a weight that isn't a whole number from 1 to 65,535, or a weight on a given character. A key it doesn't know is
/// ignored, with a line naming it added to `warnings` when given.
[[nodiscard]] std::expected<core::Names, core::Error> ParseNames(
    std::string_view yaml,
    std::vector<std::string>* warnings = nullptr
);

/// The names in `file` (`assets/data/names.yaml`), or why it couldn't be read; an unknown key (a typo) is logged as a warning.
[[nodiscard]] std::expected<core::Names, core::Error> LoadNames(const std::filesystem::path& file);

} // namespace data
