#include "core/cxx23.hpp" // IWYU pragma: keep
#include "gfx/distance_field.hpp"

#include "core/manifest.hpp"
#include "gfx/sprite_block.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 PAD = core::FIELD_REACH_TEXELS;
constexpr std::size_t ALPHA_OFFSET = 3;
constexpr u8 SOLID = 255;
constexpr u8 CLEAR = 0;
constexpr u8 JUST_OUTSIDE = 127;
constexpr u8 JUST_INSIDE = 128;
constexpr f32 HALF = 0.5F;
constexpr f32 ONE_AND_HALF = 1.5F;
constexpr f32 REACH = static_cast<f32>(core::FIELD_REACH_TEXELS);
constexpr f32 BEYOND_REACH = 40.0F;
constexpr f32 ROOT_TWO = std::numbers::sqrt2_v<f32>;
constexpr u32 SQUARE_SIZE = 4;
constexpr u32 RING_SIZE = 5;
constexpr u32 DIAGONAL_SIZE = 6;
constexpr u32 WIDE = 40;
constexpr u32 FAR_COLUMN = 20;
constexpr std::size_t SPACED_PITCH = 8;
constexpr std::size_t SPACED_BYTES = 16;
constexpr u8 ENCODED_EDGE = 128;
constexpr u8 ENCODED_ROUNDED = 139; // 255 x 0.546875 = 139.45

// A distance field's block of bytes and its image's width.
struct Field {
    std::vector<u8> bytes;
    u32 width;
    // The byte at the image's texel (column, row).
    [[nodiscard]] u8 At(u32 column, u32 row) const {
        return bytes[((row + PAD) * (width + (2 * PAD))) + column + PAD];
    }
};

// The field of an image of `width` x `height` texels whose alpha is `alpha(column, row)`.
template<typename Alpha>
Field FieldOf(u32 width, u32 height, Alpha alpha) {
    const std::size_t pitch = static_cast<std::size_t>(width) * gfx::BYTES_PER_TEXEL;
    std::vector<std::byte> image(pitch * height);
    for (u32 row = 0; row < height; ++row) {
        for (u32 column = 0; column < width; ++column) {
            image[(row * pitch) + (column * gfx::BYTES_PER_TEXEL) + ALPHA_OFFSET] = static_cast<std::byte>(alpha(column, row));
        }
    }
    return { .bytes = gfx::DistanceField(image, pitch, width, height), .width = width };
}

constexpr auto OPAQUE = [](u32, u32) { return SOLID; };

TEST_CASE("distance_field: the edge encodes as the middle byte") {
    CHECK(gfx::EncodeDistance(0.0F) == ENCODED_EDGE);
}

TEST_CASE("distance_field: the inside reach encodes as zero") {
    CHECK(gfx::EncodeDistance(-REACH) == 0);
}

TEST_CASE("distance_field: the outside reach encodes as full") {
    CHECK(gfx::EncodeDistance(REACH) == SOLID);
}

TEST_CASE("distance_field: beyond the reach clamps") {
    CHECK(gfx::EncodeDistance(BEYOND_REACH) == SOLID);
    CHECK(gfx::EncodeDistance(-BEYOND_REACH) == 0);
}

TEST_CASE("distance_field: a distance rounds to the nearest byte") {
    CHECK(gfx::EncodeDistance(ONE_AND_HALF) == ENCODED_ROUNDED);
}

TEST_CASE("distance_field: the block is the image and the reach all round") {
    const Field field = FieldOf(3, 2, OPAQUE);
    CHECK(field.bytes.size() == static_cast<std::size_t>(3 + (2 * PAD)) * (2 + (2 * PAD)));
}

TEST_CASE("distance_field: a lone inside texel is half a texel inside") {
    const Field field = FieldOf(1, 1, OPAQUE);
    CHECK(field.At(0, 0) == gfx::EncodeDistance(-HALF));
}

