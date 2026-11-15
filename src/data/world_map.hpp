#pragma once

/// @file
/// @ingroup data
/// The world map (phase 6 chunk 5b1): `assets/data/world.yaml` into core::WorldMap.

#include "core/error.hpp"
#include "core/strings.hpp"
#include "core/world_map.hpp"

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace data {

/// The world map `yaml` (world.yaml's text) holds, its places' names looked up in `strings` and the map checked and
/// derived by core::WorldMap::Build. The grid is a literal block scalar (`|`; a folded or quoted one is refused, as yaml-cpp
/// hides its style, by the character its mark points at) of one line per row and one kind symbol per tile; a trailing carriage
/// return or spaces on a row are dropped. Fails, naming the line, on YAML that doesn't parse, a field missing or of the wrong
/// type, a ragged or empty row, a symbol no kind has, a place whose name or kind is unknown, and whatever Build refuses. A key
/// it doesn't know, at the top or in a kind or place, is ignored, with a line naming it added to `warnings` when given.
[[nodiscard]] std::expected<core::WorldMap, core::Error> ParseWorldMap(
    std::string_view yaml,
    const core::Strings& strings,
    std::vector<std::string>* warnings = nullptr
);

/// The world map in `folder` (`assets/data/`), or why it couldn't be read; an unknown key (a typo) is logged as a warning.
[[nodiscard]] std::expected<std::shared_ptr<const core::WorldMap>, core::Error> LoadWorldMap(
    const std::filesystem::path& folder,
    const core::Strings& strings
);

} // namespace data
