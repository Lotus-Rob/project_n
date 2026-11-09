#pragma once

/// @file
/// @ingroup gfx
/// One frame's command buffer and swapchain texture (design doc 0002 §8.3).

#include "core/error.hpp"
#include "core/types.hpp"

#include <SDL3/SDL.h>

#include <expected>

namespace gfx::internal {

/// A frame being recorded: its command buffer, and the swapchain texture it draws into.
struct Frame {
    /// The frame's commands; submitted by SubmitFrame().
    SDL_GPUCommandBuffer* commands = nullptr;
    /// The window's texture to draw into, or null when the window can't be shown: then nothing is
    /// drawn, but copies still run and the command buffer is still submitted (§8.3).
    SDL_GPUTexture* swapchain = nullptr;
    /// The swapchain texture's width in pixels.
    u32 width = 0;
    /// Its height in pixels.
    u32 height = 0;
};

/// Acquires a command buffer and the window's swapchain texture, waiting while the GPU has its
/// frames in flight (and, with vsync, for the display).
[[nodiscard]] std::expected<Frame, core::Error> AcquireFrame(SDL_GPUDevice* device, SDL_Window* window);

/// Submits `frame`'s commands; presentation follows.
[[nodiscard]] std::expected<void, core::Error> SubmitFrame(const Frame& frame);
} // namespace gfx::internal
