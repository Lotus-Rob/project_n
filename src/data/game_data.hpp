#pragma once

/// @file
/// @ingroup data
/// The data tables (design doc 0002.3 §7): `assets/data/creatures.yaml` into core::GameData.

#include "core/error.hpp"
#include "core/game_data.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace data {

/// The game data `creatures` (creatures.yaml's text) holds. Fails, naming the line where it can, on
/// YAML that doesn't parse, a creature or field missing, a value of the wrong type or not finite,
/// or a sprite the atlas doesn't have. A key it doesn't know is ignored, with a line naming it added
/// to `warnings` when given.
[[nodiscard]] std::expected<core::GameData, core::Error> ParseGameData(
    std::string_view creatures,
    std::vector<std::string>* warnings = nullptr
);

/// The game data in `folder` (`assets/data/`), or why it couldn't be read; an unknown key (a typo) is
/// logged as a warning.
[[nodiscard]] std::expected<core::GameData, core::Error> LoadGameData(const std::filesystem::path& folder);

} // namespace data
