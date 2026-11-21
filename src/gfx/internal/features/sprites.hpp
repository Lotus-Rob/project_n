#pragma once

/// @file
/// @ingroup gfx
/// Drawing the snapshot's sprite instances: uploaded into a storage buffer
/// when the drawn snapshot changes, expanded into quads and interpolated by the vertex shader.

#include "core/error.hpp"
#include "core/frame.hpp"
#include "core/glow.hpp"
#include "core/hit.hpp"
#include "core/world_look.hpp"
#include "gfx/camera.hpp"
#include "gfx/fog.hpp"
#include "gfx/glow_table.hpp"
#include "gfx/internal/gpu.hpp"
#include "gfx/internal/resources/sprite_array.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <expected>
#include <optional>
#include <span>

namespace gfx::internal {

/// The most instances the storage buffer holds: the stress arena's 100,000 and some room.
inline constexpr u32 MAX_INSTANCES = 1U << 17U;

/// The glow table (gfx::BuildGlowTable()) in a storage buffer the sprite shaders read.
class GlowTableBuffer {
public:
    /// Creates the buffer holding `table`, uploaded and submitted before it returns, or says why it couldn't.
    [[nodiscard]] static std::expected<GlowTableBuffer, core::Error> Create(
        SDL_GPUDevice* device,
        const std::array<GlowRecord, core::GLOW_ROLES>& table
    );

    /// Copies `table` into the buffer in `copy`, cycling it, so frames in flight keep the previous table. Fails when the
    /// transfer buffer can't be mapped; then the buffer keeps what it held.
    [[nodiscard]] std::expected<void, core::Error> Upload(
        SDL_GPUCopyPass* copy,
        const std::array<GlowRecord, core::GLOW_ROLES>& table
    );

    /// The storage buffer, for GlowFrame::table.
    [[nodiscard]] SDL_GPUBuffer* Buffer() const {
        return m_buffer.get();
    }

private:
    SDL_GPUDevice* m_device = nullptr;
    Gpu<SDL_GPUBuffer> m_buffer = nullptr;
    Gpu<SDL_GPUTransferBuffer> m_transfer = nullptr;
};

/// What every glow a frame draws shares.
struct GlowFrame {
    /// The glow table's storage buffer (GlowTableBuffer::Buffer()); never null.
    SDL_GPUBuffer* table = nullptr;
    /// The render clock's whole tick, low 32 bits: drives count from an instance's start tick to it.
    u32 tick = 0;
    /// How far past `tick` the render clock is, from 0 to 1.
    f32 fraction = 0.0F;
    /// The camera's zoom: a glow's reach shrinks as it grows, by the glow's zoom shrink.
    f32 zoom = 1.0F;
    /// The reduce flashing setting.
    bool reduceFlashing = false;
    /// The palette's effect colour of each core::HitKind (core::EffectColour()), linear premultiplied RGBA8, for a glow
    /// in its ability's element colour.
    std::array<u32, core::HIT_KINDS> elementColours{};
    /// One texel of an atlas page, in UV: 1 / the page size.
    f32 texelUv = 0.0F;
};

/// The fog's values as a two-channel texture with a texel per tile (explored, then in sight), which every sprite draw
/// binds, and the uniforms its frame shades by. With no fog it binds a 1 × 1 texture, explored and in sight.
class FogTexture {
public:
    /// Creates the 1 × 1 texture, uploaded and submitted before it returns, and the sampler, or says why it couldn't.
    [[nodiscard]] static std::expected<FogTexture, core::Error> Create(SDL_GPUDevice* device);

    /// Makes `fog`, in `look`'s colours, what the frame's sprites are shaded by, copying its values in `copy` when their
    /// revision or the map's size differs from what the texture holds; a texture of another size is made anew. Fails when
    /// that texture or the transfer buffer can't be made or mapped; then the frame draws without the fog.
    [[nodiscard]] std::expected<void, core::Error> Update(
        SDL_GPUCopyPass* copy,
        const FogFrame& fog,
        const core::FogLook& look
    );

    /// The command buffer holding the last Update()'s copy was submitted, or failed to be, or was cancelled: a copy counts
    /// as held only once submitted, so a dropped one is made again.
    void Submitted(bool submitted);

