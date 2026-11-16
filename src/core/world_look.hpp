#pragma once

/// @file
/// @ingroup core
/// The world map's look: which sprites draw each terrain kind, the road and river overlays, the
/// crossings and the markers, read from `assets/data/world_look.yaml`. Presentation, not game data: it is never hashed
/// into the game data or saved, `game` never sees it, and the main thread reloads it as the file is edited.

#include "core/archive.hpp"
#include "core/types.hpp"

#include "core/generated/atlas.hpp" // by `project_n atlas`

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace core {

/// How many sprites a road or river list holds: one for each combination of its four neighbours (bits 1, 2, 4, 8 for
/// north, east, south and west).
inline constexpr u32 LINE_MASKS = 16;

/// The sprites that draw the world map.
struct WorldLook {
    /// The variants of each terrain kind, by the map's kind index; each has at least one.
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<std::vector<atlas::SpriteId>> kinds{};
    /// The road overlay by neighbour mask.
    std::array<atlas::SpriteId, LINE_MASKS> roads{};
    /// The river overlay by neighbour mask.
    std::array<atlas::SpriteId, LINE_MASKS> rivers{};
    /// A road across a river, where the road runs east to west.
    atlas::SpriteId bridgeAcross{};
    /// A road across a river, where the road runs north to south.
    atlas::SpriteId bridgeDown{};
    /// A ford over a river.
    atlas::SpriteId ford{};
    /// A city's marker.
    atlas::SpriteId cityMarker{};
    /// The marker of each danger sort, in the file's order (a sort is a key the data names, not an enum).
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<std::pair<std::string, atlas::SpriteId>> dangerMarkers{};

    /// A hash of every value, so a reload can tell an edit from none. Not part of the game data's hash.
    [[nodiscard]] u64 Hash() const {
        Hasher hasher;
        hasher(static_cast<u64>(kinds.size()));
        for (const std::vector<atlas::SpriteId>& variants : kinds) {
            hasher(static_cast<u64>(variants.size()));
            for (const atlas::SpriteId sprite : variants) {
                hasher(sprite);
            }
        }
        for (const atlas::SpriteId sprite : roads) {
            hasher(sprite);
        }
        for (const atlas::SpriteId sprite : rivers) {
            hasher(sprite);
        }
        hasher(bridgeAcross, bridgeDown, ford, cityMarker);
        hasher(static_cast<u64>(dangerMarkers.size()));
        for (const auto& [sort, sprite] : dangerMarkers) {
            hasher(static_cast<u64>(sort.size()));
            for (const char character : sort) {
                hasher(character);
            }
            hasher(sprite);
        }
        return hasher.Value();
    }
};

} // namespace core
