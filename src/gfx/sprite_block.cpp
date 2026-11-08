#include "gfx/sprite_block.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

namespace gfx {

namespace {

// Half the gutter the packer leaves, 2^(mips - 1) texels shared by two neighbours: each fills its own half.
u32 EdgeTexels(u32 mipLevels) {
    PN_ASSERT(mipLevels >= 2, "a sprite's edge needs two mip levels or more");
    return 1U << (mipLevels - 2);
}

} // namespace

SpriteBlock BlockOf(const core::SpriteInfo& sprite, u32 mipLevels) {
    const u32 edge = EdgeTexels(mipLevels);
    return {
        .x = sprite.x - edge, .y = sprite.y - edge, .width = sprite.width + (2 * edge), .height = sprite.height + (2 * edge)
    };
}

void WriteBlock(
    const core::SpriteInfo& sprite,
    u32 mipLevels,
    std::span<const std::byte> pixels,
    std::size_t pitch,
    std::span<std::byte> destination
) {
    const u32 edgeTexels = EdgeTexels(mipLevels);
    const std::size_t edge = edgeTexels * BYTES_PER_TEXEL;
    const std::size_t row = sprite.width * BYTES_PER_TEXEL;
    const std::size_t blockRow = row + (2 * edge);
    const std::size_t lastLine = sprite.height - std::size_t{ 1 };
    for (std::size_t line = 0; line < lastLine + 1 + (2 * std::size_t{ edgeTexels }); ++line) {
        const std::size_t sourceLine = line < edgeTexels ? 0 : std::min(line - edgeTexels, lastLine);
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
