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

/// The sprite pipelines (one per core::Pipeline) and their instance buffer.
class SpriteRenderer {
public:
    /// Creates the pipelines for colour targets of `format` and a storage buffer of MAX_INSTANCES.
    [[nodiscard]] static std::expected<SpriteRenderer, core::Error> Create(SDL_GPUDevice* device, SDL_GPUTextureFormat format);

    /// Copies `instances` into the storage buffer in `copy`, cycling it, so frames in flight keep
    /// the previous contents. Returns how many didn't fit (past MAX_INSTANCES) and aren't drawn.
    [[nodiscard]] std::size_t Upload(SDL_GPUCopyPass* copy, std::span<const core::Instance> instances);

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

private:
    SDL_GPUDevice* m_device = nullptr;
    std::array<Gpu<SDL_GPUGraphicsPipeline>, core::PIPELINES> m_pipelines{};
    Gpu<SDL_GPUBuffer> m_instances = nullptr;
    Gpu<SDL_GPUTransferBuffer> m_transfer = nullptr;
    u32 m_count = 0;
};

} // namespace gfx::internal
