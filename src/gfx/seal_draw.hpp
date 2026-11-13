#pragma once

/// @file
/// @ingroup gfx
/// Laying seals out (the phase 6 chunk 4b spec, "The view and the ground" and "Drawing"): each seal in play, from its record,
/// its look (core::Seals) and the render time, becomes the band geometry of its layers and the wash under them at the art's
/// angle, its layers' records pointing at the parts each band's fragment shader draws, and its light's particle records. The
/// parts and their sprites are packed once per table (PackSeals(), when the table loads or reloads), never per frame. Pure,
/// with no state between frames: the renderer uploads what DrawSeals() writes, and tests read it without a device. The GPU
/// sees only small times: each layer's angle already turned and each particle stream's age already wrapped, never a time
/// since a seal's start.

#include "core/manifest.hpp"
#include "core/seal.hpp"
#include "core/seals.hpp"
#include "core/types.hpp"
#include "core/ui_colours.hpp"
#include "core/vec2.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace gfx {

/// The most seals a frame draws, satellites included; DrawSeals() counts the rest in SealDrawing::droppedSeals.
inline constexpr std::size_t MAX_SEALS_DRAWN = 64;
static_assert(core::MAX_SEAL_SATELLITES < MAX_SEALS_DRAWN, "a seal and its satellites fit one frame");
/// The quads a round layer's band takes round its ring.
inline constexpr u32 SEAL_BAND_SEGMENTS = 48;
/// The layer records a drawn seal takes besides its layers': its emblem's and an overhead seal's shadow's.
inline constexpr u32 SEAL_EXTRA_LAYERS = 2;
/// The quads a drawn seal takes besides its layers' bands: its wash's and an overhead seal's shadow's (its emblem's band is
/// SEAL_BAND_SEGMENTS more).
inline constexpr u32 SEAL_EXTRA_QUADS = 2;
/// The most layers a frame's seals have, which the renderer's layer buffer holds: every drawn seal's, with its emblem's and
/// its shadow's.
inline constexpr u32 MAX_SEAL_LAYERS_DRAWN = static_cast<u32>(MAX_SEALS_DRAWN) * (core::MAX_SEAL_LAYERS + SEAL_EXTRA_LAYERS);
/// The most quads a frame's seals take, which the renderer's vertex buffer holds: for every drawn seal, its layers' bands
/// (each round or a frame of at most core::MAX_SEAL_FRAME_SIDES sides), its emblem's, its wash and its shadow.
inline constexpr u32 MAX_SEAL_QUADS =
    static_cast<u32>(MAX_SEALS_DRAWN) * ((core::MAX_SEAL_LAYERS * std::max(SEAL_BAND_SEGMENTS, core::MAX_SEAL_FRAME_SIDES)) +
                                         SEAL_BAND_SEGMENTS + SEAL_EXTRA_QUADS);
/// The room a band leaves past its parts for their anti-aliased edges (half a line and a pixel's filter at the art's sizes),
/// a fraction of the seal's size.
inline constexpr f32 SEAL_ANTIALIAS_ALLOWANCE = 0.018F;

/// How far past a layer's inner and outer radii its band reaches, a fraction of the seal's size, for a table whose halo
/// reaches `haloReach` (core::Seals::haloReach): room for the halo and SEAL_ANTIALIAS_ALLOWANCE, so neither is cut off at
/// the band's edge.
[[nodiscard]] constexpr f32 SealBandMargin(f32 haloReach) {
    return haloReach + SEAL_ANTIALIAS_ALLOWANCE;
}
/// SealGpuPart::kind of a seal's emblem, past core::SealPartKind's values (which the other parts' kinds are).
inline constexpr u32 SEAL_EMBLEM_KIND = 7;
static_assert(std::to_underlying(core::SealPartKind::Flourish) + 1 == SEAL_EMBLEM_KIND, "the emblem's kind follows the parts'");

/// A point of a seal's layer: `x` and `depth` across the seal's plane, in world units from its centre (`x` to the right,
/// `depth` towards the bottom of the screen: nearer the viewer on a flat plane, down an upright one), and `up` off the plane
/// (the layer's height), in world units.
struct SealLocal {
    /// Across the plane, to the right.
    f32 x = 0.0F;
    /// Across the plane, towards the bottom of the screen.
    f32 depth = 0.0F;
    /// Off the plane: the layer's height.
    f32 up = 0.0F;
};

