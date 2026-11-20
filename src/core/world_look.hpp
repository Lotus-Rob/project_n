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

/// The distance between a route's dots in a look nothing has read yet, in world units.
inline constexpr f32 DEFAULT_ROUTE_SPACING = 48.0F;

/// A cloud's width in cell spacings in a look nothing has read yet.
inline constexpr f32 DEFAULT_CLOUD_WIDTH = 1.15F;
/// A cloud's bob, in cell spacings, in a look nothing has read yet.
inline constexpr f32 DEFAULT_BOB_AMPLITUDE = 0.03F;
/// The seconds a cloud's bob takes in a look nothing has read yet.
inline constexpr f32 DEFAULT_BOB_PERIOD = 4.0F;
/// How far an uncovered cloud drifts, in tiles, in a look nothing has read yet.
inline constexpr f32 DEFAULT_CLOUD_DRIFT = 0.3F;
/// The seconds a cloud takes to fade in a look nothing has read yet.
inline constexpr f32 DEFAULT_FADE_SECONDS = 0.5F;
/// How far land seen but not near drains of colour in a look nothing has read yet.
inline constexpr f32 DEFAULT_DRAIN = 0.85F;
/// How much of the mist shows in a look nothing has read yet.
inline constexpr f32 DEFAULT_MIST_STRENGTH = 0.25F;

/// The sprites that draw the player's route and the target cursor's tile.
struct RouteLook {
    /// One dot of the route's line.
    atlas::SpriteId dot{};
    /// The world units between one dot and the next along the line.
    f32 spacing = DEFAULT_ROUTE_SPACING;
    /// The mark on the route's end.
    atlas::SpriteId marker{};
    /// The outline of the cursor's tile.
    atlas::SpriteId cursor{};
};

/// The clouds over the unexplored map and the colours that shade the explored edge. Lengths in cell spacings or tiles as
/// each field says; colours are sRGB.
struct FogLook {
    /// The cloud sprites, at least one; a cloud picks one by its place.
    std::vector<atlas::SpriteId> clouds;
    /// A cloud's width in cell spacings, before its own draw of 0.85 to 1.45.
    f32 cloudWidth = DEFAULT_CLOUD_WIDTH;
    /// How far a cloud bobs up and down, in cell spacings.
    f32 bobAmplitude = DEFAULT_BOB_AMPLITUDE;
    /// The seconds one bob takes.
    f32 bobPeriod = DEFAULT_BOB_PERIOD;
    /// How far a cloud with its cell uncovered drifts away while it fades, in tiles.
    f32 drift = DEFAULT_CLOUD_DRIFT;
    /// The seconds a cloud takes to fade once its cell is uncovered.
    f32 fadeSeconds = DEFAULT_FADE_SECONDS;
    /// How far the colour drains from land the player has seen but isn't near, from 0 (none) to 1 (all).
    f32 drain = DEFAULT_DRAIN;
    /// The colour drained land tends toward.
    std::array<u8, 3> sepia{};
    /// The colour of the mist laid over drained land.
    std::array<u8, 3> mist{};
    /// How much of the mist shows, from 0 to 1.
    f32 mistStrength = DEFAULT_MIST_STRENGTH;
    /// The colour under the clouds, so no land shows between them.
    std::array<u8, 3> shade{};
};

/// The sprites that draw the world map.
struct WorldLook {
    /// The variants of each terrain kind, by the map's kind index; each has at least one.
    std::vector<std::vector<atlas::SpriteId>> kinds;
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
    std::vector<std::pair<std::string, atlas::SpriteId>> dangerMarkers;
    /// The route line and the cursor's tile.
    RouteLook route{};
    /// The fog's clouds and colours.
    FogLook fog{};

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
        hasher(route.dot, route.spacing, route.marker, route.cursor);
        hasher(static_cast<u64>(fog.clouds.size()));
        for (const atlas::SpriteId sprite : fog.clouds) {
            hasher(sprite);
        }
        hasher(fog.cloudWidth, fog.bobAmplitude, fog.bobPeriod, fog.drift, fog.fadeSeconds, fog.drain, fog.mistStrength);
        for (const std::array<u8, 3>& colour : { fog.sepia, fog.mist, fog.shade }) {
            for (const u8 channel : colour) {
                hasher(channel);
            }
        }
        return hasher.Value();
    }
};

} // namespace core
