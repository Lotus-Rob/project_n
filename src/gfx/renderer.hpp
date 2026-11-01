#pragma once

/// @file
/// @ingroup gfx
/// Drawing frames into the window with SDL_GPU.

#include "core/error.hpp"
#include "core/types.hpp"
#include "platform/window.hpp"

#include <expected>
#include <memory>

namespace gfx {

/// An opaque colour in the window's colour space.
struct Colour {
    /// The red channel, from 0 (none) to 1 (full).
    f32 red = 0.0F;
    /// The green channel, from 0 (none) to 1 (full).
    f32 green = 0.0F;
    /// The blue channel, from 0 (none) to 1 (full).
    f32 blue = 0.0F;
};

/// Draws into the game's window with SDL_GPU (design doc 0002 §8). The device, the window's
/// swapchain and the pipelines are all made when it's created.
class Renderer {
public:
    /// Creates a GPU device for `window` and presents to it, or says why it couldn't. The window
    /// must outlive the renderer.
    [[nodiscard]] static std::expected<Renderer, core::Error> Create(platform::Window& window);

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    /// Takes over `other`'s device, leaving `other` empty.
    Renderer(Renderer&& other) noexcept;
    Renderer& operator=(Renderer&& other) = delete;
    ~Renderer();

    /// Draws a frame cleared to `colour` and presents it. Waits while the GPU has its two frames in
    /// flight and, when presenting with vsync, for the display.
    void Draw(Colour colour);

private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace gfx
