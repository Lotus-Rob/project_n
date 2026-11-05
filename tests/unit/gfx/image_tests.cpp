#include "core/cxx23.hpp" // IWYU pragma: keep
#include "gfx/image.hpp"

#include <doctest/doctest.h>

namespace {

// Two pixels wide and one high; only the second's red varies.
gfx::Image Pair(u8 secondRed) {
    constexpr std::byte DARK{ 40 };
    constexpr std::byte OPAQUE{ 255 };
    return { .width = 2, .height = 1, .rgba = { DARK, DARK, DARK, OPAQUE, std::byte{ secondRed }, DARK, DARK, OPAQUE } };
}

constexpr u8 RED = 100;
constexpr u8 TOLERANCE = 2;

TEST_CASE("pictures within the tolerance don't differ") {
    CHECK(gfx::DifferingPixels(Pair(RED), Pair(RED), 0) == 0);
    CHECK(gfx::DifferingPixels(Pair(RED), Pair(RED + TOLERANCE), TOLERANCE) == 0);
    CHECK(gfx::DifferingPixels(Pair(RED + TOLERANCE), Pair(RED), TOLERANCE) == 0);
}

TEST_CASE("a pixel past the tolerance differs, in either direction") {
    CHECK(gfx::DifferingPixels(Pair(RED), Pair(RED + TOLERANCE + 1), TOLERANCE) == 1);
    CHECK(gfx::DifferingPixels(Pair(RED + TOLERANCE + 1), Pair(RED), TOLERANCE) == 1);
}

TEST_CASE("pictures of different sizes differ in every pixel") {
    const gfx::Image tall{ .width = 1, .height = 2, .rgba = Pair(RED).rgba };
    CHECK(gfx::DifferingPixels(Pair(RED), tall, TOLERANCE) == 2);
}

TEST_CASE("a PNG reads back as the picture saved") {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "project_n_image_tests.png";
    const gfx::Image saved = Pair(RED);
    REQUIRE(gfx::SavePng(saved, file).has_value());
    const std::expected<gfx::Image, core::Error> loaded = gfx::LoadPng(file);
    std::filesystem::remove(file);
    REQUIRE(loaded.has_value());
    CHECK(loaded->width == saved.width);
    CHECK(loaded->height == saved.height);
    CHECK(gfx::DifferingPixels(saved, *loaded, 0) == 0);
}

TEST_CASE("a missing PNG is an error") {
    CHECK_FALSE(gfx::LoadPng("no such picture.png").has_value());
}

} // namespace
