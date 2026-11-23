#pragma once

/// @file
/// @ingroup data
/// The figures' animations: `assets/data/animations.yaml` into core::Animations.

#include "core/animations.hpp"
#include "core/error.hpp"
#include "core/manifest.hpp"

#include <expected>
#include <filesystem>
#include <string_view>

namespace data {

/// The animations `yaml` (animations.yaml's) holds: a map from a standing sprite to its entry, whose `walk:` is `frames:`
/// (1 to core::MAX_WALK_FRAMES sprites, in order) and `stride:` (the world units walked while one frame shows, above 0 and at
/// most 1,000,000). An empty table is no walks. Every sprite must be in `manifest`, and a scene holding a standing sprite must
/// hold each of its frames, or the frame would be missing from the atlas where the figure is drawn. Fails, naming the line, on
/// YAML that doesn't parse, a standing sprite given twice, a key it doesn't know (an entry's or a walk's), a missing `walk`,
/// `frames` or `stride`, a sprite the manifest lacks, a frame a scene lacks, no frames or too many, and a stride that isn't a
/// number above 0 and at most 1,000,000, or is too small for an f32.
[[nodiscard]] std::expected<core::Animations, core::Error> ParseAnimations(
    std::string_view yaml,
    const core::Manifest& manifest
);

/// The animations in `folder` (`assets/data/`), or why they couldn't be read.
[[nodiscard]] std::expected<core::Animations, core::Error> LoadAnimations(
    const std::filesystem::path& folder,
    const core::Manifest& manifest
);

} // namespace data
