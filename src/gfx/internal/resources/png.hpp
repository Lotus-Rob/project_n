#pragma once

/// @file
/// @ingroup gfx
/// PNG decoding through core SDL, shared by the sprite array and `gfx::LoadPng`.

#include "core/error.hpp"
#include "gfx/internal/gpu.hpp"

#include <expected>
#include <filesystem>

namespace gfx::internal {

/// Decodes `file` into a surface in SDL_PIXELFORMAT_RGBA32: bytes R, G, B, A on every platform.
/// Fails, naming the file and SDL's cause, when it is missing or not a PNG SDL can read.
[[nodiscard]] std::expected<Surface, core::Error> DecodePng(const std::filesystem::path& file);

} // namespace gfx::internal
