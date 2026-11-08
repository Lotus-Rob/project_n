#include "core/cxx23.hpp" // IWYU pragma: keep
#include "gfx/sprite_block.hpp"

#include <doctest/doctest.h>

namespace {

using Colour = std::array<std::byte, gfx::BYTES_PER_TEXEL>;

constexpr std::size_t PAGE_WIDTH = 64;
constexpr std::size_t PAGE_HEIGHT = 40;
// Level 3 of the mips: each of its texels averages 8x8 page texels. It is the deepest level whose
// texels across a sprite's edge fit in a sprite's half of the 16-texel gutter.
constexpr std::size_t LEVEL_3_TEXEL = 8;
constexpr std::size_t SPRITE_SIZE = 8;
constexpr u32 MIP_LEVELS = 5; // the 16-texel gutter's

// Writes a one-colour image of `sprite` onto the page, the way the atlas does.
void Place(const core::SpriteInfo& sprite, const Colour& colour, std::span<std::byte> page) {
    std::vector<std::byte> image;
    for (std::size_t texel = 0; texel < SPRITE_SIZE * SPRITE_SIZE; ++texel) {
        image.insert(image.end(), colour.begin(), colour.end());
    }
    const gfx::SpriteBlock block = gfx::BlockOf(sprite, MIP_LEVELS);
    const std::size_t row = block.width * gfx::BYTES_PER_TEXEL;
    std::vector<std::byte> written(row * block.height);
    gfx::WriteBlock(sprite, MIP_LEVELS, image, SPRITE_SIZE * gfx::BYTES_PER_TEXEL, written);
    for (std::size_t line = 0; line < block.height; ++line) {
        const std::size_t start = (((block.y + line) * PAGE_WIDTH) + block.x) * gfx::BYTES_PER_TEXEL;
        std::ranges::copy(std::span{ written }.subspan(line * row, row), page.subspan(start).begin());
    }
}

// The level 3 texel at the page position (left, top), as "r,g,b,a".
std::string Level3Texel(std::span<const std::byte> page, std::size_t left, std::size_t top) {
    std::array<u32, gfx::BYTES_PER_TEXEL> sum{};
    for (std::size_t line = top; line < top + LEVEL_3_TEXEL; ++line) {
        for (std::size_t column = left; column < left + LEVEL_3_TEXEL; ++column) {
            for (std::size_t channel = 0; channel < sum.size(); ++channel) {
                const std::size_t texel = (line * PAGE_WIDTH) + column;
                sum.at(channel) += std::to_integer<u32>(page[(texel * gfx::BYTES_PER_TEXEL) + channel]);
            }
        }
    }
    constexpr u32 TEXELS = LEVEL_3_TEXEL * LEVEL_3_TEXEL;
    return std::format("{},{},{},{}", sum[0] / TEXELS, sum[1] / TEXELS, sum[2] / TEXELS, sum[3] / TEXELS);
}

TEST_CASE("opaque neighbours stay opaque and their own colour across their edges at the low mips") {
    // Laid out as the packer does: 16 texels at the page's edges and between the two sprites.
    const core::SpriteInfo leftSprite{
        .file = "left.png", .page = 0, .x = 16, .y = 16, .width = 8, .height = 8, .footX = 0, .footY = 0
    };
    const core::SpriteInfo rightSprite{
        .file = "right.png", .page = 0, .x = 40, .y = 16, .width = 8, .height = 8, .footX = 0, .footY = 0
    };
    constexpr Colour LEFT_COLOUR{ { std::byte{ 200 }, std::byte{ 100 }, std::byte{ 50 }, std::byte{ 255 } } };
    constexpr Colour RIGHT_COLOUR{ { std::byte{ 20 }, std::byte{ 40 }, std::byte{ 60 }, std::byte{ 255 } } };
    constexpr std::size_t GUTTER_MIDDLE = 32;
    constexpr std::size_t FIRST = 8;
    constexpr std::size_t PAST_LAST_COLUMN = 56;
    constexpr std::size_t PAST_LAST_LINE = 32;

    std::vector<std::byte> page(PAGE_WIDTH * PAGE_HEIGHT * gfx::BYTES_PER_TEXEL); // transparent black
    Place(leftSprite, LEFT_COLOUR, page);
    Place(rightSprite, RIGHT_COLOUR, page);

    // A sampler at a sprite's edge blends the level 3 texels on either side of that edge.
    for (std::size_t top = FIRST; top < PAST_LAST_LINE; top += LEVEL_3_TEXEL) {
        for (std::size_t left = FIRST; left < PAST_LAST_COLUMN; left += LEVEL_3_TEXEL) {
            CAPTURE(left);
            CAPTURE(top);
            const std::string expected = left < GUTTER_MIDDLE ? "200,100,50,255" : "20,40,60,255";
            const std::string texel = Level3Texel(page, left, top);
            CHECK(texel == expected);
        }
    }
}

} // namespace