/// Where `local`, a point of a layer of `seal`, lands in the world at the art's angle `viewRatio`, its plane `plane` lifted
/// `lift` above the ground. A flat (ground or overhead) plane lies in the seal's frame, so the point is `(x, depth, up +
/// lift)`; an upright one stands facing the viewer, so it is `(x, up, -depth + lift)` there, its heights coming forward.
/// A frame point `(x, depth, up)` lands at `seal.point + (x, depth * viewRatio - up)`.
[[nodiscard]] core::Vec2 SealToWorld(const core::Seal& seal, core::SealPlane plane, f32 lift, f32 viewRatio, SealLocal local);

/// How much of layer `layer` of `look` shows, from 0 to 1, `secondsSinceStart` after its seal started and `secondsToEnd`
/// before it ends (infinite for one that never ends). The layers appear one after another over `look.appear`, each over its
/// share `appear / layers`, outermost first (by outer radius) or innermost first, along `look.appearCurve`; all fade together
/// over the last `look.fade` seconds, as the glows fade (core::GlowStrength()): `1 - CurveAt(look.fadeCurve, passed)`, `passed`
/// the share of the fade gone by, `1 - secondsToEnd / fade` held to 0..1; 1 while held. 0 for a layer the look hasn't. The
/// caller scales both times by core::Seals::spinScale.
[[nodiscard]] f32 SealLifeOpacity(const core::SealLook& look, std::size_t layer, f64 secondsSinceStart, f64 secondsToEnd);

/// How far `layer` has turned `seconds` after its seal started at its spin times `spinScale`, in radians: worked in `f64` and
/// wrapped to less than a turn either way (the sign kept), so the result is as precise however long the seal has been up.
[[nodiscard]] f32 SealLayerAngle(const core::SealLayer& layer, f64 seconds, f32 spinScale);

/// One part of a layer as the seal fragment shader reads it: the StructuredBuffer element `SealPart` of shaders/seal.hlsli,
/// which must keep this layout (every field 4 bytes, 64 in all). Radii, widths and sizes are fractions of the seal's size;
/// angles in radians.
struct SealGpuPart {
    /// A core::SealPartKind, or SEAL_EMBLEM_KIND.
    u32 kind = 0;
    /// Which of its layer's colours it takes: 0 or 1.
    u32 slot = 0;
    /// Where it sits (an emblem: 0, the centre).
    f32 radius = 0.0F;
    /// Its line's width.
    f32 width = 0.0F;
    /// Its items' size: a tick's length, a dot's diameter, a glyph's, flourish's or emblem's height.
    f32 size = 0.0F;
    /// How many ticks, dots, glyphs, flourishes, star points or polygon sides (an emblem: 1).
    u32 count = 0;
    /// A star's step.
    u32 step = 0;
    /// A dashed circle's dash; 0 is solid.
    f32 dash = 0.0F;
    /// A dashed circle's gap.
    f32 gap = 0.0F;
    /// Where its first item or corner is from its layer's zero.
    f32 rotation = 0.0F;
    /// How far a flourish may reach past its layer's outer edge.
    f32 spill = 0.0F;
    /// 1 when a script's glyphs face the centre, else 0.
    u32 inward = 0;
    /// Its first sprite in SealPack::sprites (scripts, flourishes and emblems), drawn in turn round the ring.
    u32 firstSprite = 0;
    /// How many sprites it has from `firstSprite`: a script's glyph set's, a flourish's 3, an emblem's 1; else 0.
    u32 spriteCount = 0;
    /// Pads the part to a multiple of 16 bytes.
    std::array<u32, 2> reserved{};
};
/// The bytes of one SealGpuPart.
inline constexpr std::size_t SEAL_GPU_PART_BYTES = 64;
static_assert(sizeof(SealGpuPart) == SEAL_GPU_PART_BYTES, "the seal shader reads parts as 64 bytes");
/// Where SealGpuPart::firstSprite is, in bytes: the HLSL `SealPart` keeps the same.
inline constexpr std::size_t SEAL_GPU_PART_SPRITES_AT = 48;
static_assert(offsetof(SealGpuPart, firstSprite) == SEAL_GPU_PART_SPRITES_AT, "the seal shader finds a part's sprites there");

