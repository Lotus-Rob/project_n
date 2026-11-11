#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/stable_id.hpp"
#include "core/strings.hpp"
#include "gfx/camera.hpp"
#include "gfx/mesh_vertex.hpp"
#include "gfx/text_layout.hpp"
#include "gfx/text_place.hpp"
#include "platform/paths.hpp"

#include <doctest/doctest.h>

namespace {

constexpr core::TextDirection ACROSS = core::TextDirection::LeftToRight;
constexpr std::array<core::NumberSign, 2> SIGNS{ { core::NumberSign::Plain, core::NumberSign::Plus } };
constexpr std::array<i32, 11> VALUES{ { 0,
                                        7,
                                        12,
                                        105,
                                        9'999,
                                        40'321,
                                        1'234'567'890,
                                        -5,
                                        -1'200,
                                        std::numeric_limits<i32>::max(),
                                        std::numeric_limits<i32>::min() } };

constexpr u32 WIDTH = 1280;
constexpr u32 HEIGHT = 720;
constexpr f64 ALPHA = 0.3;
constexpr f32 PIXELS = 17.3F; // a size between the base pixels' steps, so nothing lands on whole pixels
constexpr gfx::Camera CAMERA{ .centre = { .x = 912.5F, .y = 530.25F }, .zoom = 0.7F };
constexpr core::StableId TINY_FOX{ 41 };
constexpr core::StableId SEEN_FOX{ 42 };
constexpr core::Vec2 TINY_FOX_START{ .x = 301.75F, .y = 288.5F };
constexpr u32 HEALING = 0xFF25'B533U;
constexpr u32 DAMAGE = 0xFFFF'FFFFU;
// What the vertices and pages hold before the numbers are placed after it.
constexpr gfx::MeshVertex EARLIER{ .x = 1.0F };
constexpr u32 EARLIER_PAGE = 0;

// A cache with every digit and sign pre-warmed, as the renderer's is.
gfx::TextLayout Warmed() {
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    const std::string why = assets.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(assets, why);
    std::expected<gfx::TextLayout, core::Error> layout = gfx::TextLayout::Open(*assets / "fonts");
    const std::string error = layout.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(layout, error);
    layout->Prewarm(core::Strings{});
    return std::move(*layout);
}

// What the renderer writes for a number.
std::string Written(i32 value, core::NumberSign sign) {
    return std::format("{}{}", sign == core::NumberSign::Plus ? "+" : "", value);
}

TEST_CASE("a number composed from its characters is the layout its text shapes into") {
    gfx::TextLayout text = Warmed();
    const gfx::NumberFace face = gfx::NumberFaceOf(text);
    for (const core::NumberSign sign : SIGNS) {
        for (const i32 value : VALUES) {
            const std::string written = Written(value, sign);
            CAPTURE(written);
            const gfx::Layout& shaped = text.Lay(written, ACROSS);
            REQUIRE(shaped.complete);
            const gfx::ComposedNumber composed = gfx::ComposeNumber(face, value, sign);
            CHECK(composed.complete);
            CHECK(composed.width == shaped.width);
            CHECK(composed.height == shaped.height);
            REQUIRE(composed.count == shaped.glyphs.size());
            for (std::size_t glyph = 0; glyph < shaped.glyphs.size(); ++glyph) {
                CAPTURE(glyph);
                const gfx::PlacedGlyph& want = shaped.glyphs[glyph];
                const gfx::PlacedGlyph& got = composed.glyphs.at(glyph);
                CHECK(got.x == want.x);
                CHECK(got.y == want.y);
                CHECK(got.width == want.width);
                CHECK(got.height == want.height);
                CHECK(got.page == want.page);
                CHECK(got.u == want.u);
                CHECK(got.v == want.v);
            }
        }
    }
}

// A frame's numbers: every kind of value and sign, scattered, lifted and fading, some on the tiny fox; and texts that
// aren't placed: a label, a number faded out and one running down.
std::vector<core::TextItem> Numbers(std::size_t count) {
    constexpr f32 SPREAD_X = 37.0F;
    constexpr f32 SPREAD_Y = 11.0F;
    constexpr f32 WORLD_HEIGHT = 900.0F;
    constexpr core::Vec2 MOVED{ .x = 3.0F, .y = -2.0F };
    constexpr f32 SCATTER = 0.013F;
    constexpr f32 SCATTER_X = 0.75F; // either way
    constexpr f32 SCATTER_Y = 0.35F;
    constexpr f32 SCATTER_WIDTH = 1.5F;
    constexpr f32 SCATTER_HEIGHT = 0.7F;
    constexpr f32 LIFT = 0.007F;
    constexpr f32 MOST_LIFT = 1.5F;
    constexpr f32 FADING = 0.4F;
    constexpr std::size_t FADING_EVERY = 3;
    constexpr std::size_t TINY_EVERY = 5;
    constexpr core::Vec2 LABEL_AT{ .x = 5.0F };
    constexpr i32 FADED_OUT = 3;
    constexpr i32 RUNNING_DOWN = 4;
    std::vector<core::TextItem> texts;
    for (std::size_t index = 0; index < count; ++index) {
        const auto step = static_cast<f32>(index);
        const core::Vec2 from{ .x = std::fmod(step * SPREAD_X, static_cast<f32>(WIDTH)),
                               .y = std::fmod(step * SPREAD_Y, WORLD_HEIGHT) };
        texts.push_back(
            { .kind = core::TextKind::Number,
              .value = VALUES.at(index % VALUES.size()),
              .sign = SIGNS.at(index % SIGNS.size()),
              .previous = from,
              .current = from + MOVED,
              .scatter = { .x = std::fmod(step * SCATTER, SCATTER_WIDTH) - SCATTER_X,
                           .y = std::fmod(step * SCATTER, SCATTER_HEIGHT) - SCATTER_Y },
              .lift = std::fmod(step * LIFT, MOST_LIFT),
              .opacity = index % FADING_EVERY == 0 ? FADING : 1.0F,
              .entity = index % TINY_EVERY == 0 ? TINY_FOX : SEEN_FOX,
              .colour = index % 2 == 0 ? DAMAGE : HEALING }
        );
    }
    texts.push_back({ .kind = core::TextKind::Label, .previous = LABEL_AT, .current = LABEL_AT });
    texts.push_back({ .kind = core::TextKind::Number, .value = FADED_OUT, .opacity = 0.0F });
    texts.push_back({ .kind = core::TextKind::Number, .value = RUNNING_DOWN, .direction = core::TextDirection::TopToBottom });
    return texts;
}

constexpr std::array<gfx::NumberOrigin, 1> ORIGINS{ { { .entity = TINY_FOX, .start = TINY_FOX_START } } };

gfx::NumberPlacement Placement(const gfx::NumberFace& face) {
    return { .face = face,
             .clip = gfx::WorldToClip(CAMERA, WIDTH, HEIGHT),
             .alpha = static_cast<f32>(ALPHA),
             .width = WIDTH,
             .height = HEIGHT,
             .pixels = PIXELS,
             .outline = true,
             .shadow = false,
             .origins = ORIGINS };
}

TEST_CASE("a number is placed where its shaped layout would be drawn") {
    constexpr std::size_t COUNT = (2 * gfx::NUMBER_CHUNK) + 40; // three chunks, the last ending part of the way
    constexpr u32 SEVERAL = 4;
    gfx::TextLayout text = Warmed();
    const std::vector<core::TextItem> texts = Numbers(COUNT);
    // What drawing each number's shaped layout gives: its anchor, or the tiny fox's origin, moved by its scatter and lift.
    std::vector<gfx::MeshVertex> expected;
    std::vector<u32> expectedPages;
    const gfx::View view{ .alpha = ALPHA, .camera = CAMERA };
    for (const core::TextItem& item : std::span{ texts }.first(COUNT)) {
        core::Vec2 start = item.entity == TINY_FOX ? TINY_FOX_START : gfx::TextAnchor(item, view, WIDTH, HEIGHT);
        start.x += item.scatter.x * PIXELS;
        start.y += (item.scatter.y - item.lift) * PIXELS;
        const gfx::Layout& shaped = text.Lay(Written(item.value, item.sign), ACROSS);
        const f32 scale = PIXELS / gfx::SDF_BASE_PIXELS;
        const core::Vec2 topLeft =
            gfx::TextTopLeft(start, { .x = shaped.width * scale, .y = shaped.height * scale }, item.align);
        gfx::PlaceGlyphs(shaped, topLeft, PIXELS, gfx::TextColour(item, true, false), expected);
        for (const gfx::PlacedGlyph& glyph : shaped.glyphs) {
            expectedPages.push_back(glyph.page);
        }
    }
    const gfx::NumberFace face = gfx::NumberFaceOf(text);
    core::Jobs jobs{ SEVERAL };
    std::vector<gfx::MeshVertex> placed;
    std::vector<u32> pages;
    std::vector<std::size_t> scratch;
    gfx::PlaceNumbers(texts, Placement(face), jobs, placed, pages, scratch);
    REQUIRE(placed.size() == expected.size());
    CHECK(pages == expectedPages);
    for (std::size_t vertex = 0; vertex < expected.size(); ++vertex) {
        CAPTURE(vertex);
        CHECK(placed[vertex].x == expected[vertex].x);
        CHECK(placed[vertex].y == expected[vertex].y);
        CHECK(placed[vertex].uv == expected[vertex].uv);
        CHECK(placed[vertex].colour == expected[vertex].colour);
    }
}

// The vertices and pages `texts` are placed into on `workers` workers, after what was there, with scratch left over from
// an earlier call.
std::pair<std::vector<gfx::MeshVertex>, std::vector<u32>> PlacedOn(
    u32 workers,
    std::span<const core::TextItem> texts,
    const gfx::NumberFace& face
) {
    core::Jobs jobs{ workers };
    std::vector<gfx::MeshVertex> vertices(gfx::QUAD_VERTICES, EARLIER);
    std::vector<u32> pages{ EARLIER_PAGE };
    constexpr std::size_t STALE = 99;
    std::vector<std::size_t> scratch(3, STALE);
    gfx::PlaceNumbers(texts, Placement(face), jobs, vertices, pages, scratch);
    return { std::move(vertices), std::move(pages) };
}

TEST_CASE("numbers are placed the same, byte for byte, by one worker or many, after what was there") {
    constexpr std::size_t COUNT = 4'000; // a debug burst's worth: many chunks
    constexpr u32 MANY = 8;
    gfx::TextLayout text = Warmed();
    const std::vector<core::TextItem> texts = Numbers(COUNT);
    const gfx::NumberFace face = gfx::NumberFaceOf(text);
    const auto [alone, alonePages] = PlacedOn(1, texts, face);
    const auto [shared, sharedPages] = PlacedOn(MANY, texts, face);
    REQUIRE(alone.size() > COUNT * gfx::QUAD_VERTICES);
    CHECK(alone.front().x == EARLIER.x);
    CHECK(alonePages.front() == EARLIER_PAGE);
    CHECK(alonePages.size() * gfx::QUAD_VERTICES == alone.size());
    const std::span<const std::byte> aloneBytes = std::as_bytes(std::span{ alone });
    const std::span<const std::byte> sharedBytes = std::as_bytes(std::span{ shared });
    CHECK(std::ranges::equal(aloneBytes, sharedBytes));
    CHECK(alonePages == sharedPages);
}

} // namespace