    /// The texture the frame's sprites sample and its sampler.
    [[nodiscard]] SDL_GPUTextureSamplerBinding Binding() const;

    /// The frame's fog uniforms.
    [[nodiscard]] const FogUniforms& Uniforms() const {
        return m_uniforms;
    }

private:
    SDL_GPUDevice* m_device = nullptr;
    Gpu<SDL_GPUTexture> m_clear = nullptr; // 1 × 1, explored and in sight
    Gpu<SDL_GPUTexture> m_field = nullptr; // m_columns × m_rows, once a view has had fog
    Gpu<SDL_GPUTransferBuffer> m_transfer = nullptr;
    Gpu<SDL_GPUSampler> m_sampler = nullptr;
    u32 m_columns = 0;
    u32 m_rows = 0;
    std::optional<u64> m_held = std::nullopt;     // the revision m_field holds, once submitted
    std::optional<u64> m_recorded = std::nullopt; // the revision copied in the command buffer not yet submitted
    FogUniforms m_uniforms{};
};

/// The sprite pipelines (one per core::Pipeline) and their instance buffer.
class SpriteRenderer {
public:
    /// Creates the pipelines for colour targets of `format` and a storage buffer of `capacity` instances, named `name`
    /// for graphics debuggers: the snapshot's MAX_INSTANCES, or the overlay's fewer.
    [[nodiscard]] static std::expected<SpriteRenderer, core::Error> Create(
        SDL_GPUDevice* device,
        SDL_GPUTextureFormat format,
        u32 capacity = MAX_INSTANCES,
        const char* name = "sprite instances"
    );

    /// Copies `instances` into the storage buffer in `copy`, cycling it, so frames in flight keep
    /// the previous contents. Returns how many didn't fit (past its capacity) and aren't drawn.
    /// Fails when the transfer buffer can't be mapped; then nothing is drawn until an upload succeeds.
    [[nodiscard]] std::expected<std::size_t, core::Error> Upload(
        SDL_GPUCopyPass* copy,
        std::span<const core::Instance> instances
    );

    /// Draws the uploaded instances in `pass`, a draw per batch of `batches` (the uploaded instances' batches, in any order
    /// of their instances), textured from `sprites`, placed by `transform` and interpolated by `alpha` (0:
    /// previous tick, 1: this tick). An instance wearing a glow draws it under itself, its quad grown by the glow's reach,
    /// as `glows` says; one without draws as before, reading no field. While `fog` is on, an instance flagged
    /// core::INSTANCE_FOGGED_LAND is drained, veiled and shaded by its pixels' fog, one flagged core::INSTANCE_FOG_SHADE
    /// only shaded, and one flagged core::INSTANCE_SIGHT_FADE fades with the sight at its core::SightPoint(). Instances past
    /// what fitted aren't drawn.
    void Draw(
        SDL_GPUCommandBuffer* commands,
        SDL_GPURenderPass* pass,
        std::span<const core::Batch> batches,
        const SpriteArray& sprites,
        const ClipTransform& transform,
        f32 alpha,
        const GlowFrame& glows,
        const FogTexture& fog
    ) const;

    /// Draws instance `instance` (by its index in the uploaded snapshot) again in `pass` after Draw(), with the same
    /// `sprites`, `transform`, `alpha`, `glows` and `fog`, as `role`'s glow alone: outside the sprite, from the instance's
    /// own drive (its value and start), ignoring the instance's glow, colour alpha and dissolve, and drawing no sprite
    /// colour; the fog treats it as it does the instance. Nothing when the instance wasn't uploaded or `role` is None.
    void DrawGlowOnly(
        SDL_GPUCommandBuffer* commands,
        SDL_GPURenderPass* pass,
        u32 instance,
        core::GlowRole role,
        const SpriteArray& sprites,
        const ClipTransform& transform,
        f32 alpha,
        const GlowFrame& glows,
        const FogTexture& fog
    ) const;

private:
    SDL_GPUDevice* m_device = nullptr;
    std::array<Gpu<SDL_GPUGraphicsPipeline>, core::PIPELINES> m_pipelines{};
    Gpu<SDL_GPUBuffer> m_instances = nullptr;
    Gpu<SDL_GPUTransferBuffer> m_transfer = nullptr;
    u32 m_capacity = 0;
    u32 m_count = 0;
};

} // namespace gfx::internal
