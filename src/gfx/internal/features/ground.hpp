#pragma once

/// @file
/// @ingroup gfx
/// The world map's painted ground: one quad over the map's part of the view, painted by the ground shader from the ground
/// field (gfx::GroundTexels()) and the look's uniforms (gfx::GroundUniformsOf()), and shaded by the fog as land is.

#include "core/error.hpp"
#include "core/rect.hpp"
#include "core/types.hpp"
#include "core/world_look.hpp"
#include "core/world_map.hpp"
#include "gfx/camera.hpp"
#include "gfx/ground.hpp"
#include "gfx/internal/features/sprites.hpp"
#include "gfx/internal/gpu.hpp"

#include <SDL3/SDL.h>

#include <expected>
#include <optional>
#include <vector>

namespace gfx::internal {

/// The ground pipeline (premultiplied over: opaque for the ground and the paper, the veil laid over what is drawn), the
/// ground field's texture and its two samplers.
class GroundRenderer {
public:
    /// Creates the pipeline for colour targets of `format` and the samplers, or says why it couldn't. Nothing draws until
    /// Update() has built a field.
    [[nodiscard]] static std::expected<GroundRenderer, core::Error> Create(SDL_GPUDevice* device, SDL_GPUTextureFormat format);

    /// Makes the field `map`'s under `look` (its water kinds), copying it in `copy`, unless the texture already holds the
    /// field built for `version` (a count that changes whenever the map or the look does); a map of another size makes
    /// the texture anew. Fails when the texture or the transfer buffer can't be made or mapped; then nothing draws until a
    /// later Update() succeeds.
    [[nodiscard]] std::expected<void, core::Error> Update(
        SDL_GPUCopyPass* copy,
        const core::WorldMap& map,
        const core::WorldLook& look,
        u64 version
    );

    /// The command buffer holding the last Update()'s copy was submitted, or failed to be, or was cancelled: a copy counts
    /// as held only once submitted, so a dropped one is made again.
    void Submitted(bool submitted);

    /// Draws the ground over `quad` (world units; gfx::GroundQuad()) in `pass`, placed by `worldToClip`, painted by
    /// `uniforms` and shaded by `fog` as land is. Nothing when the field isn't ready.
    void Draw(
        SDL_GPUCommandBuffer* commands,
        SDL_GPURenderPass* pass,
        const ClipTransform& worldToClip,
        const core::Rect& quad,
        const GroundUniforms& uniforms,
        const FogTexture& fog
    ) const;

private:
    SDL_GPUDevice* m_device = nullptr;
    Gpu<SDL_GPUGraphicsPipeline> m_pipeline = nullptr;
    Gpu<SDL_GPUTexture> m_field = nullptr; // m_columns × m_rows, once a field is built
    Gpu<SDL_GPUTransferBuffer> m_transfer = nullptr;
    Gpu<SDL_GPUSampler> m_point = nullptr;  // the kind: indices aren't filtered
    Gpu<SDL_GPUSampler> m_linear = nullptr; // the shore distance
    u32 m_columns = 0;
    u32 m_rows = 0;
    std::vector<u8> m_texels;                     // the last field built, kept for its capacity
    std::optional<u64> m_held = std::nullopt;     // the version m_field holds, once submitted
    std::optional<u64> m_recorded = std::nullopt; // the version copied in the command buffer not yet submitted
    bool m_ready = false;                         // whether this frame's Update() left a field to draw
};

} // namespace gfx::internal
