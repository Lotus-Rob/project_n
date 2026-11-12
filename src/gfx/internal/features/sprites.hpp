#pragma once

/// @file
/// @ingroup gfx
/// Drawing the snapshot's sprite instances (design doc 0002 §8.5): uploaded into a storage buffer
/// when the drawn snapshot changes, expanded into quads and interpolated by the vertex shader.

#include "core/error.hpp"
#include "core/frame.hpp"
#include "core/glow.hpp"
#include "core/hit.hpp"
#include "gfx/camera.hpp"
#include "gfx/glow_table.hpp"
#include "gfx/internal/gpu.hpp"
#include "gfx/internal/resources/sprite_array.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <expected>
#include <span>

namespace gfx::internal {

/// The most instances the storage buffer holds: the stress arena's 100,000 and some room (§8.1).
inline constexpr u32 MAX_INSTANCES = 1U << 17U;

/// A ring drawn around one uploaded instance's sprite, over everything (the hovered entity).
struct Outline {
    /// The instance, by its index in the uploaded snapshot.
    u32 instance = 0;
    /// The ring's premultiplied RGBA8 colour (red in the low byte); never 0.
    u32 colour = 0;
    /// The ring's width in UV on the atlas page.
    f32 ring = 0.0F;
};

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

/// What every glow a frame draws shares (the phase 6 glow spec, "Drawing").
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
    /// as `glows` says; one without draws as before, reading no field. Instances past what fitted aren't drawn.
    void Draw(
        SDL_GPUCommandBuffer* commands,
        SDL_GPURenderPass* pass,
        std::span<const core::Batch> batches,
        const SpriteArray& sprites,
        const ClipTransform& transform,
        f32 alpha,
        const GlowFrame& glows
    ) const;

    /// Draws `outline` in `pass` after Draw(), with the same `sprites`, `transform` and `alpha`: the
    /// transparent pixels within the ring's width of its instance's sprite take the ring's colour. `glows` is bound as
    /// Draw() binds it; the outline draws no glow. Nothing when the instance wasn't uploaded.
    void DrawOutline(
        SDL_GPUCommandBuffer* commands,
        SDL_GPURenderPass* pass,
        const Outline& outline,
        const SpriteArray& sprites,
        const ClipTransform& transform,
        f32 alpha,
        const GlowFrame& glows
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
