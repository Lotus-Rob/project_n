#pragma once

/// @file
/// @ingroup data
/// The atlas manifest's reader (design doc 0002.3.1 §2): `assets/manifest.yaml` into core::Manifest.

#include "core/error.hpp"
#include "core/manifest.hpp"

#include <expected>
#include <filesystem>
#include <string_view>

namespace data {

/// The manifest `text` (manifest.yaml's) holds. Fails, naming the line where it can, on YAML that
/// doesn't parse, a field missing or of the wrong type, fewer than 2 or more than 15 mip levels, a sprite
/// past the last page or, with its mip gutter, outside its page, a foot outside its image, a scene naming a sprite the
/// manifest doesn't have or listing one twice, and a sprite list that differs from atlas::SPRITE_NAMES (the atlas is stale).
[[nodiscard]] std::expected<core::Manifest, core::Error> ParseManifest(std::string_view text);

/// The manifest in `path`, or why it couldn't be read.
[[nodiscard]] std::expected<core::Manifest, core::Error> LoadManifest(const std::filesystem::path& path);

} // namespace data