/// One sprite a part draws, as the seal fragment shader reads it: the StructuredBuffer element `SealSprite` of
/// shaders/seal.hlsli, which must keep this layout (32 bytes; its float2s at 8-byte offsets).
struct SealGpuSprite {
    /// The top left of its distance field's block (gfx::FieldBlockOf()), in UV of its page.
    core::Vec2 fieldMin{};
    /// The bottom right of that block, in UV of its page.
    core::Vec2 fieldMax{};
    /// The sprite's width in texels (its block's less the field's reach each side).
    f32 width = 0.0F;
    /// The sprite's height in texels.
    f32 height = 0.0F;
    /// Its page: the layer of the field array.
    u32 page = 0;
    /// Pads the sprite to a multiple of 16 bytes.
    u32 reserved = 0;
};
/// The bytes of one SealGpuSprite.
inline constexpr std::size_t SEAL_GPU_SPRITE_BYTES = 32;
static_assert(sizeof(SealGpuSprite) == SEAL_GPU_SPRITE_BYTES, "the seal shader reads sprites as 32 bytes");
/// Where SealGpuSprite::page is, in bytes: the HLSL `SealSprite` keeps the same.
inline constexpr std::size_t SEAL_GPU_SPRITE_PAGE_AT = 24;
static_assert(offsetof(SealGpuSprite, page) == SEAL_GPU_SPRITE_PAGE_AT, "the seal shader finds a sprite's page there");
static_assert(offsetof(SealGpuSprite, fieldMax) % sizeof(core::Vec2) == 0, "a float2 of a StructuredBuffer is 8-aligned");

/// One layer of a seal (or its emblem, drawn as a layer of its own) as the seal shaders read it: the StructuredBuffer element
/// `SealLayer` of shaders/seal.hlsli, which must keep this layout (88 bytes; its float2s at 8-byte offsets).
struct SealGpuLayer {
    /// Its centre in the world: its seal's point, lifted and raised by its height at the art's angle.
    core::Vec2 centre{};
    /// Where one seal size along its plane's x lands in the world, from `centre`.
    core::Vec2 axisX{};
    /// Where one seal size along its plane's depth lands in the world, from `centre`.
    core::Vec2 axisDepth{};
    /// How far it has turned, in radians (SealLayerAngle()): its parts' zero and its frame turn with it.
    f32 angle = 0.0F;
    /// Its lines' strength: its opacity times its life (SealLifeOpacity()) times core::Seals::brightness, then capped at
    /// core::Seals::aboveOpacity off the ground.
    f32 opacity = 0.0F;
    /// Its seal's colours, slot 0 then 1 (slot 1 repeats slot 0 for a look of one): linear premultiplied RGBA8, red in the
    /// low byte (core::UiColours::Of()).
    std::array<u32, 2> colours{};
    /// Its first part in SealPack::parts.
    u32 firstPart = 0;
    /// How many parts it has from `firstPart`, sorted by where their bands start.
    u32 partCount = 0;
    /// Its inner radius, a fraction of the seal's size; a frame's inner polygon's corners.
    f32 inner = 0.0F;
    /// Its outer radius, a fraction of the seal's size (its band reaches further by its flourishes' spill); a frame's
    /// corners: its sides' middles are only `outer * cos(pi / frameSides)` out, so round parts past that clip there.
    f32 outer = 0.0F;
    /// Its frame's sides: 0 round, else 3 or more, a regular polygon with its corners at `inner` and `outer`.
    u32 frameSides = 0;
    /// Its frame's turn from its zero, in radians.
    f32 frameRotation = 0.0F;
    /// Its seal's ink wash's colour: the table's text ink, linear premultiplied RGBA8.
    u32 washColour = 0;
    /// How strongly its seal's wash shows: its darkness times its seal's life, capped as `opacity` is.
    f32 washOpacity = 0.0F;
    /// How far its seal's wash reaches, a fraction of the seal's size.
    f32 washReach = 0.0F;
    /// How soft its seal's wash's edge is, 0 (hard) to 1.
    f32 washSoftness = 0.0F;
    /// How far its flourishes reach past its outer radius (SealPackedLayer::spill), a fraction of the seal's size: a frame
    /// lets them through that far.
    f32 spill = 0.0F;
    /// Pads the layer to a multiple of 8 bytes, as its float2s need.
    u32 reserved = 0;
};
/// The bytes of one SealGpuLayer.
inline constexpr std::size_t SEAL_GPU_LAYER_BYTES = 88;
static_assert(sizeof(SealGpuLayer) == SEAL_GPU_LAYER_BYTES, "the seal shaders read layers as 88 bytes");
/// Where SealGpuLayer::colours is, in bytes: the HLSL `SealLayer` keeps the same.
inline constexpr std::size_t SEAL_GPU_LAYER_COLOURS_AT = 32;
/// Where SealGpuLayer::washColour is, in bytes: the HLSL `SealLayer` keeps the same.
inline constexpr std::size_t SEAL_GPU_LAYER_WASH_AT = 64;
static_assert(offsetof(SealGpuLayer, colours) == SEAL_GPU_LAYER_COLOURS_AT, "the seal shaders find a layer's colours there");
static_assert(offsetof(SealGpuLayer, washColour) == SEAL_GPU_LAYER_WASH_AT, "the seal shaders find a layer's wash there");
static_assert(offsetof(SealGpuLayer, axisDepth) % sizeof(core::Vec2) == 0, "a float2 of a StructuredBuffer is 8-aligned");

