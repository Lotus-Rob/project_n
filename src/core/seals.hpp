#pragma once

/// @file
/// @ingroup core
/// The seals' looks (`assets/data/seals.yaml`; the phase 6 chunk 4b spec, "In the data"): how each named seal is drawn,
/// its plane, ink wash, colours, life, rings and their parts, emblem, satellites and light. Presentation, not game data: it
/// is not hashed or saved, and the main thread reloads it as the file is edited. A seal in play (core::Seal) finds its look
/// here by its key. Angles are in degrees, times in seconds; radii, widths and sizes are fractions of the seal's size (its
/// outer radius) except where a field says world units.

#include "core/glow.hpp"
#include "core/seal.hpp"
#include "core/types.hpp"

#include "core/generated/atlas.hpp" // by `project_n atlas`

#include <algorithm>
#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace core {

/// The most layers (rings and frames) a seal has; it has at least one.
inline constexpr u32 MAX_SEAL_LAYERS = 9;
/// The most parts a layer has.
inline constexpr u32 MAX_SEAL_PARTS = 16;
/// The most sides a layer's frame has.
inline constexpr u32 MAX_SEAL_FRAME_SIDES = 64;
/// How deep satellites nest at most: a satellite's satellite is 2 deep.
inline constexpr u32 MAX_SATELLITE_DEPTH = 2;
/// The most looks a seals table has.
inline constexpr u32 MAX_SEAL_LOOKS = 1'024;
/// The most satellites a look has: with its seal, one frame's most drawn seals (gfx::MAX_SEALS_DRAWN).
inline constexpr u32 MAX_SEAL_SATELLITES = 63;
/// The most glyph sets a seals table has.
inline constexpr u32 MAX_SEAL_GLYPH_SETS = 64;
/// The most sprites a glyph set has.
inline constexpr u32 MAX_GLYPH_SET_SPRITES = 256;
/// The most particles (pillars, sparks and shards together) a seal's light has.
inline constexpr u32 MAX_SEAL_PARTICLES = 256;

/// What a seal's look and the table take when the data leaves a key out.
namespace shipped {

/// The default `washDarkness` of SealLook.
inline constexpr f32 SEAL_WASH_DARKNESS = 0.5F;
/// The default `washReach` of SealLook: a little past the outer ring.
inline constexpr f32 SEAL_WASH_REACH = 1.15F;
/// The default `washSoftness` of SealLook.
inline constexpr f32 SEAL_WASH_SOFTNESS = 0.25F;
/// The default `appear` of SealLook, in seconds.
inline constexpr f64 SEAL_APPEAR_SECONDS = 0.4;
/// The default `fade` of SealLook, in seconds.
inline constexpr f64 SEAL_FADE_SECONDS = 0.3;
/// The default `viewRatio` of Seals: the blob shadow's 24 / 64.
inline constexpr f32 SEAL_VIEW_RATIO = 24.0F / 64.0F;
/// The default `aboveOpacity` of Seals.
inline constexpr f32 SEAL_ABOVE_OPACITY = 0.7F;
/// The default `haloReach` of Seals: a little past a fine line.
inline constexpr f32 SEAL_HALO_REACH = 0.012F;
/// The default `haloStrength` of Seals.
inline constexpr f32 SEAL_HALO_STRENGTH = 0.3F;

} // namespace shipped

/// Which plane a seal lies in, seen at the art's angle (Seals::viewRatio).
enum class SealPlane : u8 {
    Ground,   ///< Flat on the ground: an ellipse squashed by the view ratio.
    Upright,  ///< Standing, facing the viewer: a circle.
    Overhead, ///< Flat like a ground seal, lifted high.
};

/// What a part of a layer draws.
enum class SealPartKind : u8 {
    Circle,   ///< A ring at `radius`, `width` wide, solid or dashed (`dash` and `gap`).
    Ticks,    ///< `count` strokes across the ring at `radius`, `size` long and `width` wide.
    Dots,     ///< `count` dots on the ring at `radius`, `size` across.
    Script,   ///< `count` glyphs of glyph set `glyphSet` round the ring at `radius`, `size` tall, facing out or in (`inward`).
    Star,     ///< A star polygon of `count` points joining every `step`th, its points at `radius`, its lines `width` wide.
    Polygon,  ///< A regular polygon of `count` sides, its corners at `radius`, its lines `width` wide.
    Flourish, ///< `count` sprites of `sprites` round the ring at `radius`, `size` tall, reaching `spill` past the layer.
};

