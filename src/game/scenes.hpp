#pragma once

/// @file
/// @ingroup game
/// The game's scenes, in F2's order: a scene's key (core::SceneKey) is its place in the list (game/scenes.cpp), which is
/// also what a save stores of it. Adding a scene is its class in game/scenes/, an instance and its place in the list in
/// game/scenes.cpp, and its key constant here, which game/scenes.cpp checks against its place.

#include "core/error.hpp"
#include "core/frame.hpp"
#include "core/game_data.hpp"
#include "core/manifest.hpp"
#include "game/scene.hpp"

#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace game {

/// The demonstration arena (game/scenes/arena.hpp).
inline constexpr core::SceneKey ARENA_SCENE{ 0 };
/// The arena with a crowd, a bullet spiral and a large world, for profiling (game/scenes/stress.hpp).
inline constexpr core::SceneKey STRESS_SCENE{ 1 };
/// A still scene of painted sprites and overlapping glows (game/scenes/painted.hpp).
inline constexpr core::SceneKey PAINTED_SCENE{ 2 };
/// A still grid of map cells (game/scenes/cells.hpp).
inline constexpr core::SceneKey CELLS_SCENE{ 3 };

/// Every scene, in F2's order: a scene's index is its key.
[[nodiscard]] std::span<const SceneClass* const> Scenes();

/// The scene of `key`, which must be listed.
[[nodiscard]] const SceneClass& SceneOf(core::SceneKey key);

/// The scene called `name` (SceneTraits::name), if one is listed.
[[nodiscard]] std::optional<core::SceneKey> SceneNamed(std::string_view name);

/// The name of the scene of `key` (SceneTraits::name), also its manifest scene's: `arena`, `stress`, `painted`, `cells`.
[[nodiscard]] std::string_view SceneName(core::SceneKey key);

/// The scene after `key` in F2's order, the first again after the last.
[[nodiscard]] core::SceneKey NextScene(core::SceneKey key);

/// Whether every scene can run from `manifest` with `data` (design doc 0002.3.1 §3): the manifest names each,
/// and the scenes with creatures load the sprites `data` gives them, without which they would be invisible.
/// Otherwise, which scene is short of what.
[[nodiscard]] std::expected<void, core::Error> CheckScenes(const core::Manifest& manifest, const core::GameData& data);

} // namespace game