TEST_CASE("distance_field: the neighbour of a lone texel is half a texel outside") {
    const Field field = FieldOf(1, 1, OPAQUE);
    CHECK(field.At(1, 0) == gfx::EncodeDistance(HALF));
}

TEST_CASE("distance_field: two texels away is one and a half outside") {
    const Field field = FieldOf(1, 1, OPAQUE);
    CHECK(field.At(2, 0) == gfx::EncodeDistance(ONE_AND_HALF));
}

TEST_CASE("distance_field: a diagonal neighbour is measured straight") {
    const Field field = FieldOf(1, 1, OPAQUE);
    CHECK(field.At(1, 1) == gfx::EncodeDistance(ROOT_TWO - HALF));
}

TEST_CASE("distance_field: a texel 20 away from the only inside texel is full") {
    const Field field = FieldOf(WIDE, 1, [](u32 column, u32) { return column == 0 ? SOLID : CLEAR; });
    CHECK(field.At(FAR_COLUMN, 0) == SOLID);
}

TEST_CASE("distance_field: the centre of a square is deep inside") {
    const Field field = FieldOf(SQUARE_SIZE, SQUARE_SIZE, OPAQUE);
    CHECK(field.At(1, 1) == gfx::EncodeDistance(-ONE_AND_HALF));
    CHECK(field.At(2, 2) == gfx::EncodeDistance(-ONE_AND_HALF));
}

TEST_CASE("distance_field: the edge of a square is half a texel inside") {
    const Field field = FieldOf(SQUARE_SIZE, SQUARE_SIZE, OPAQUE);
    CHECK(field.At(0, 0) == gfx::EncodeDistance(-HALF));
    CHECK(field.At(3, 1) == gfx::EncodeDistance(-HALF));
}

TEST_CASE("distance_field: a hole in a ring is half a texel outside") {
    const Field field =
        FieldOf(RING_SIZE, RING_SIZE, [](u32 column, u32 row) { return (column == 2 && row == 2) ? CLEAR : SOLID; });
    CHECK(field.At(2, 2) == gfx::EncodeDistance(HALF));
}

TEST_CASE("distance_field: a transparent image is full everywhere") {
    const Field field = FieldOf(3, 3, [](u32, u32) { return CLEAR; });
    CHECK(field.At(0, 0) == SOLID);
    CHECK(field.At(1, 2) == SOLID);
    CHECK(field.bytes.front() == SOLID);
}

TEST_CASE("distance_field: a diagonal edge is measured to the nearest texel centre") {
    // Inside where column >= row: the outside texel (1, 2) is one texel from the inside (2, 2) and (1, 1).
    const Field field =
        FieldOf(DIAGONAL_SIZE, DIAGONAL_SIZE, [](u32 column, u32 row) { return column >= row ? SOLID : CLEAR; });
    CHECK(field.At(1, 2) == gfx::EncodeDistance(HALF));
}

TEST_CASE("distance_field: alpha 127 is outside") {
    const Field field = FieldOf(1, 1, [](u32, u32) { return JUST_OUTSIDE; });
    CHECK(field.At(0, 0) == SOLID);
}

TEST_CASE("distance_field: alpha 128 is inside") {
    const Field field = FieldOf(1, 1, [](u32, u32) { return JUST_INSIDE; });
    CHECK(field.At(0, 0) == gfx::EncodeDistance(-HALF));
}

TEST_CASE("distance_field: rows are read pitch bytes apart") {
    // A 1x2 image whose rows are 8 bytes apart: only the first row is inside.
    std::vector<std::byte> image(SPACED_BYTES);
    image[SPACED_PITCH - 1] = std::byte{ SOLID }; // in the gap after row 0: rows read tightly would take it for row 1's alpha
    image[ALPHA_OFFSET] = std::byte{ SOLID };
    const Field field{ .bytes = gfx::DistanceField(image, SPACED_PITCH, 1, 2), .width = 1 };
    CHECK(field.At(0, 0) == gfx::EncodeDistance(-HALF));
    CHECK(field.At(0, 1) == gfx::EncodeDistance(HALF));
}

} // namespace