/// One part of a layer. Fields its kind doesn't use stay zero.
struct SealPart {
    /// What it draws.
    SealPartKind kind = SealPartKind::Circle;
    /// Which of its seal's colours it takes: 0 or 1, below SealLook::colourCount.
    u8 slot = 0;
    /// Where it sits: a fraction of the seal's size, above 0, at most 1.
    f32 radius = 0.0F;
    /// Its line's width: a fraction of the seal's size (circle, ticks, star, polygon).
    f32 width = 0.0F;
    /// Its items' size: a tick's length, a dot's diameter, a glyph's or flourish's height, as a fraction of the seal's size.
    f32 size = 0.0F;
    /// How many ticks, dots, glyphs, flourishes, star points or polygon sides: at least 1 (a star or polygon at least 3).
    u32 count = 0;
    /// A star's step: it joins every `step`th point, at least 1 and below half its points (a hexagram is 6 and 2).
    u32 step = 0;
    /// A dashed circle's dash, in degrees of its ring; 0 is solid.
    f32 dash = 0.0F;
    /// A dashed circle's gap between dashes, in degrees of its ring.
    f32 gap = 0.0F;
    /// Where its first item (or corner) is from the layer's zero, in degrees.
    f32 rotation = 0.0F;
    /// How far a flourish may reach past the layer's outer edge, as a fraction of the seal's size.
    f32 spill = 0.0F;
    /// Whether a script's glyphs face the centre rather than out.
    bool inward = false;
    /// A script's glyph set: an index into Seals::glyphSets.
    u32 glyphSet = 0;
    /// A flourish's sprites, drawn in turn round the ring (the data's one to three, repeated to fill all three).
    std::array<atlas::SpriteId, 3> sprites{};
};

