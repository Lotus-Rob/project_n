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

/// How many terrain kinds the ground's table holds: the most a look may describe, and so the most a map may have.
inline constexpr u32 MAX_GROUND_KINDS = 16;

/// The painted stamps scattered over one terrain kind. Lengths in tiles.
struct StampLook {
    /// The stamp sprites of the kind's family; a stamp picks one by its place.
    std::vector<atlas::SpriteId> sprites;
    /// How many stamps a tile gets on average.
    f32 density = 0.0F;
    /// The least and greatest width of a stamp, in tiles.
    std::array<f32, 2> size{ 1.0F, 1.0F };
    /// How far a stamp strays from its cell's centre, as a share of the cell, from 0 to 1.
    f32 jitter = 0.0F;
    /// The zoom below which stamps thin out, from 0 (never) to 2.
    f32 thinBelow = 0.0F;
    /// Whether mist gathers among the kind's stamps.
    bool mist = false;
};

/// How one terrain kind looks: its wash and its stamps, and the tiles it draws until the stamps replace them.
struct KindLook {
    /// The colour the kind's ground is washed with (sRGB).
    std::array<u8, 3> wash{};
    /// How strongly the wash shows, from 0 to 1.
    f32 strength = 0.0F;
    /// The stamps over the kind.
    StampLook stamps{};
    /// The tile sprites that draw the kind for now, at least one; a tile picks by its place.
    std::vector<atlas::SpriteId> variants;
};

/// The paper under the map and how washes meet. Colours are sRGB, lengths in tiles.
struct GroundLook {
    /// The paper's colour.
    std::array<u8, 3> paper{};
    /// The paper fibre's strength, from 0 to 1.
    f32 grain = 0.0F;
    /// How far noise moves the edge between two kinds.
    f32 edgeWarp = 0.0F;
    /// How wide that edge blends.
    f32 edgeSoft = 0.0F;
    /// How unevenly a wash pools, from 0 to 1.
    f32 blotch = 0.0F;
    /// The map's kind (by index) that is shallow water.
    u8 shallowKind = 0;
    /// The map's kind (by index) that is deep water.
    u8 deepKind = 0;
    /// How many tiles from the shore the water takes to turn from shallow to deep.
    f32 depthTiles = 0.0F;
    /// The shore line's colour.
    std::array<u8, 3> shore{};
    /// How strongly the shore line shows, from 0 to 1.
    f32 shoreStrength = 0.0F;
    /// The shore line's width.
    f32 shoreWidth = 0.0F;
    /// The tile grid's colour.
    std::array<u8, 3> grid{};
    /// How strongly the grid shows, from 0 to 1.
    f32 gridStrength = 0.0F;
    /// The zoom where the grid is whole, then the zoom where it is gone.
    std::array<f32, 2> gridFade{};
};

/// The haze that softens the ground between its stamps.
struct VeilLook {
    /// The haze's colour (sRGB).
    std::array<u8, 3> colour{};
    /// The size of the haze's patches, in tiles.
    f32 scale = 0.0F;
    /// How strongly it shows, from 0 to 1.
    f32 strength = 0.0F;
};

/// The mist puffs that gather among the stamps of the kinds that ask for them.
struct MistLook {
    /// The puff sprites; a puff picks one by its place.
    std::vector<atlas::SpriteId> sprites;
    /// The puffs' colour (sRGB).
    std::array<u8, 3> colour{};
    /// How strongly they show, from 0 to 1.
    f32 strength = 0.0F;
    /// The chance of a puff beside a stamp, from 0 to 1.
    f32 chance = 0.0F;
    /// The least and greatest width of a puff, as a share of its stamp's width.
    std::array<f32, 2> width{};
};

/// The painted river ribbon. Lengths in tiles, colours sRGB.
struct RiverLook {
    /// The ribbon's width.
    f32 width = 0.0F;
    /// The width of its bank.
    f32 edge = 0.0F;
    /// The water's colour.
    std::array<u8, 3> colour{};
    /// The bank's colour.
    std::array<u8, 3> edgeColour{};
    /// How strongly the bank shows, from 0 to 1.
    f32 edgeStrength = 0.0F;
};

/// The paintings that stand for places.
struct PlaceLook {
    /// A city's painting.
    atlas::SpriteId city{};
    /// The painting of each danger sort, in the file's order.
    std::vector<std::pair<std::string, atlas::SpriteId>> dangers;
    /// The painting of the place with a given key, which stands in for its sort's or the city's.
    std::vector<std::pair<std::string, atlas::SpriteId>> byKey;
};

/// The sprites that draw the world map.
struct WorldLook {
    /// The look of each terrain kind, by the map's kind index.
    std::vector<KindLook> kinds;
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
    /// The paper under the map and how washes meet.
    GroundLook ground{};
    /// The haze over the ground.
    VeilLook veil{};
    /// The mist puffs among the stamps.
    MistLook mist{};
    /// The river ribbon.
    RiverLook river{};
    /// The paintings of the places.
    PlaceLook places{};

    /// A hash of every value, so a reload can tell an edit from none. Not part of the game data's hash.
    [[nodiscard]] u64 Hash() const {
        Hasher hasher;
        const auto sprites = [&](const std::vector<atlas::SpriteId>& list) {
            hasher(static_cast<u64>(list.size()));
            for (const atlas::SpriteId sprite : list) {
                hasher(sprite);
            }
        };
        const auto colour = [&](const std::array<u8, 3>& channels) {
            for (const u8 channel : channels) {
                hasher(channel);
            }
        };
        const auto named = [&](const std::vector<std::pair<std::string, atlas::SpriteId>>& list) {
            hasher(static_cast<u64>(list.size()));
            for (const auto& [name, sprite] : list) {
                hasher(static_cast<u64>(name.size()));
                for (const char character : name) {
                    hasher(character);
                }
                hasher(sprite);
            }
        };
        hasher(static_cast<u64>(kinds.size()));
        for (const KindLook& kind : kinds) {
            colour(kind.wash);
            hasher(kind.strength);
            sprites(kind.stamps.sprites);
            hasher(
                kind.stamps.density,
                kind.stamps.size[0],
                kind.stamps.size[1],
                kind.stamps.jitter,
                kind.stamps.thinBelow,
                kind.stamps.mist
            );
            sprites(kind.variants);
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
        colour(fog.sepia);
        colour(fog.mist);
        colour(fog.shade);
        colour(ground.paper);
        hasher(ground.grain, ground.edgeWarp, ground.edgeSoft, ground.blotch, ground.shallowKind, ground.deepKind);
        hasher(ground.depthTiles, ground.shoreStrength, ground.shoreWidth, ground.gridStrength);
        hasher(ground.gridFade[0], ground.gridFade[1]);
        colour(ground.shore);
        colour(ground.grid);
        colour(veil.colour);
        hasher(veil.scale, veil.strength);
        sprites(mist.sprites);
        colour(mist.colour);
        hasher(mist.strength, mist.chance, mist.width[0], mist.width[1]);
        hasher(river.width, river.edge, river.edgeStrength);
        colour(river.colour);
        colour(river.edgeColour);
        hasher(places.city);
        named(places.dangers);
        named(places.byKey);
        return hasher.Value();
    }
};

} // namespace core