/// One corner of a band's or a wash's quad, as the seal vertex shader reads it (`SealVertex` in shaders/seal.vert.hlsl): 24
/// bytes.
struct SealVertex {
    /// Where it is in the world.
    core::Vec2 position{};
    /// Where it is on its seal's plane, in seal sizes from the centre, not turned with its layer: x, then depth.
    core::Vec2 local{};
    /// Its layer in SealDrawing::layers (a wash's: its seal's first).
    u32 layer = 0;
    /// Which way the vertex shader grows its quad by a pixel, so the band's anti-aliased edge never meets the geometry's
    /// end at any zoom: 1 out from the centre (a band's outer edge), -1 towards it (its inner edge), 0 not at all (a wash's
    /// corner, or a band's at the centre).
    f32 grow = 0.0F;
};
/// The bytes of one SealVertex.
inline constexpr std::size_t SEAL_VERTEX_BYTES = 24;
static_assert(sizeof(SealVertex) == SEAL_VERTEX_BYTES, "the seal pipelines read vertices as 24 bytes");

/// A seal's light as the particle draw reads it: the StructuredBuffer element `SealParticles` of shaders/seal.hlsli, which must
/// keep this layout (160 bytes; its float2s at 8-byte offsets). Each stream's age is already wrapped by its period and scaled
/// by core::Seals::spinScale, so its speeds are not scaled again; particle `i` of a stream staggers its own age from it, and
/// a spark or shard is born somewhere new each time it comes round (its stream's lap counts those, wrapping at 2^32).
struct SealGpuParticles {
    /// Its seal's plane's centre in the world, lifted at the art's angle.
    core::Vec2 centre{};
    /// Where one seal size along its plane's x lands in the world, from `centre`.
    core::Vec2 axisX{};
    /// Where one seal size along its plane's depth lands in the world, from `centre`.
    core::Vec2 axisDepth{};
    /// A hash of its seal's key, start tick and place, which seeds each particle's hash.
    u32 seed = 0;
    /// Its colour: linear premultiplied RGBA8.
    u32 colour = 0;
    /// Its strength: its seal's life times core::Seals::brightness, then capped as a layer's is.
    f32 opacity = 0.0F;
    /// Its seal's size, in world units.
    f32 size = 0.0F;
    /// Its plane's list's particle index of its first particle (SealDrawing::groundParticles' or aboveParticles'): its
    /// pillars, then sparks, then shards.
    u32 firstParticle = 0;
    /// How many pillars.
    u32 pillars = 0;
    /// How many sparks.
    u32 sparks = 0;
    /// How many shards.
    u32 shards = 0;
    /// The pillars' age, in seconds, below `pillarCycle`.
    f32 pillarAge = 0.0F;
    /// How long a pillar's light takes to fall its length once, in seconds (core::SealLight::pillarCycle).
    f32 pillarCycle = 0.0F;
    /// A pillar's height, in world units.
    f32 pillarHeight = 0.0F;
    /// A pillar's width, in world units.
    f32 pillarWidth = 0.0F;
    /// How much of a pillar's length it fades out over, towards its far end.
    f32 pillarFade = 0.0F;
    /// The sparks' age, in seconds, below `sparkLife`.
    f32 sparkAge = 0.0F;
    /// How long a spark lives, in seconds.
    f32 sparkLife = 0.0F;
    /// How fast a spark rises, in world units a second.
    f32 sparkRise = 0.0F;
    /// The shards' age, in seconds, below `shardCycle`.
    f32 shardAge = 0.0F;
    /// How long a shard drifts before it comes round again, in seconds: one seal size at its drift (or one turn at its spin
    /// when it doesn't drift); 0 when it neither drifts nor turns.
    f32 shardCycle = 0.0F;
    /// How fast a shard drifts, in world units a second.
    f32 shardDrift = 0.0F;
    /// How fast a shard turns, in radians a second.
    f32 shardSpin = 0.0F;
    /// Which way its pillars run from the plane, in world y: 1 down the screen (falling from an overhead seal towards the
    /// ground), -1 up it (rising from a seal on the ground or standing upright).
    f32 pillarFall = 0.0F;
    /// A spark's size across, in world units.
    f32 sparkSize = 0.0F;
    /// How many times the sparks' age has come round (wrapping at 2^32): each life of a spark is born somewhere new.
    u32 sparkLap = 0;
    /// A shard's length, in world units.
    f32 shardSize = 0.0F;
    /// How many times the shards' age has come round.
    u32 shardLap = 0;
    /// Its seal's ink: the wash's colour, linear premultiplied RGBA8, drawn under each particle's light.
    u32 washColour = 0;
    /// How strongly that ink shows under a particle at its full strength: its seal's wash's opacity.
    f32 washOpacity = 0.0F;
    /// How bright a pillar is between its falling light's passes (core::SealLight::pillarRest).
    f32 pillarRest = 0.0F;
    /// How far a pillar's falling light reaches each way of its middle, a share of its length (core::SealLight::pillarBand).
    f32 pillarBand = 0.0F;
    /// A shard's width over its length (core::SealLight::shardThin).
    f32 shardThin = 0.0F;
    /// How much of the way from a shard's middle to its points its light softens over (core::SealLight::shardEdge).
    f32 shardEdge = 0.0F;
    /// Pads the record to a multiple of 16 bytes.
    std::array<u32, 3> reserved{};
};
/// The bytes of one SealGpuParticles.
inline constexpr std::size_t SEAL_GPU_PARTICLES_BYTES = 160;
static_assert(sizeof(SealGpuParticles) == SEAL_GPU_PARTICLES_BYTES, "the particle draw reads seal light as 160 bytes");
/// Where SealGpuParticles::pillarAge (the first stream's) is, in bytes: the HLSL `SealParticles` keeps the same.
inline constexpr std::size_t SEAL_GPU_PARTICLES_STREAMS_AT = 56;
static_assert(
    offsetof(SealGpuParticles, pillarAge) == SEAL_GPU_PARTICLES_STREAMS_AT,
    "the particle draw finds the streams there"
);
/// Where SealGpuParticles::washColour is, in bytes: the HLSL `SealParticles` keeps the same.
inline constexpr std::size_t SEAL_GPU_PARTICLES_INK_AT = 124;
static_assert(offsetof(SealGpuParticles, washColour) == SEAL_GPU_PARTICLES_INK_AT, "the particle draw finds the ink there");
/// Where SealGpuParticles::pillarRest (the first of the shapes) is, in bytes: the HLSL `SealParticles` keeps the same.
inline constexpr std::size_t SEAL_GPU_PARTICLES_SHAPES_AT = 132;
static_assert(
    offsetof(SealGpuParticles, pillarRest) == SEAL_GPU_PARTICLES_SHAPES_AT,
    "the particle draw finds the shapes there"
);
static_assert(offsetof(SealGpuParticles, axisDepth) % sizeof(core::Vec2) == 0, "a float2 of a StructuredBuffer is 8-aligned");

