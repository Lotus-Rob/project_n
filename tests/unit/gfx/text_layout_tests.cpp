#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/jobs.hpp"
#include "core/strings.hpp"
#include "data/strings.hpp"
#include "gfx/text_layout.hpp"
#include "platform/paths.hpp"

#include <SDL3_ttf/SDL_ttf.h>
#include <doctest/doctest.h>

namespace {

constexpr core::TextDirection ACROSS = core::TextDirection::LeftToRight;
constexpr core::TextDirection DOWN = core::TextDirection::TopToBottom;
constexpr std::array<core::TextDirection, 2> BOTH_WAYS{ { ACROSS, DOWN } };
constexpr u8 FIELD_EDGE = 128; // FreeType's distance fields: the outline, inside at or above it
constexpr std::string_view FONT_FILE = "NotoSansSC-Regular.otf";
// A blocked background job gives up after this, so a glyph batch queued behind it fails the test rather than hanging it.
constexpr std::chrono::seconds PATIENCE{ 10 };

std::filesystem::path Assets() {
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    const std::string why = assets.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(assets, why);
    return *assets;
}

gfx::TextLayout OpenFonts() {
    std::expected<gfx::TextLayout, core::Error> layout = gfx::TextLayout::Open(Assets() / "fonts");
    const std::string error = layout.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(layout, error);
    return std::move(*layout);
}

// A cache whose Pending() holds only `text`'s glyphs: the digits and signs every pre-warm covers have gone up.
gfx::TextLayout WarmedWith(std::string_view text) {
    gfx::TextLayout layout = OpenFonts();
    layout.Prewarm(core::Strings{});
    layout.Uploaded(layout.Pending().size());
    core::Strings strings{};
    strings.text.at(0).at(0) = text;
    layout.Prewarm(strings);
    return layout;
}

// The label stack `stack` (core::StringId::LabelStack's text) filled in with `name` and `count`.
std::string Stacked(std::string stack, std::string_view name, std::string_view count) {
    constexpr std::string_view NAME = "{name}";
    constexpr std::string_view COUNT = "{count}";
    stack.replace(stack.find(NAME), NAME.size(), name);
    stack.replace(stack.find(COUNT), COUNT.size(), count);
    return stack;
}

struct FontCloser {
    void operator()(TTF_Font* font) const {
        TTF_CloseFont(font);
    }
};

// The plain font's measures of one character at the base size, in pixels: its outline's box from the pen and the
// baseline (y up), and the font's ascent.
struct Ink {
    i32 minX = 0;
    i32 maxX = 0;
    i32 minY = 0;
    i32 maxY = 0;
    i32 ascent = 0;
};

Ink InkOf(u32 character) {
    REQUIRE(TTF_Init());
    const std::string file = (Assets() / "fonts" / FONT_FILE).string();
    Ink ink{};
    {
        const std::unique_ptr<TTF_Font, FontCloser> font{ TTF_OpenFont(file.c_str(), gfx::SDF_BASE_PIXELS) };
        REQUIRE(font);
        REQUIRE(TTF_GetGlyphMetrics(font.get(), character, &ink.minX, &ink.maxX, &ink.minY, &ink.maxY, nullptr));
        ink.ascent = TTF_GetFontAscent(font.get());
    }
    TTF_Quit();
    return ink;
}

// The shaping font's line height at the base size, in pixels.
f32 LineHeight() {
    REQUIRE(TTF_Init());
    const std::string file = (Assets() / "fonts" / FONT_FILE).string();
    f32 height = 0.0F;
    {
        const std::unique_ptr<TTF_Font, FontCloser> font{ TTF_OpenFont(file.c_str(), gfx::SDF_BASE_PIXELS) };
        REQUIRE(font);
        height = static_cast<f32>(TTF_GetFontHeight(font.get()));
    }
    TTF_Quit();
    return height;
}

TEST_CASE("a text is laid out once, and each of its glyphs rasterised once") {
    constexpr std::string_view NAME = "Cultivator 修士";
    gfx::TextLayout text = WarmedWith(NAME);
    CHECK(text.Pending().size() == 11); // the two t's are one glyph, and the space draws nothing
    const gfx::Layout& first = text.Lay(NAME, ACROSS);
    CHECK(first.complete);
    CHECK(first.glyphs.size() == 12);
    const gfx::Layout& again = text.Lay(NAME, ACROSS);
    CHECK(&again == &first);
    const gfx::Layout& tot = text.Lay("tot", ACROSS);
    CHECK(tot.complete);
    CHECK(text.Pending().size() == 11);
    CHECK(text.Pages() == 1);
}

TEST_CASE("uploaded glyphs leave the queue, oldest first") {
    gfx::TextLayout text = WarmedWith("ab");
    REQUIRE(text.Pending().size() == 2);
    const u32 second = text.Pending()[1].x;
    text.Uploaded(1);
    REQUIRE(text.Pending().size() == 1);
    CHECK(text.Pending()[0].x == second);
}

TEST_CASE("a glyph's field is rasterised once and is a distance field") {
    constexpr std::string_view XIU = "修";
    gfx::TextLayout text = WarmedWith(XIU);
    REQUIRE(text.Pending().size() == 1);
    const gfx::GlyphUpload& upload = text.Pending()[0];
    REQUIRE(upload.field.size() == std::size_t{ upload.width } * upload.height);
    const auto fieldAt = [&upload](u32 column, u32 row) {
        return upload.field.at((std::size_t{ row } * upload.width) + column);
    };
    bool borderOutside = true; // the spread's margin is all outside the glyph
    for (u32 column = 0; column < upload.width; ++column) {
        borderOutside = borderOutside && fieldAt(column, 0) < FIELD_EDGE && fieldAt(column, upload.height - 1) < FIELD_EDGE;
    }
    for (u32 row = 0; row < upload.height; ++row) {
        borderOutside = borderOutside && fieldAt(0, row) < FIELD_EDGE && fieldAt(upload.width - 1, row) < FIELD_EDGE;
    }
    CHECK(borderOutside);
    const auto inside = std::ranges::count_if(upload.field, [](u8 texel) { return texel >= FIELD_EDGE; });
    const auto near = std::ranges::count_if(upload.field, [](u8 texel) { return texel > 0 && texel < FIELD_EDGE; });
    CHECK(inside > 0);    // the strokes
    CHECK(near > inside); // a field fades out over the spread around every stroke; coverage stops at its edge
    const gfx::Layout& layout = text.Lay(XIU, ACROSS);
    REQUIRE(layout.glyphs.size() == 1);
    const gfx::PlacedGlyph& glyph = layout.glyphs[0];
    CHECK(glyph.width == static_cast<f32>(upload.width)); // a texel a base pixel
    CHECK(glyph.height == static_cast<f32>(upload.height));
    CHECK(glyph.u == upload.x);
    CHECK(glyph.v == upload.y);
    CHECK(text.Pending().size() == 1);
}

TEST_CASE("pre-warm covers every string in both languages and the digits") {
    gfx::TextLayout text = OpenFonts();
    const std::expected<core::Strings, core::Error> strings = data::LoadStrings(Assets() / "data");
    const std::string why = strings.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(strings, why);
    text.Prewarm(*strings);
    const std::size_t warmed = text.Pending().size();
    std::vector<std::string> shown{ "-1234567890+" };
    for (std::size_t language = 0; language < core::LANGUAGES; ++language) {
        const std::string& stack = strings->text.at(std::to_underlying(core::StringId::LabelStack)).at(language);
        for (std::size_t string = 0; string < core::STRINGS; ++string) {
            const std::string& entry = strings->text.at(string).at(language);
            if (string == std::to_underlying(core::StringId::LabelStack)) {
                shown.push_back(Stacked(entry, "", ""));
                continue;
            }
            shown.push_back(entry);
            shown.push_back(Stacked(stack, entry, "1234567890"));
        }
    }
    for (const std::string& entry : shown) {
        for (const core::TextDirection direction : BOTH_WAYS) {
            const bool complete = text.Lay(entry, direction).complete;
            CHECK_MESSAGE(complete, entry);
        }
    }
    CHECK(text.Pending().size() == warmed);
}

TEST_CASE("a glyph not pre-warmed is drawn once the worker has rasterised it") {
    constexpr std::string_view CRANE = "鹤";
    core::Jobs jobs{ 1 };
    gfx::TextLayout text = OpenFonts();
    const gfx::Layout& before = text.Lay(CRANE, ACROSS);
    CHECK_FALSE(before.complete);
    CHECK(before.glyphs.empty());
    CHECK(text.Pending().empty());
    text.EndFrame(jobs); // asks the worker
    text.Flush(jobs);    // waits for it, and takes what it rasterised
    const gfx::Layout& after = text.Lay(CRANE, ACROSS);
    CHECK(after.complete);
    CHECK(after.glyphs.size() == 1);
    CHECK(text.Pending().size() == 1);
}

TEST_CASE("a glyph lands while another background job holds a worker") {
    constexpr std::string_view CRANE = "鹤";
    std::atomic<bool> release{ false };
    std::atomic<bool> gaveUp{ false };
    core::Jobs jobs{ 2 };
    jobs.Background([&release, &gaveUp] { // a save, say
        const auto deadline = std::chrono::steady_clock::now() + PATIENCE;
        while (!release.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        gaveUp.store(!release.load());
    });
    gfx::TextLayout text = OpenFonts();
    static_cast<void>(text.Lay(CRANE, ACROSS));
    text.EndFrame(jobs);
    text.Flush(jobs);
    const bool landed = text.Lay(CRANE, ACROSS).complete;
    const bool landedFirst = !gaveUp.load();
    release.store(true);
    jobs.WaitForBackground();
    CHECK(landed);
    CHECK(landedFirst);
}

TEST_CASE("a cache destroyed while the worker rasterises for it lets the next one draw") {
    constexpr std::string_view BIRDS = "鹤鹰鸽鸦鹊鹭鹳鹦鹉";
    constexpr std::string_view CRANE = "鹤";
    core::Jobs jobs{ 1 };
    {
        gfx::TextLayout text = OpenFonts();
        static_cast<void>(text.Lay(BIRDS, ACROSS));
        text.EndFrame(jobs); // the worker starts on nine glyphs, a few milliseconds each
    } // gone with most of them still waiting
    gfx::TextLayout again = OpenFonts();
    static_cast<void>(again.Lay(CRANE, ACROSS));
    again.Flush(jobs);
    const gfx::Layout& layout = again.Lay(CRANE, ACROSS);
    CHECK(layout.complete);
    REQUIRE(layout.glyphs.size() == 1);
    REQUIRE(again.Pending().size() == 1);
    const gfx::GlyphUpload& upload = again.Pending()[0];
    CHECK(layout.glyphs[0].width == static_cast<f32>(upload.width));
    CHECK(std::ranges::any_of(upload.field, [](u8 texel) { return texel >= FIELD_EDGE; }));
}

TEST_CASE("a layout is in base pixels whatever size it is drawn at") {
    gfx::TextLayout text = OpenFonts();
    const f32 lineHeight = LineHeight();
    const gfx::Layout fox = text.Lay("Fox", ACROSS);
    const gfx::Layout foxFox = text.Lay("FoxFox", ACROSS);
    constexpr auto MARGINS = static_cast<f32>(2 * gfx::SDF_SPREAD); // the spread on either side
    CHECK(std::abs(fox.height - (lineHeight + MARGINS)) <= 1.0F);
    constexpr f32 TWICE = 2.0F;
    CHECK(std::abs(foxFox.width - (TWICE * fox.width)) <= MARGINS);
}

TEST_CASE("every glyph's field falls inside its layout's box, either way") {
    constexpr std::string_view MIXED = "问道 Fox";
    gfx::TextLayout text = WarmedWith(MIXED);
    for (const core::TextDirection direction : BOTH_WAYS) {
        const gfx::Layout& layout = text.Lay(MIXED, direction);
        REQUIRE(layout.glyphs.size() == 5);
        for (const gfx::PlacedGlyph& glyph : layout.glyphs) {
            CHECK(glyph.x >= 0.0F);
            CHECK(glyph.y >= 0.0F);
            CHECK(glyph.x + glyph.width <= layout.width);
            CHECK(glyph.y + glyph.height <= layout.height);
        }
    }
}

TEST_CASE("a glyph's field sits on its outline, across") {
    constexpr std::string_view LETTER = "F";
    constexpr u32 CHARACTER = 'F';
    constexpr auto MARGINS = static_cast<f32>(2 * gfx::SDF_SPREAD);
    gfx::TextLayout text = WarmedWith(LETTER);
    const gfx::Layout& layout = text.Lay(LETTER, ACROSS);
    REQUIRE(layout.glyphs.size() == 1);
    const gfx::PlacedGlyph& glyph = layout.glyphs[0];
    const Ink ink = InkOf(CHARACTER);
    // The outline starts SDF_SPREAD into its field, and the text SDF_SPREAD into its box: the field's corner in the
    // box is the outline's in the line, its bearing from the pen and its height below the ascent.
    CHECK(std::abs(glyph.x - static_cast<f32>(ink.minX)) <= 1.0F);
    CHECK(std::abs(glyph.y - static_cast<f32>(ink.ascent - ink.maxY)) <= 1.0F);
    CHECK(std::abs((glyph.width - MARGINS) - static_cast<f32>(ink.maxX - ink.minX)) <= 1.0F);
    CHECK(std::abs((glyph.height - MARGINS) - static_cast<f32>(ink.maxY - ink.minY)) <= 1.0F);
}

TEST_CASE("a vertical caption's glyphs sit centred in its column, an em apart") {
    constexpr std::string_view CAPTION = "问道";
    constexpr u32 WEN = 0x95EE; // 问
    constexpr u32 DAO = 0x9053; // 道
    constexpr auto MARGINS = static_cast<f32>(2 * gfx::SDF_SPREAD);
    constexpr f32 HALF = 0.5F;
    gfx::TextLayout text = WarmedWith(CAPTION);
    const gfx::Layout& down = text.Lay(CAPTION, DOWN);
    REQUIRE(down.glyphs.size() == 2);
    const Ink wen = InkOf(WEN);
    const Ink dao = InkOf(DAO);
    // Across: the column is as wide as its widest outline, and each outline sits in its middle.
    const f32 column = down.width - MARGINS;
    CHECK(std::abs(column - static_cast<f32>(std::max(wen.maxX - wen.minX, dao.maxX - dao.minX))) <= 1.0F);
    for (const gfx::PlacedGlyph& glyph : down.glyphs) {
        const f32 centred = (column - (glyph.width - MARGINS)) * HALF;
        CHECK(std::abs(glyph.x - centred) <= 1.0F);
    }
    // Down: the first outline starts the column and the last ends it, and the second is an em (an ideograph's
    // vertical advance) below the first, as the two sit alike in their em boxes.
    const auto top = std::ranges::min_element(down.glyphs, {}, &gfx::PlacedGlyph::y);
    const auto bottom = std::ranges::max_element(down.glyphs, {}, &gfx::PlacedGlyph::y);
    CHECK(top->y == 0.0F);
    CHECK(bottom->y + bottom->height == down.height);
    CHECK(std::abs((bottom->y - top->y) - gfx::SDF_BASE_PIXELS) <= 1.0F);
    CHECK(std::abs((top->height - MARGINS) - static_cast<f32>(wen.maxY - wen.minY)) <= 1.0F);
}

TEST_CASE("a lone character's column is its outline's height") {
    constexpr std::string_view LONE = "问";
    constexpr u32 WEN = 0x95EE;
    constexpr auto MARGINS = static_cast<f32>(2 * gfx::SDF_SPREAD);
    gfx::TextLayout text = WarmedWith(LONE);
    const gfx::Layout& down = text.Lay(LONE, DOWN);
    REQUIRE(down.glyphs.size() == 1);
    const Ink wen = InkOf(WEN);
    CHECK(down.glyphs[0].y == 0.0F); // not an em down, as SDL3_ttf leaves a column shorter than three characters
    CHECK(std::abs((down.height - MARGINS) - static_cast<f32>(wen.maxY - wen.minY)) <= 1.0F);
}

TEST_CASE("a vertical caption runs down, its first character on top") {
    constexpr std::string_view CAPTION = "问道";
    gfx::TextLayout text = WarmedWith(CAPTION);
    const gfx::Layout& across = text.Lay(CAPTION, ACROSS);
    REQUIRE(across.glyphs.size() == 2);
    CHECK(across.width > across.height);
    const u32 first = across.glyphs[0].u; // 问's place on the page, whichever layout draws it
    const gfx::Layout& down = text.Lay(CAPTION, DOWN);
    CHECK(down.height > down.width);
    REQUIRE(down.glyphs.size() == 2);
    const auto top = std::ranges::min_element(down.glyphs, {}, &gfx::PlacedGlyph::y);
    const auto bottom = std::ranges::max_element(down.glyphs, {}, &gfx::PlacedGlyph::y);
    CHECK(top->u == first);
    constexpr auto MARGINS = static_cast<f32>(2 * gfx::SDF_SPREAD); // neighbouring fields' margins overlap
    CHECK(bottom->y >= top->y + top->height - MARGINS);
}

TEST_CASE("a character the font lacks draws nothing and is never asked of the worker") {
    gfx::TextLayout text = OpenFonts();
    const gfx::Layout& layout = text.Lay("😀", ACROSS);
    CHECK(layout.glyphs.empty());
    CHECK(layout.complete);
    CHECK(text.Pending().empty());
}

} // namespace
