#pragma once

/// @file
/// @ingroup gfx
/// Drawing the seals (the phase 6 chunk 4b spec, "Drawing"): what gfx::DrawSeals() lays out each frame, its washes with
/// normal premultiplied blending, then its bands' lines additively, each layer's parts drawn as distance fields by the seal
/// fragment shader from the table's pack (gfx::SealPack), which is uploaded only when the table is packed again.

#include "core/error.hpp"
#include "core/seals.hpp"
#include "core/types.hpp"
#include "gfx/camera.hpp"
#include "gfx/internal/gpu.hpp"
#include "gfx/internal/resources/sprite_array.hpp"
#include "gfx/mesh_vertex.hpp"
#include "gfx/seal_draw.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <optional>
#include <vector>

namespace gfx::internal {

/// The most layers a frame's seals have: every drawn seal's, its emblem's among them.
inline constexpr u32 MAX_SEAL_LAYERS_DRAWN = static_cast<u32>(MAX_SEALS_DRAWN) * (core::MAX_SEAL_LAYERS + 1);
/// The most quads a frame's seals take: for every drawn seal, its layers' bands (each round or a frame of at most
/// core::MAX_SEAL_FRAME_SIDES sides), its emblem's and its wash.
inline constexpr u32 MAX_SEAL_QUADS =
    static_cast<u32>(MAX_SEALS_DRAWN) *
    ((core::MAX_SEAL_LAYERS * std::max(SEAL_BAND_SEGMENTS, core::MAX_SEAL_FRAME_SIDES)) + SEAL_BAND_SEGMENTS + 1);

/// Where a frame's seals are drawn.
struct SealTarget {
    /// How the world is placed on the target.
    ClipTransform worldToClip{};
    /// The target's width in pixels.
    u32 width = 0;
    /// Its height in pixels.
    u32 height = 0;
};

/// The seal pipelines (the wash's normal premultiplied blend, ONE, ONE_MINUS_SRC_ALPHA; the lines' additive one, ONE, ONE),
/// the table's pack and the buffers they read.
class SealRenderer {
public:
    /// Creates the pipelines for colour targets of `format`, the vertex buffer of MAX_SEAL_QUADS, the layer buffer of
    /// MAX_SEAL_LAYERS_DRAWN and the index buffer, with room for a small pack.
    [[nodiscard]] static std::expected<SealRenderer, core::Error> Create(SDL_GPUDevice* device, SDL_GPUTextureFormat format);

    /// Takes `pack` (gfx::PackSeals() of `seals`, the table drawn from now on) and `seals`' halo: the next Upload() copies
    /// the pack's parts and sprites, and the draws hand the halo and its band margin (gfx::SealBandMargin()) to the shader.
    void SetPack(SealPack pack, const core::Seals& seals);

    /// The pack the seals are drawn from, for gfx::DrawSeals().
    [[nodiscard]] const SealPack& Pack() const {
        return m_pack;
    }

    /// Copies `drawing`'s quads (whole quads, QUAD_VERTICES each: the ground's washes and lines, then the others') and
    /// layers into their buffers in `copy`, cycling them, and the pack too when it is new since the last upload that was
    /// submitted (Submitted()), and the quad indices the first time. Returns how many quads didn't fit (past
    /// MAX_SEAL_QUADS, the later lists' first) and aren't drawn. Fails when a buffer can't be made or a transfer buffer
    /// mapped; then no seal is drawn until an upload succeeds.
    [[nodiscard]] std::expected<std::size_t, core::Error> Upload(SDL_GPUCopyPass* copy, const SealDrawing& drawing);

    /// The command buffer holding the last Upload() was submitted (`submitted`), or failed to be, or was cancelled: a pack
    /// it copied is held only once it was submitted, else the next upload copies it again.
    void Submitted(bool submitted);

    /// Draws the uploaded ground seals in `pass` on `target`, their sprites' fields from `sprites`: the washes, then the
    /// lines, each band grown a pixel past its edges.
    void DrawGround(
        SDL_GPUCommandBuffer* commands,
        SDL_GPURenderPass* pass,
        const SealTarget& target,
        const SpriteArray& sprites
    ) const;

    /// Draws the uploaded upright and overhead seals likewise.
    void DrawAbove(
        SDL_GPUCommandBuffer* commands,
        SDL_GPURenderPass* pass,
        const SealTarget& target,
        const SpriteArray& sprites
    ) const;

private:
    // A run of the uploaded quads: one draw.
    struct Run {
        u32 first = 0;
        u32 quads = 0;
    };

    void DrawRuns(
        SDL_GPUCommandBuffer* commands,
        SDL_GPURenderPass* pass,
        const SealTarget& target,
        const SpriteArray& sprites,
        Run wash,
        Run lines
    ) const;
    // Makes `buffer` and `transfer` hold `bytes` at least, named `name` (SealBufferRoom()); keeps them when they already
    // do, and fails when no buffer can.
    [[nodiscard]] bool Reserve(
        Gpu<SDL_GPUBuffer>& buffer,
        Gpu<SDL_GPUTransferBuffer>& transfer,
        u32& capacity,
        std::size_t bytes,
        const char* name
    );
    [[nodiscard]] std::expected<void, core::Error> UploadPack(SDL_GPUCopyPass* copy);

    SDL_GPUDevice* m_device = nullptr;
    Gpu<SDL_GPUGraphicsPipeline> m_wash = nullptr;
    Gpu<SDL_GPUGraphicsPipeline> m_lines = nullptr;
    Gpu<SDL_GPUBuffer> m_vertices = nullptr;
    Gpu<SDL_GPUBuffer> m_indices = nullptr;
    Gpu<SDL_GPUBuffer> m_layers = nullptr;
    Gpu<SDL_GPUBuffer> m_parts = nullptr;
    Gpu<SDL_GPUBuffer> m_sprites = nullptr;
    Gpu<SDL_GPUTransferBuffer> m_vertexTransfer = nullptr;
    Gpu<SDL_GPUTransferBuffer> m_layerTransfer = nullptr;
    Gpu<SDL_GPUTransferBuffer> m_partTransfer = nullptr;
    Gpu<SDL_GPUTransferBuffer> m_spriteTransfer = nullptr;
    u32 m_partBytes = 0;   // what m_parts and its transfer buffer hold
    u32 m_spriteBytes = 0; // likewise m_sprites
    bool m_indicesUploaded = false;
    // The pack's versions: the latest (SetPack()), the one on the GPU, and one copied but not yet submitted.
    u64 m_packVersion = 1;
    u64 m_packHeld = 0;
    std::optional<u64> m_packRecorded = std::nullopt;
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    SealPack m_pack{};
    std::vector<SealVertex> m_quads{}; // the frame's lists end to end, as uploaded
    // NOLINTEND(readability-redundant-member-init)
    Run m_groundWash{};
    Run m_groundLines{};
    Run m_aboveWash{};
    Run m_aboveLines{};
    f32 m_haloReach = core::shipped::SEAL_HALO_REACH;
    f32 m_haloStrength = core::shipped::SEAL_HALO_STRENGTH;
};

} // namespace gfx::internal