/// SealParticle::kind of a pillar of light.
inline constexpr u32 SEAL_PILLAR = 0;
/// SealParticle::kind of a spark.
inline constexpr u32 SEAL_SPARK = 1;
/// SealParticle::kind of a shard.
inline constexpr u32 SEAL_SHARD = 2;

/// One particle of a seal's light where the particle vertex shader places it: a quad in the world, round its centre.
struct SealParticle {
    /// Its centre in the world.
    core::Vec2 centre{};
    /// From its centre to the middle of its far edge, in world units: half its length (a pillar's, away from its plane).
    core::Vec2 along{};
    /// From its centre to the middle of its side, in world units: half its width.
    core::Vec2 across{};
    /// How strongly it shows, 0 to 1, before its seal's opacity: a spark's and a drifting shard's rise and fall over its
    /// life, so none pops in or out.
    f32 strength = 0.0F;
    /// A pillar's falling light: how far along it the light is, 0 (at its plane) to below 1 (its far end); else 0.
    f32 phase = 0.0F;
    /// SEAL_PILLAR, SEAL_SPARK or SEAL_SHARD.
    u32 kind = SEAL_PILLAR;
};

/// Where particle `index` of `light` is (its pillars first, then sparks, then shards; below their sum), as the particle
/// vertex shader places it: `SealParticleAt` in shaders/seal.hlsli is the same function, with the same constants, and the
/// two must change together. A pure function of the record: its seed, the index and its stream's age and lap, never a
/// time since the seal's start and no state. A pillar stands at a point of its seal's disc for good, running `pillarHeight`
/// from its plane (down when `pillarFall` is 1), its light falling along it once a `pillarCycle`; a spark is born at a point
/// of the disc and rises `sparkRise` a second over its life; a shard is born at a point of the disc and drifts out from the
/// centre across the plane at `shardDrift`, turning at `shardSpin` on the plane, `shardThin` of its length wide. Each particle
/// staggers its stream's age by its own hash, and a spark or shard is born somewhere new each lap.
[[nodiscard]] SealParticle SealParticleAt(const SealGpuParticles& light, u32 index);

