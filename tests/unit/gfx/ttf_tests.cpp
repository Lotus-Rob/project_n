#include "core/cxx23.hpp" // IWYU pragma: keep
#include "platform/paths.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <doctest/doctest.h>

namespace {

constexpr f32 POINT_SIZE = 24.0F;
constexpr u32 XIU = 0x4FEEU; // 修, in the cultivator's name label (design doc 0002.4 §2)
constexpr u32 LATIN_A = 0x41U;

struct FontCloser {
    void operator()(TTF_Font* font) const {
        TTF_CloseFont(font);
    }
};

// SDL3_ttf as built for the game (design doc 0002.4 §3).
TEST_CASE("SDL3_ttf links HarfBuzz") {
    REQUIRE(TTF_Init());
    i32 major = 0;
    i32 minor = 0;
    i32 patch = 0;
    TTF_GetHarfBuzzVersion(&major, &minor, &patch);
    CHECK(major > 0);
    TTF_Quit();
}

TEST_CASE("SDL3_ttf opens the game's font with its Chinese and Latin glyphs") {
    REQUIRE(TTF_Init());
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    const std::string why = assets.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(assets, why);
    const std::string file = (*assets / "fonts" / "NotoSansSC-Regular.otf").string();
    {
        const std::unique_ptr<TTF_Font, FontCloser> font{ TTF_OpenFont(file.c_str(), POINT_SIZE) };
        const std::string error = SDL_GetError();
        REQUIRE_MESSAGE(font, error);
        CHECK(TTF_FontHasGlyph(font.get(), XIU));
        CHECK(TTF_FontHasGlyph(font.get(), LATIN_A));
    }
    TTF_Quit();
}

} // namespace
