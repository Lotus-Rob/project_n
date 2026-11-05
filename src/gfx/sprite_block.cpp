#include "gfx/sprite_block.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace gfx {

SpriteBlock BlockOf(const core::atlas::Sprite& sprite) {
    return { .x = sprite.x, .y = sprite.y, .width = sprite.width, .height = sprite.height };
}

void WriteBlock(
    const core::atlas::Sprite& sprite,
    std::span<const std::byte> pixels,
    std::size_t pitch,
    std::span<std::byte> destination
) {
    const std::size_t row = sprite.width * BYTES_PER_TEXEL;
    for (std::size_t line = 0; line < sprite.height; ++line) {
        std::ranges::copy(pixels.subspan(line * pitch, row), destination.subspan(line * row).begin());
    }
}

} // namespace gfx
