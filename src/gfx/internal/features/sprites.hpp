#pragma once

/// @file
/// @ingroup gfx
/// Drawing the snapshot's sprite instances (design doc 0002 §8.5): uploaded into a storage buffer
/// when the drawn snapshot changes, expanded into quads and interpolated by the vertex shader.

#include "core/error.hpp"
#include "core/frame.hpp"
#include "gfx/camera.hpp"
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

    /// Draws the uploaded instances in `pass`, a draw per batch of `batches` (the uploaded
    /// snapshot's), textured from `sprites`, placed by `transform` and interpolated by `alpha` (0:
    /// previous tick, 1: this tick). Instances past what fitted aren't drawn.
    void Draw(
        SDL_GPUCommandBuffer* commands,
        SDL_GPURenderPass* pass,
        std::span<const core::Batch> batches,
        const SpriteArray& sprites,
        const ClipTransform& transform,
        f32 alpha
    ) const;

    /// Draws `outline` in `pass` after Draw(), with the same `sprites`, `transform` and `alpha`: the
    /// transparent pixels within the ring's width of its instance's sprite take the ring's colour.
    /// Nothing when the instance wasn't uploaded.
    void DrawOutline(
        SDL_GPUCommandBuffer* commands,
        SDL_GPURenderPass* pass,
        const Outline& outline,
        const SpriteArray& sprites,
        const ClipTransform& transform,
        f32 alpha
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
