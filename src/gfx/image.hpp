#pragma once

/// @file
/// @ingroup gfx
/// Pictures in memory and on disk, for reading what the renderer drew and comparing it with a
/// committed reference (design doc 0002 §8.9).

#include "core/error.hpp"
#include "core/types.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <vector>

namespace gfx {

/// A picture in rows from the top, four bytes a pixel in the order R, G, B, A, sRGB-encoded.
struct Image {
    /// Its width in pixels.
    u32 width = 0;
    /// Its height in pixels.
    u32 height = 0;
    /// `width` × `height` pixels, without gaps between rows.
    std::vector<std::byte> rgba{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
};

/// Reads the PNG at `file`, or says why it couldn't.
[[nodiscard]] std::expected<Image, core::Error> LoadPng(const std::filesystem::path& file);

/// Writes `image` to `file` as a PNG, or says why it couldn't.
[[nodiscard]] std::expected<void, core::Error> SavePng(const Image& image, const std::filesystem::path& file);

/// How many pixels have a channel more than `tolerance` levels away from the other picture's.
/// Pictures of different sizes differ in every pixel of the first.
[[nodiscard]] std::size_t DifferingPixels(const Image& first, const Image& second, u8 tolerance);

} // namespace gfx
