#pragma once

/// @file
/// @ingroup data
/// The seals' looks: `assets/data/seals.yaml` into core::Seals.

#include "core/error.hpp"
#include "core/manifest.hpp"
#include "core/seals.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace data {

/// The seals `text` (seals.yaml's) holds: view_ratio, above_opacity, spin_scale and brightness (each left out keeps
/// core::Seals' default), `glyph_sets:` (named lists of sprites) and `seals:` (named looks: plane, lift, wash, colours,
/// life, layers and their parts, emblem, satellites and light; the file's comments name every key). A sprite must be in
/// `manifest`. Fails, naming the line, on YAML that doesn't parse, a value that isn't a number or is out of range (a size of
/// 0 or less, an inner radius not below its outer one, a negative time), 0 or more than core::MAX_SEAL_LAYERS layers, more
/// than core::MAX_SEAL_PARTS parts in a layer, more than core::MAX_SEAL_PARTICLES particles, an unknown palette colour,
/// curve, sprite, glyph set or seal, a colour slot the seal lacks, and a satellite naming its own seal, forming a loop or
/// nesting deeper than core::MAX_SATELLITE_DEPTH. A key it doesn't know is ignored, with a line naming it added to
/// `warnings` when given.
[[nodiscard]] std::expected<core::Seals, core::Error> ParseSeals(
    std::string_view text,
    const core::Manifest& manifest,
    std::vector<std::string>* warnings = nullptr
);

/// The seals in `folder` (`assets/data/`), or why they couldn't be read; an unknown key (a typo) is logged as a warning.
[[nodiscard]] std::expected<core::Seals, core::Error> LoadSeals(
    const std::filesystem::path& folder,
    const core::Manifest& manifest
);

} // namespace data
