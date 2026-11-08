#pragma once

/// @file
/// @ingroup gfx
/// Drawing frames into the window with SDL_GPU.

#include "core/error.hpp"
#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/manifest.hpp"
#include "core/types.hpp"
#include "gfx/camera.hpp"
#include "gfx/image.hpp"
#include "platform/window.hpp"

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>

namespace gfx {

/// Draws into the game's window with SDL_GPU (design doc 0002 §8), blending in linear space (§8.4). The device, the window's
/// swapchain, the pipelines and the empty sprite atlas are made when it's created; each scene's sprites load into the atlas
/// when the game needs them (design doc 0002.3.1 §5), and stay.
class Renderer {
public:
    /// Creates a GPU device for `window` and presents to it, with the empty sprite atlas `manifest`
    /// describes, whose scenes load from `assets`, or says why it couldn't (SDL doesn't guarantee an
    /// sRGB swapchain). The window must outlive the renderer.
    [[nodiscard]] static std::expected<Renderer, core::Error> Create(
        platform::Window& window,
        const std::filesystem::path& assets,
        std::shared_ptr<const core::Manifest> manifest
    );

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    /// Takes over `other`'s device, leaving `other` empty.
    Renderer(Renderer&& other) noexcept;
    Renderer& operator=(Renderer&& other) = delete;
    ~Renderer();

    /// Starts loading the sprites of `scene` that aren't in the atlas yet, decoding one per low-priority chunk on `jobs`,
    /// which must outlive the renderer. Does nothing when it already holds `scene`; no other scene may be loading.
    void BeginScene(core::SceneId scene, core::Jobs& jobs);

    /// Goes on with the load BeginScene() started: nothing while the sprites decode; then uploads them one at a time
    /// until `budgetNs` is spent (one at least), and once all are in, generates the mips. True once nothing is loading,
    /// the scene held; an error, ending the load, when a sprite failed to decode (`./dev atlas`) or upload.
    [[nodiscard]] std::expected<bool, core::Error> ContinueScene(u64 budgetNs);

    /// Acquires the window's next frame to draw into: waits while the GPU has its two frames in
    /// flight and, with vsync, for the display. The time for alpha is read after it (§6); Draw()
    /// follows before the next Acquire(). A failure is logged, and the Draw() after it does nothing.
    void Acquire();
    /// Draws `snapshot` into the frame Acquire() acquired as `camera` sees it, interpolated by
    /// `alpha` (0: its previous tick, 1: its own), and presents it. Uploads its instances only when
    /// it isn't the snapshot drawn last (§8.3); a snapshot of a scene it doesn't hold draws the backdrop only.
    /// The instance at index `outlined`, when given, gets a pale
    /// outline drawn over everything (the hovered entity, design doc 0002.3 §3), and the snapshot's
    /// selected instance a gold one; the gold wins where both fall on one.
    void Draw(
        const core::Snapshot& snapshot,
        f64 alpha,
        const Camera& camera,
        std::optional<std::size_t> outlined = std::nullopt
    );

    /// Draws as Draw() does, but into a picture `width` by `height` pixels instead of the window,
    /// waits for the GPU and returns the picture, or says why it couldn't. Stalls: for tests.
    [[nodiscard]] std::expected<Image, core::Error> Capture(
        const core::Snapshot& snapshot,
        f64 alpha,
        const Camera& camera,
        u32 width,
        u32 height,
        std::optional<std::size_t> outlined = std::nullopt
    );

private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace gfx