/// One layer: a ring (or a non-round frame) of a seal, with its own height, spin and parts.
struct SealLayer {
    /// Its inner radius: a fraction of the seal's size, from 0, below `outer`.
    f32 inner = 0.0F;
    /// Its outer radius: a fraction of the seal's size, above `inner`, at most 1.
    f32 outer = 1.0F;
    /// How far it rises above its seal's plane, in world units, 0 or more.
    f32 height = 0.0F;
    /// How fast it turns, in degrees a second; negative turns the other way.
    f32 spin = 0.0F;
    /// Its opacity, 0 to 1.
    f32 opacity = 1.0F;
    /// Its frame's sides: 0 is round, else 3 to MAX_SEAL_FRAME_SIDES (a square, a diamond).
    u32 frameSides = 0;
    /// Its frame's turn from the layer's zero, in degrees.
    f32 frameRotation = 0.0F;
    /// Its parts, at most MAX_SEAL_PARTS, in the data's order.
    std::vector<SealPart> parts{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
};

/// A sprite at a seal's centre.
struct SealEmblem {
    /// Its sprite.
    atlas::SpriteId sprite = atlas::SpriteId{};
    /// Its height: a fraction of the seal's size, above 0, at most 1.
    f32 size = 0.0F;
    /// How fast it turns, in degrees a second; negative turns the other way.
    f32 spin = 0.0F;
    /// Which of its seal's colours it takes.
    u8 slot = 0;
};

/// How a satellite keeps its place on its layer's ring.
enum class SealMount : u8 {
    Fixed,     ///< At its angle, however the layer turns.
    WithLayer, ///< At its angle on the layer, turning with it.
    Orbit,     ///< From its angle, going round at its own speed (SealSatellite::orbit).
};

/// Another seal drawn on a layer's ring of this one.
struct SealSatellite {
    /// Which seal (its key): another seal of the table, never its own.
    SealKey seal = 0;
    /// The layer whose ring it sits on: below its seal's layer count.
    u32 layer = 0;
    /// Where on the ring, in degrees.
    f32 angle = 0.0F;
    /// Its size: a fraction of this seal's size, above 0, at most 1.
    f32 size = 0.0F;
    /// How it keeps its place.
    SealMount mount = SealMount::WithLayer;
    /// An orbiting satellite's speed round the ring, in degrees a second; negative goes the other way.
    f32 orbit = 0.0F;
};

/// A seal's light: pillars falling from it, sparks rising and shards drifting, together at most MAX_SEAL_PARTICLES.
struct SealLight {
    /// How many pillars.
    u32 pillars = 0;
    /// A pillar's height, in world units.
    f32 pillarHeight = 0.0F;
    /// A pillar's width: a fraction of the seal's size.
    f32 pillarWidth = 0.0F;
    /// How much of a pillar's length it fades out over, towards its far end, 0 (a hard end) to 1 (fading all the way).
    f32 pillarFade = 0.0F;
    /// How long a pillar's light takes to fall its length once, in seconds: above 0 when there are pillars.
    f64 pillarCycle = 0.0;
    /// How many sparks.
    u32 sparks = 0;
    /// How fast a spark rises, in world units a second.
    f32 sparkRise = 0.0F;
    /// How long a spark lives, in seconds: above 0 when there are sparks.
    f64 sparkLife = 0.0;
    /// How many shards.
    u32 shards = 0;
    /// How fast a shard drifts, in world units a second.
    f32 shardDrift = 0.0F;
    /// How fast a shard turns, in degrees a second.
    f32 shardSpin = 0.0F;
    /// Which of its seal's colours it takes.
    u8 slot = 0;
};

/// How one seal looks.
struct SealLook {
    /// Its plane.
    SealPlane plane = SealPlane::Ground;
    /// How high above the ground its plane sits, in world units, 0 or more.
    f32 lift = 0.0F;
    /// How dark its ink wash is, 0 (none) to 1.
    f32 washDarkness = shipped::SEAL_WASH_DARKNESS;
    /// How far its ink wash reaches: a fraction of the seal's size, above 0.
    f32 washReach = shipped::SEAL_WASH_REACH;
    /// How soft its ink wash's edge is, 0 (hard) to 1.
    f32 washSoftness = shipped::SEAL_WASH_SOFTNESS;
    /// Its colours: `element` (the record's element) or a palette colour; `colourCount` of them count.
    std::array<GlowColour, 2> colours{};
    /// How many of `colours` it has: 1 or 2.
    u8 colourCount = 1;
    /// How long it takes to appear, in seconds, 0 or more.
    f64 appear = shipped::SEAL_APPEAR_SECONDS;
    /// The shape of its appearing.
    GlowCurve appearCurve = GlowCurve::EaseOut;
    /// How long it takes to fade before its end, in seconds, 0 or more.
    f64 fade = shipped::SEAL_FADE_SECONDS;
    /// The shape of its fading.
    GlowCurve fadeCurve = GlowCurve::Smooth;
    /// Whether its layers appear from the innermost out, rather than the outermost in.
    bool innerFirst = false;
    /// Its layers, 1 to MAX_SEAL_LAYERS, in the data's order.
    std::vector<SealLayer> layers{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// The sprite at its centre, if it has one.
    std::optional<SealEmblem> emblem = std::nullopt;
    /// The seals drawn on its rings.
    std::vector<SealSatellite>
        satellites{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// Its light.
    SealLight light{};
};

/// Every seal's look, and what all seals share.
struct Seals {
    /// The art's angle: how far depth on a flat plane is squashed on screen, above 0, at most 1 (the blob shadow's 24 / 64).
    f32 viewRatio = shipped::SEAL_VIEW_RATIO;
    /// The most opacity an upright or overhead seal reaches, over the creatures, 0 to 1.
    f32 aboveOpacity = shipped::SEAL_ABOVE_OPACITY;
    /// Scales the seals' time: spins, particle motion and life, and the appear and fade (reduce motion lowers it), 0 or more.
    f32 spinScale = 1.0F;
    /// Multiplies every line and light (reduce flashing lowers it), 0 or more.
    f32 brightness = 1.0F;
    /// How far the halo round every part of every seal reaches past the part's edge: a fraction of the seal's size, 0 (none)
    /// to 0.1. Each layer's band reaches past its radii by this and an allowance for anti-aliasing (gfx::SealBandMargin()).
    f32 haloReach = shipped::SEAL_HALO_REACH;
    /// How strong that halo is at the part's edge, 0 to 1, fading to nothing at its reach.
    f32 haloStrength = shipped::SEAL_HALO_STRENGTH;
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// The glyph sets scripts draw from, each one or more sprites, in the data's order.
    std::vector<std::vector<atlas::SpriteId>> glyphSets{};
    /// Every seal's look by key, sorted by key.
    std::vector<std::pair<SealKey, SealLook>> looks{};
    // NOLINTEND(readability-redundant-member-init)

    /// The look of the seal `key` names, or null when the table has none.
    [[nodiscard]] const SealLook* Find(SealKey key) const {
        const auto found = std::ranges::lower_bound(looks, key, {}, &std::pair<SealKey, SealLook>::first);
        return found != looks.end() && found->first == key ? &found->second : nullptr;
    }
};

} // namespace core
