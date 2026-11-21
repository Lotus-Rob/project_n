#pragma once

/// @file
/// @ingroup gfx
/// The world map's rivers: the painted bands gfx::DrawRivers() lays out once for a map and a look, drawn premultiplied
/// over the ground in world units, the edge band's rim fading out softly and every side anti-aliased, and shaded by the fog
/// as land is.

#include "core/error.hpp"
#include "core/types.hpp"
#include "core/world_look.hpp"
#include "core/world_map.hpp"
#include "gfx/camera.hpp"
#include "gfx/internal/features/ribbons.hpp"
#include "gfx/internal/features/sprites.hpp"
#include "gfx/internal/gpu.hpp"
#include "gfx/ribbon.hpp"

#include <SDL3/SDL.h>

#include <expected>
#include <optional>
#include <vector>

namespace gfx::internal {

/// The river pipeline (premultiplied over: ONE, ONE_MINUS_SRC_ALPHA) and its buffers.
class RiverRenderer {
public:
    /// Creates the pipeline for colour targets of `format`, a vertex buffer of MAX_RIVER_QUADS and the index buffer, or
    /// says why it couldn't.
    [[nodiscard]] static std::expected<RiverRenderer, core::Error> Create(SDL_GPUDevice* device, SDL_GPUTextureFormat format);

    /// Lays out `map`'s rivers in `look`'s shape and copies them in `copy`, unless the buffer already holds those laid out
    /// for `version` (a count that changes whenever the map or the look does). Fails when a transfer buffer can't be
    /// mapped; then nothing draws until a later Update() succeeds.
    [[nodiscard]] std::expected<void, core::Error> Update(
        SDL_GPUCopyPass* copy,
        const core::WorldMap& map,
        const core::WorldLook& look,
        u64 version
    );

    /// The command buffer holding the last Update()'s copy was submitted, or failed to be, or was cancelled: a copy counts
    /// as held only once submitted, so a dropped one is made again.
    void Submitted(bool submitted);

    /// Draws the rivers in `pass`, every edge band and then every core, placed by `worldToClip` and shaded by `fog` as land
    /// is. Nothing when none is ready.
    void Draw(
        SDL_GPUCommandBuffer* commands,
        SDL_GPURenderPass* pass,
        const ClipTransform& worldToClip,
        const FogTexture& fog
    ) const;

private:
    SDL_GPUDevice* m_device = nullptr;
    Gpu<SDL_GPUGraphicsPipeline> m_pipeline = nullptr;
    RibbonBuffers m_buffers{};
    std::vector<RibbonVertex> m_laid;             // the last layout, kept for its capacity
    std::optional<u64> m_held = std::nullopt;     // the version the buffers hold, once submitted
    std::optional<u64> m_recorded = std::nullopt; // the version copied in the command buffer not yet submitted
    bool m_indicesHeld = false;
    bool m_indicesRecorded = false;
    u32 m_quads = 0;       // in the layout the buffers hold or are being given, both bands
    f32 m_rimStart = 1.0F; // RimStart() of that layout's shape
    bool m_ready = false;  // whether this frame's Update() left rivers to draw
};

} // namespace gfx::internal