/// One layer of a look as PackSeals() packed it.
struct SealPackedLayer {
    /// Its first part in SealPack::parts.
    u32 firstPart = 0;
    /// How many parts it has from `firstPart`, sorted by where their bands start (nearest the centre first).
    u32 partCount = 0;
    /// How far its band reaches past its outer radius: its flourishes' greatest spill, a fraction of the seal's size.
    f32 spill = 0.0F;
};

/// One look as PackSeals() packed it.
struct SealPackedLook {
    /// Its first layer in SealPack::layers: it has as many as its look (at most core::MAX_SEAL_LAYERS).
    u32 firstLayer = 0;
    /// Its emblem's part in SealPack::parts, when its look has an emblem.
    u32 emblemPart = 0;
    /// How far its emblem's disc reaches, a fraction of the seal's size: it holds the emblem's whole distance field, which
    /// reaches core::FIELD_REACH_TEXELS past its sprite, however it turns.
    f32 emblemReach = 0.0F;
    /// How many seals drawing it adds at each satellite depth (0: a seal in play, 1: its satellite, 2: a satellite's):
    /// itself and every satellite DrawSeals() would draw on it, each with its own. A full frame counts them all in
    /// SealDrawing::droppedSeals without walking them.
    std::array<u32, core::MAX_SATELLITE_DEPTH + 1> seals{};
};

/// Every look's parts and sprites as the seal shaders read them, packed once for a table: the renderer calls PackSeals() when
/// it loads `seals.yaml` and on every reload, and uploads `parts` and `sprites` then; each frame DrawSeals() only points its
/// layers at their look's parts.
struct SealPack {
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// Every look's layers' parts, each layer's sorted by where their bands start, nearest the centre first (ties in the
    /// look's order: the seal fragment shader stops at the first part whose band starts past its pixel), then its
    /// emblem's.
    std::vector<SealGpuPart> parts{};
    /// The parts' sprites: every glyph set's first (its scripts share them), then each flourish's and emblem's.
    std::vector<SealGpuSprite> sprites{};
    /// Every look's layers, each look's together.
    std::vector<SealPackedLayer> layers{};
    /// Every look, in core::Seals::looks' order.
    std::vector<SealPackedLook> looks{};
    // NOLINTEND(readability-redundant-member-init)
};

/// The pack of `seals`' looks, their sprites' field blocks from `manifest` (gfx::FieldBlockOf() in UV of their page).
[[nodiscard]] SealPack PackSeals(const core::Seals& seals, const core::Manifest& manifest);

/// The bytes a seal buffer of `capacity` bytes grows to so it holds `bytes`: `capacity` when it already does, else twice
/// `capacity` or `bytes`, whichever is more (so a reload that grows the table a little reuses it), held to the most a u32
/// counts; none when `bytes` is past that, which no GPU buffer holds.
[[nodiscard]] std::optional<u32> SealBufferRoom(u32 capacity, std::size_t bytes);

