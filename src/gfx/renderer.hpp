#pragma once

/// @file
/// @ingroup gfx
/// Drawing frames into the window with SDL_GPU.

#include "core/error.hpp"
#include "core/frame.hpp"
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
/// swapchain, the pipelines and the sprite atlas are all made when it's created.
class Renderer {
public:
    /// Creates a GPU device for `window` and presents to it, with the sprite atlas loaded from
    /// `assets`, or says why it couldn't (SDL doesn't guarantee an sRGB swapchain). The window must
    /// outlive the renderer.
    [[nodiscard]] static std::expected<Renderer, core::Error> Create(
        platform::Window& window,
        const std::filesystem::path& assets
    );

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    /// Takes over `other`'s device, leaving `other` empty.
    Renderer(Renderer&& other) noexcept;
    Renderer& operator=(Renderer&& other) = delete;
    ~Renderer();

    /// Acquires the window's next frame to draw into: waits while the GPU has its two frames in
    /// flight and, with vsync, for the display. The time for alpha is read after it (§6); Draw()
    /// follows before the next Acquire(). A failure is logged, and the Draw() after it does nothing.
    void Acquire();
    /// Draws `snapshot` into the frame Acquire() acquired as `camera` sees it, interpolated by
    /// `alpha` (0: its previous tick, 1: its own), and presents it. Uploads its instances only when
    /// it isn't the snapshot drawn last (§8.3). The instance at index `outlined`, when given, gets a pale
    /// outline drawn over everything (the hovered entity, design doc 0002.3 §3).
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
