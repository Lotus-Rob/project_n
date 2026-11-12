#include "gfx/sprite_block.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace gfx {

SpriteBlock BlockOf(const core::SpriteInfo& sprite, u32 edge) {
    return {
        .x = sprite.x - edge, .y = sprite.y - edge, .width = sprite.width + (2 * edge), .height = sprite.height + (2 * edge)
    };
}

void WriteBlock(
    const core::SpriteInfo& sprite,
    u32 edge,
    std::span<const std::byte> pixels,
    std::size_t pitch,
    std::span<std::byte> destination
) {
    const std::size_t edgeBytes = std::size_t{ edge } * BYTES_PER_TEXEL;
    const std::size_t row = sprite.width * BYTES_PER_TEXEL;
    const std::size_t blockRow = row + (2 * edgeBytes);
    const std::size_t lastLine = sprite.height - std::size_t{ 1 };
    for (std::size_t line = 0; line < lastLine + 1 + (2 * std::size_t{ edge }); ++line) {
        const std::size_t sourceLine = line < edge ? 0 : std::min(line - edge, lastLine);
        const std::span<const std::byte> source = pixels.subspan(sourceLine * pitch, row);
        const std::span<std::byte> target = destination.subspan(line * blockRow, blockRow);
        for (std::size_t offset = 0; offset < edgeBytes; offset += BYTES_PER_TEXEL) {
            std::ranges::copy(source.first(BYTES_PER_TEXEL), target.subspan(offset).begin());
            std::ranges::copy(source.last(BYTES_PER_TEXEL), target.subspan(edgeBytes + row + offset).begin());
        }
        std::ranges::copy(source, target.subspan(edgeBytes).begin());
    }
}

} // namespace gfx
