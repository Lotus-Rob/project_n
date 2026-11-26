#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/names.hpp"
#include "core/strings.hpp"
#include "data/names.hpp"
#include "data/strings.hpp"
#include "platform/paths.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <doctest/doctest.h>

namespace {

constexpr f32 POINT_SIZE = 24.0F;
constexpr u32 LAST_CONTROL = 0x1FU;
constexpr u32 DELETE_CONTROL = 0x7FU;
constexpr u32 FIRST_C1_CONTROL = 0x80U;
constexpr u32 LAST_C1_CONTROL = 0x9FU;

struct FontCloser {
    void operator()(TTF_Font* font) const {
        TTF_CloseFont(font);
    }
};

bool IsControl(u32 character) {
    return character <= LAST_CONTROL || character == DELETE_CONTROL ||
           (character >= FIRST_C1_CONTROL && character <= LAST_C1_CONTROL);
}

/// The codepoints of `text` that `font` has no glyph for, each once, in order of first use. SDL's decoder is the one the
/// text layout warns with.
std::vector<u32> Missing(TTF_Font* font, std::string_view text) {
    std::vector<u32> missing;
    const char* next = text.data();
    std::size_t left = text.size();
    while (left > 0) {
        const u32 character = SDL_StepUTF8(&next, &left);
        if (!IsControl(character) && !TTF_FontHasGlyph(font, character) && !std::ranges::contains(missing, character)) {
            missing.push_back(character);
        }
    }
    return missing;
}

/// Checks `text` against `font`, naming `where` for each codepoint it lacks.
void CheckCovered(TTF_Font* font, std::string_view text, const std::string& where) {
    for (const u32 character : Missing(font, text)) {
        CHECK_MESSAGE(false, std::format("the font has no U+{:04X} (in {}: \"{}\")", character, where, text));
    }
}

TEST_CASE("every codepoint of the shipped strings and names is in the font") {
    REQUIRE(TTF_Init());
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    const std::string why = assets.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(assets, why);
    const std::string file = (*assets / "fonts" / "NotoSansSC-Regular.otf").string();
    const std::expected<core::Strings, core::Error> strings = data::LoadStrings(*assets / "data");
    const std::string stringsWhy = strings.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(strings, stringsWhy);
    const std::expected<core::Names, core::Error> names = data::LoadNames(*assets / "data" / "names.yaml");
    const std::string namesWhy = names.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(names, namesWhy);
    {
        const std::unique_ptr<TTF_Font, FontCloser> font{ TTF_OpenFont(file.c_str(), POINT_SIZE) };
        const std::string error = SDL_GetError();
        REQUIRE_MESSAGE(font, error);
        for (std::size_t id = 0; id < strings->text.size(); ++id) {
            const std::string key =
                id < core::STRINGS ? std::string{ core::STRING_NAMES.at(id) } : std::format("data string {}", id);
            for (std::size_t language = 0; language < core::LANGUAGES; ++language) {
                CheckCovered(
                    font.get(),
                    strings->text.at(id).at(language),
                    std::format("{} [{}]", key, core::LANGUAGE_CODES.at(language))
                );
            }
        }
        for (const core::NameEntry& entry : names->surnames) {
            CheckCovered(font.get(), entry.characters, "the surnames");
            CheckCovered(font.get(), entry.pinyin, "the surnames' pinyin");
        }
        for (const core::NameEntry& entry : names->given) {
            CheckCovered(font.get(), entry.characters, "the given names");
            CheckCovered(font.get(), entry.pinyin, "the given names' pinyin");
        }
    }
    TTF_Quit();
}

} // namespace
