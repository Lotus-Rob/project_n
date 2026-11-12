#pragma once

/// @file
/// @ingroup gfx
/// The glow table: every role's look, flattened for the shaders to read as a StructuredBuffer (design doc: the phase 6
/// glow spec, "Drawing"). It changes only when the effects data or the palette does.

#include "core/effects.hpp"
#include "core/glow.hpp"
#include "core/types.hpp"
#include "core/ui_colours.hpp"

#include <array>
#include <cstddef>

namespace gfx {

/// One role's glow as a shader reads it: the StructuredBuffer element `GlowRecord` of shaders/sprite.vert.hlsl, which must
/// keep this layout (every field 4 bytes, the size a multiple of 16).
struct GlowRecord {
    /// How far past the sprite's edge it reaches at zoom 1, in texels.
    f32 size = 0.0F;
    /// 0 a hard ring, 1 a soft fade.
    f32 feather = 0.0F;
    /// Its strength at full drive.
    f32 opacity = 0.0F;
    /// How much it keeps its width on screen as the camera zooms.
    f32 zoomShrink = 0.0F;
    /// A core::GlowBlend.
    u32 blend = 0;
    /// Linear premultiplied RGBA8 (core::UiColours::Of()); 0 for the ability's element colour.
    u32 colour = 0;
    /// A core::DriveKind.
    u32 driveKind = 0;
    /// A core::GlowCurve: the drive's curve.
    u32 curve = 0;
    /// A pulse's beat in ticks.
    f32 periodTicks = 0.0F;
    /// How far a pulse falls below full strength.
    f32 depth = 0.0F;
    /// A charge's fade in ticks.
    f32 fadeTicks = 0.0F;
    /// A core::GlowCurve: the fade's curve.
    u32 fadeCurve = 0;
    /// How many of `results` count.
    u32 resultCount = 0;
    /// A progress's result colours, as `colour` is.
    std::array<u32, core::MAX_GLOW_RESULTS> results{};
    /// Fills the record up to a multiple of 16 bytes.
    std::array<u32, 3> padding{};
};
/// The bytes of one GlowRecord.
inline constexpr std::size_t GLOW_RECORD_BYTES = 96;
static_assert(sizeof(GlowRecord) == GLOW_RECORD_BYTES, "the shaders read glow records as 96 bytes");
/// The unit the record's size is kept a multiple of, for the HLSL `GlowRecord` (shaders/sprite.vert.hlsl) to match.
inline constexpr std::size_t GLOW_RECORD_SIZE_UNIT = 16;
static_assert(GLOW_RECORD_BYTES % GLOW_RECORD_SIZE_UNIT == 0, "a glow record's size is a multiple of 16 bytes");

/// The render clock as the glow shaders take it: an f32 can't hold a long session's tick, so its whole tick's low 32 bits
/// and the fraction past it go apart, and a drive's age is their wrapping difference from its start tick's low bits.
struct GlowTime {
    /// The low 32 bits of the render clock's whole tick (core::RenderTick() rounded down).
    u32 tick = 0;
    /// How far past `tick` the render clock is, from 0 up to (not including) 1.
    f32 fraction = 0.0F;
};

/// The render clock at `alpha` (0 to 1) of the way from the tick before `simulationTick` to it (core::RenderTick()), as
/// GlowTime splits it.
[[nodiscard]] GlowTime GlowTimeAt(u64 simulationTick, f64 alpha);

/// Every role's record, indexed by core::GlowRole (None's is zero).
using GlowTable = std::array<GlowRecord, core::GLOW_ROLES>;
// MSVC warns (C4686) at a call of a function returning a specialisation that nothing has instantiated by its declaration:
// the size check completes it here.
static_assert(sizeof(GlowTable) == core::GLOW_ROLES * GLOW_RECORD_BYTES);

/// The table of every role's record, from `effects`' looks and `colours`' palette.
[[nodiscard]] GlowTable BuildGlowTable(const core::Effects& effects, const core::UiColours& colours);

} // namespace gfx
