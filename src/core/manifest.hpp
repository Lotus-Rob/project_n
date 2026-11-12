#pragma once

/// @file
/// @ingroup core
/// The atlas manifest (design doc 0002.3.1 §2): what `project_n atlas` wrote to assets/manifest.yaml,
/// the texture array's shape, where each sprite is on it, and the sprites each scene draws. data reads it.

#include "core/types.hpp"

#include "core/generated/atlas.hpp" // by `project_n atlas`

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace core {

/// How far a sprite's distance field reaches past its edge, in texels: the most a glow can reach, and half the atlas
/// gutter's least, so a field never meets its neighbour's.
inline constexpr u32 FIELD_REACH_TEXELS = 16;

/// The sprite called `name` in art/sprites.toml, if there is one.
[[nodiscard]] inline std::optional<atlas::SpriteId> SpriteNamed(std::string_view name) {
    const std::span<const std::string_view> names{ atlas::SPRITE_NAMES };
    const auto found = std::ranges::find(names, name);
    if (found == names.end()) {
        return std::nullopt;
    }
    return static_cast<atlas::SpriteId>(found - names.begin());
}

/// A scene's index in the manifest, in art/scenes.toml's order.
enum class SceneId : u16 {
}; // NOLINT(performance-enum-size): saved and sent with actions as two bytes

/// Where the atlas step put a sprite in the texture array, all in pixels.
struct SpriteInfo {
    /// The image, relative to assets/.
    std::string file;
    /// The array layer (page) it is on.
    u16 page = 0;
    /// The region's left edge on the page.
    u16 x = 0;
    /// The region's top edge on the page.
    u16 y = 0;
    /// The image's width: its size in reference pixels (1920x1080) too.
    u16 width = 0;
    /// The image's height.
    u16 height = 0;
    /// The foot point from the image's left edge: where an instance's position puts it.
    u16 footX = 0;
    /// The foot point from the image's top edge; also the y-sort point.
    u16 footY = 0;
};

/// A scene: its name and every sprite it draws.
struct SceneInfo {
    /// Its name in art/scenes.toml.
    std::string name;
    /// The sprites it draws, each once.
    std::vector<atlas::SpriteId> sprites;
};

/// The atlas manifest.
struct Manifest {
    /// The width and height of every page.
    u32 pageSize = 0;
    /// The number of pages (array layers).
    u32 pages = 0;
    /// The mip levels to generate; the gutter between sprites keeps them apart down to the last.
    u32 mipLevels = 0;
    /// The texels between sprites, and from a sprite to its page's edge at least: each owns half (Edge()).
    u32 gutter = 0;
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// Every sprite, indexed by atlas::SpriteId: as many as atlas::SPRITE_NAMES.
    std::vector<SpriteInfo> sprites{};
    /// Every scene, indexed by SceneId.
    std::vector<SceneInfo> scenes{};
    // NOLINTEND(readability-redundant-member-init)

    /// Each sprite's share of the gutter: the texels past its region on every side that it writes and its field fills.
    [[nodiscard]] u32 Edge() const {
        return gutter / 2;
    }
    /// The sprite `sprite`, which must be in the manifest.
    [[nodiscard]] const SpriteInfo& Sprite(atlas::SpriteId sprite) const {
        return sprites.at(std::to_underlying(sprite));
    }
    /// The scene `scene`, which must be in the manifest.
    [[nodiscard]] const SceneInfo& Scene(SceneId scene) const {
        return scenes.at(std::to_underlying(scene));
    }
    /// The scene called `name`, if there is one.
    [[nodiscard]] std::optional<SceneId> SceneNamed(std::string_view name) const {
        const auto found = std::ranges::find(scenes, name, &SceneInfo::name);
        if (found == scenes.end()) {
            return std::nullopt;
        }
        return static_cast<SceneId>(found - scenes.begin());
    }
};

} // namespace core
