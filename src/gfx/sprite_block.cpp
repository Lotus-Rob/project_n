#include "gfx/sprite_block.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace gfx {

namespace {

// Half the gutter the packer leaves, 2^(mips - 1) texels shared by two neighbours: each fills its own half.
constexpr u32 EDGE_TEXELS = 1U << (core::atlas::MIP_LEVELS - 2);

} // namespace

SpriteBlock BlockOf(const core::atlas::Sprite& sprite) {
    return { .x = sprite.x - EDGE_TEXELS,
             .y = sprite.y - EDGE_TEXELS,
             .width = sprite.width + (2 * EDGE_TEXELS),
             .height = sprite.height + (2 * EDGE_TEXELS) };
}

void WriteBlock(
    const core::atlas::Sprite& sprite,
    std::span<const std::byte> pixels,
    std::size_t pitch,
    std::span<std::byte> destination
) {
    const std::size_t edge = EDGE_TEXELS * BYTES_PER_TEXEL;
    const std::size_t row = sprite.width * BYTES_PER_TEXEL;
    const std::size_t blockRow = row + (2 * edge);
    const std::size_t lastLine = sprite.height - std::size_t{ 1 };
    for (std::size_t line = 0; line < lastLine + 1 + (2 * std::size_t{ EDGE_TEXELS }); ++line) {
        const std::size_t sourceLine = line < EDGE_TEXELS ? 0 : std::min(line - EDGE_TEXELS, lastLine);
        const std::span<const std::byte> source = pixels.subspan(sourceLine * pitch, row);
        const std::span<std::byte> target = destination.subspan(line * blockRow, blockRow);
        for (std::size_t offset = 0; offset < edge; offset += BYTES_PER_TEXEL) {
            std::ranges::copy(source.first(BYTES_PER_TEXEL), target.subspan(offset).begin());
            std::ranges::copy(source.last(BYTES_PER_TEXEL), target.subspan(edge + row + offset).begin());
        }
        std::ranges::copy(source, target.subspan(edge).begin());
    }
}

} // namespace gfx
