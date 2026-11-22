#pragma once

/// @file
/// @ingroup gfx
/// The world map's lines drawn as ribbons, the rivers and the roads, one renderer each: the painted bands laid out once for a
/// map and a look (gfx::DrawRivers(), gfx::DrawRoads()), drawn premultiplied over the ground in world units, the edge band's
/// rim fading out softly and every side anti-aliased, and shaded by the fog as land is.

#include "core/error.hpp"
#include "core/types.hpp"
#include "gfx/camera.hpp"
#include "gfx/internal/features/ribbons.hpp"
#include "gfx/internal/features/sprites.hpp"
#include "gfx/internal/gpu.hpp"
#include "gfx/ribbon.hpp"

#include <SDL3/SDL.h>

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gfx::internal {

/// A ribbon pipeline for one kind of line (premultiplied over: ONE, ONE_MINUS_SRC_ALPHA) and its buffers.
class LineRibbonRenderer {
public:
    /// Creates the pipeline for colour targets of `format`, a vertex buffer of `maxQuads` and the index buffer, all named
    /// by `name` ("river", "road") in graphics debuggers and errors, or says why it couldn't.
    [[nodiscard]] static std::expected<LineRibbonRenderer, core::Error> Create(
        SDL_GPUDevice* device,
        SDL_GPUTextureFormat format,
        u32 maxQuads,
        std::string_view name
    );

    /// Has `lay` lay the lines out and copies them in `copy`, unless the buffer already holds those laid out for `version`
    /// (a count that changes whenever what `lay` reads does). `lay` replaces the vertices it is given with every line's
    /// edge band, then every line's core band, each band of a line as many quads, no more than `maxQuads` in all, and
    /// returns where the edge band's rim starts fading (gfx::RimStart()). Fails when a transfer buffer can't be mapped;
    /// then nothing draws until a later Update() succeeds.
    template<typename Lay>
    [[nodiscard]] std::expected<void, core::Error> Update(SDL_GPUCopyPass* copy, u64 version, const Lay& lay) {
        m_ready = m_held == version;
        if (m_ready) {
            return {};
        }
        const f32 rimStart = lay(m_laid);
        return Upload(copy, version, rimStart);
    }

    /// The command buffer holding the last Update()'s copy was submitted, or failed to be, or was cancelled: a copy counts
    /// as held only once submitted, so a dropped one is made again.
    void Submitted(bool submitted);

    /// Draws the lines in `pass`, every edge band and then every core, placed by `worldToClip` and shaded by `fog` as land
    /// is. Nothing when none is ready.
    void Draw(
        SDL_GPUCommandBuffer* commands,
        SDL_GPURenderPass* pass,
        const ClipTransform& worldToClip,
        const FogTexture& fog
    ) const;

private:
    /// Copies the layout Update() just made for `version` in `copy`, its rim starting at `rimStart`.
    [[nodiscard]] std::expected<void, core::Error> Upload(SDL_GPUCopyPass* copy, u64 version, f32 rimStart);

    SDL_GPUDevice* m_device = nullptr;
    std::string m_name;
    u32 m_maxQuads = 0;
    Gpu<SDL_GPUGraphicsPipeline> m_pipeline = nullptr;
    RibbonBuffers m_buffers{};
    std::vector<RibbonVertex> m_laid;             // the last layout, kept for its capacity
    std::optional<u64> m_held = std::nullopt;     // the version the buffers hold, once submitted
    std::optional<u64> m_recorded = std::nullopt; // the version copied in the command buffer not yet submitted
    bool m_indicesHeld = false;
    bool m_indicesRecorded = false;
    u32 m_quads = 0;       // in the layout the buffers hold or are being given, both bands
    f32 m_rimStart = 1.0F; // RimStart() of that layout's shape
    bool m_ready = false;  // whether this frame's Update() left lines to draw
};

} // namespace gfx::internal