/// What one frame draws of its seals: the ground seals' washes and bands (drawn after the ground batches), the upright and
/// overhead ones' (drawn after the outlines, under the swords' trails and the overlay), each as quads of QUAD_VERTICES corners
/// in MeshVertex's corner order, and the layers and light records they index (the layers' parts are the table's SealPack). The
/// renderer keeps one and clears it each frame, so its buffers keep their capacity.
struct SealDrawing {
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// The ground seals' washes, one quad a seal, and the shadows overhead seals cast on the ground
    /// (core::SealLook::washShadow), one quad each: drawn with normal premultiplied blending.
    std::vector<SealVertex> groundWash{};
    /// The ground seals' bands: their layers' quads, drawn additively over the washes.
    std::vector<SealVertex> groundLines{};
    /// The upright and overhead seals' washes.
    std::vector<SealVertex> aboveWash{};
    /// The upright and overhead seals' bands.
    std::vector<SealVertex> aboveLines{};
    /// Every drawn seal's layers, each seal's in its look's order, then its emblem's, then its shadow's.
    std::vector<SealGpuLayer> layers{};
    /// One record for each drawn ground seal with light, its particles counted from the list's first: drawn after the ground
    /// seals' bands.
    std::vector<SealGpuParticles> groundParticles{};
    /// One record for each drawn upright or overhead seal with light: drawn after their bands.
    std::vector<SealGpuParticles> aboveParticles{};
    // NOLINTEND(readability-redundant-member-init)
    /// Seals left out, satellites included, past MAX_SEALS_DRAWN.
    std::size_t droppedSeals = 0;
    /// Seals left out, satellites included, whose size isn't a normal f32 (a satellite of a satellite of a tiny seal),
    /// with the satellites they would have drawn: nothing of them is laid out.
    std::size_t tinySeals = 0;
    /// Particles left out of seals whose light has more than core::MAX_SEAL_PARTICLES (which the reader refuses): each seal
    /// keeps its pillars first, then its sparks, then its shards, up to the limit.
    std::size_t droppedParticles = 0;

    /// Empties it, keeping its buffers' capacity.
    void Clear();
};

/// Adds to `out` every seal of `seals` in play at the render clock, `renderSeconds` (core::RenderTick() in seconds), whose
/// whole tick is `renderTick` (in play from its start tick to the tick before its end), and its satellites, nested to
/// core::MAX_SATELLITE_DEPTH, each by its look in `looks` and its parts in `pack` (PackSeals() of `looks`), its colours from
/// `colours`. A showcase seal is left out when `showcase` is false; one whose key `looks` hasn't is skipped. Each layer adds
/// its band: an annulus of SEAL_BAND_SEGMENTS quads from SealBandMargin() of `looks`' halo inside its inner radius (or its
/// centre) to that margin past its outer (the quads' outer edges round the circle, and further by its flourishes' spill), or
/// its frame's band between two regular polygons with their corners there; each seal its emblem (a layer of its own, at its
/// innermost layer's height and life), its wash's quad to its reach (and an overhead seal's shadow: its wash's quad laid on
/// the ground under it, in `out.groundWash` with a layer record of its own, as dark as its shadow times its life, capped as
/// its wash is) and its light's record (in its plane's list, its
/// particles cut to core::MAX_SEAL_PARTICLES, the rest counted in `out.droppedParticles`). Seals past MAX_SEALS_DRAWN are
/// counted in `out.droppedSeals`, not drawn; a seal whose size isn't a normal f32 in `out.tinySeals`, with its satellites. A
/// colour slot naming `element` takes the record's element's effect colour, which for a seal without one (`hasElement` false:
/// the showcase's) is its default element's, Damage. Everything runs on the seals' time, scaled by core::Seals::spinScale:
/// spins, particle motion and life, and the appear and fade; at 0 (still) the appear and fade are instant, so the seal shows
/// whole from its start to its end. Its wash is the table's text ink, so it recolours with the palette.
void DrawSeals(
    std::span<const core::Seal> seals,
    const core::Seals& looks,
    const SealPack& pack,
    const core::UiColours& colours,
    f64 renderSeconds,
    u64 renderTick,
    bool showcase,
    SealDrawing& out
);

} // namespace gfx
