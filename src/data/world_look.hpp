#pragma once

/// @file
/// @ingroup data
/// The world map's look (phase 6 chunk 5b1): `assets/data/world_look.yaml` into core::WorldLook.

#include "core/error.hpp"
#include "core/manifest.hpp"
#include "core/world_look.hpp"
#include "core/world_map.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace data {

/// The manifest scene (art/scenes.toml) whose sprites a look may name: the scene that shows the map, `game`'s world scene,
/// whose name it must match (app_tests checks that, as data sees no scene of game's).
inline constexpr std::string_view WORLD_LOOK_SCENE = "world";

/// The look `yaml` (world_look.yaml's text) holds for `map`: `kinds:` (each kind's name and a list of variant sprites),
/// `roads:` and `rivers:` (16 sprites each, by neighbour mask), `bridge:` (`across`, `down`), `ford:` and `markers:` (`city`
/// and a sprite for each danger sort). A sprite must be in `manifest` and among the sprites of its WORLD_LOOK_SCENE, which
/// the renderer has resident when the map draws. Fails, naming the line, on YAML that doesn't parse, a field missing or of
/// the wrong type, a sprite the manifest lacks or the world scene doesn't load (a manifest without that scene fails too), a
/// mask list that isn't 16 long, a kind of `map` without a look or with no variant, and a danger sort the map uses without a
/// marker. A kind or marker the map doesn't use is accepted; a key it doesn't know at the top or in `bridge` is ignored, with a
/// line naming it added to `warnings` when given.
[[nodiscard]] std::expected<core::WorldLook, core::Error> ParseWorldLook(
    std::string_view yaml,
    const core::Manifest& manifest,
    const core::WorldMap& map,
    std::vector<std::string>* warnings = nullptr
);

/// The look in `folder` (`assets/data/`) for `map`, or why it couldn't be read; an unknown key (a typo) is logged as a
/// warning.
[[nodiscard]] std::expected<core::WorldLook, core::Error> LoadWorldLook(
    const std::filesystem::path& folder,
    const core::Manifest& manifest,
    const core::WorldMap& map
);

} // namespace data
