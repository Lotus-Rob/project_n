#include "gfx/text_layout.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"
#include "core/thread_safety.hpp"
#include "gfx/internal/gpu.hpp"
#include "gfx/shelf_packer.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_textengine.h>
#include <SDL3_ttf/SDL_ttf.h>

namespace gfx {

namespace {

constexpr std::string_view FONT_FILE = "NotoSansSC-Regular.otf";
// What numbers and label stacks show besides the strings: values, signs and the brackets of "(+N)" in either language.
constexpr std::string_view NUMBER_CHARACTERS = "0123456789+-（）() ";
// Every digit a label stack's count can show.
constexpr std::string_view DIGITS = "0123456789";
// The label stack's placeholders (core::StringId::LabelStack).
constexpr std::string_view NAME_PLACEHOLDER = "{name}";
constexpr std::string_view COUNT_PLACEHOLDER = "{count}";
constexpr std::array<core::TextDirection, 2> DIRECTIONS{ { core::TextDirection::LeftToRight,
                                                           core::TextDirection::TopToBottom } };
// Fields packed this far apart are never read into each other. Past a quad's edge the field shader reads half a texel
// (bilinear filtering) beyond the further of its shadow sample (shifted 2 texels) and its supersampling grid (0.375
// screen pixels, each SDF_BASE_PIXELS / pixels texels): within the padding for text drawn at about 2 pixels or more
// (at 2.2, 8.7 texels), and smaller text is faded out or never drawn that small (LABEL_MIN_PIXELS, NUMBER_MIN_PIXELS,
// CaptionOpacity()). The outline is sampled with the fill, at the same points.
constexpr u32 GLYPH_PADDING = SDF_SPREAD + 1;

using Font = std::unique_ptr<TTF_Font, internal::SdlDeleter<TTF_CloseFont>>;
using Text = std::unique_ptr<TTF_Text, internal::SdlDeleter<TTF_DestroyText>>;

// TTF_Init() and TTF_Quit() are counted: whoever holds the fonts holds the library open while they are.
struct TtfLibrary {
    TtfLibrary() = default;
    TtfLibrary(const TtfLibrary&) = delete;
    TtfLibrary& operator=(const TtfLibrary&) = delete;
    TtfLibrary(TtfLibrary&&) = delete;
    TtfLibrary& operator=(TtfLibrary&&) = delete;
    ~TtfLibrary() {
        if (open) {
            TTF_Quit();
        }
    }
    bool open = false;
};

// `stack` (the label stack's text) with `name` and `count` in its placeholders' places.
std::string Filled(std::string stack, std::string_view name, std::string_view count) {
    if (const std::size_t place = stack.find(NAME_PLACEHOLDER); place != std::string::npos) {
        stack.replace(place, NAME_PLACEHOLDER.size(), name);
    }
    if (const std::size_t place = stack.find(COUNT_PLACEHOLDER); place != std::string::npos) {
        stack.replace(place, COUNT_PLACEHOLDER.size(), count);
    }
    return stack;
}

// What Prewarm() rasterises: the number characters; every string in each language; and the label stack without its
// placeholders and filled with each of its language's strings and every digit, as HarfBuzz shapes a digit after Latin
// letters into another glyph than a digit on its own.
std::vector<std::string> PrewarmTexts(const core::Strings& strings) {
    std::vector<std::string> texts{ std::string{ NUMBER_CHARACTERS } };
    for (std::size_t language = 0; language < core::LANGUAGES; ++language) {
        const std::string& stack = strings.text.at(std::to_underlying(core::StringId::LabelStack)).at(language);
        texts.push_back(Filled(stack, "", ""));
        for (std::size_t string = 0; string < core::STRINGS; ++string) {
            if (string != std::to_underlying(core::StringId::LabelStack)) {
                const std::string& text = strings.text.at(string).at(language);
                texts.push_back(text);
                texts.push_back(Filled(stack, text, DIGITS));
            }
        }
    }
    return texts;
}

// A glyph's field as it was rasterised; no texels when it couldn't be.
struct Field {
    u32 glyph = 0;
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> texels{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
};

// The glyph's distance field (the alpha channel: SDL3_ttf's field glyphs are white), rows without gaps.
Field Rasterise(TTF_Font* font, u32 glyph) {
    TTF_ImageType type = TTF_IMAGE_INVALID;
    const internal::Surface image{ TTF_GetGlyphImageForIndex(font, glyph, &type) };
    Field field{ .glyph = glyph };
    if (image == nullptr) {
        PN_LOG_WARNING("glyph {} wasn't rasterised, so it isn't drawn: {}", glyph, SDL_GetError());
        return field;
    }
    field.width = static_cast<u32>(image->w);
    field.height = static_cast<u32>(image->h);
    field.texels.reserve(std::size_t{ field.width } * field.height);
    for (i32 row = 0; row < image->h; ++row) {
        for (i32 column = 0; column < image->w; ++column) {
            u8 red = 0;
            u8 green = 0;
            u8 blue = 0;
            u8 alpha = 0;
            static_cast<void>(SDL_ReadSurfacePixel(image.get(), column, row, &red, &green, &blue, &alpha));
            field.texels.push_back(alpha);
        }
    }
    return field;
}

// The fonts, and what the main thread and the worker share. The TextLayout owns it and waits for its batch before
// letting it go, so the fonts open and close on the main thread, one after the other (FreeType's library never opens
// or closes two faces at once), then the library quits.
struct Fonts {
    Fonts() = default;
    Fonts(const Fonts&) = delete;
    Fonts& operator=(const Fonts&) = delete;
    Fonts(Fonts&&) = delete;
    Fonts& operator=(Fonts&&) = delete;
    ~Fonts() = default;

    // A batch: rasterises the requests one at a time, oldest first, until none are left (or the TextLayout, going,
    // has dropped them).
    void RasteriseRequests() PN_REQUIRES(!queueMutex, !fieldMutex) {
        while (true) {
            u32 glyph = 0;
            {
                const std::scoped_lock lock{ queueMutex };
                if (requests.empty()) {
                    return;
                }
                glyph = requests.front();
                requests.erase(requests.begin());
            }
            Field rasterised = RasteriseField(glyph);
            const std::scoped_lock lock{ queueMutex };
            done.push_back(std::move(rasterised));
        }
    }

    Field RasteriseField(u32 glyph) PN_REQUIRES(!fieldMutex) {
        PN_PROFILE_ZONE();
        const std::scoped_lock lock{ fieldMutex };
        return Rasterise(field.get(), glyph);
    }

    // First, so the library quits after the fonts close.
    TtfLibrary library{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    // Shapes on the main thread. Without SDF: SDL3_ttf 3.2.2 shapes a field font's glyphs top to bottom 8 pixels too
    // far left and 8 or 16 too high by glyph, gives a space a blank field and widens the line by a spread, while a plain
    // font's glyphs sit exactly where their fields' outlines do, less the spread, either way.
    Font shaping = nullptr;
    core::Mutex fieldMutex{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    // Rasterises fields: the worker's batch, or Prewarm() on the main thread, one at a time.
    Font field PN_GUARDED_BY(fieldMutex) = nullptr;
    core::Mutex queueMutex{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<u32> requests PN_GUARDED_BY(queueMutex){}; // glyphs for the worker, oldest first
    std::vector<Field> done PN_GUARDED_BY(queueMutex){};   // its fields, not yet taken by EndFrame()
    // NOLINTEND(readability-redundant-member-init)
};

// Where a glyph the cache has seen stands.
enum class GlyphState : u8 {
    Requested, // asked of the worker, not landed yet
    Missing,   // can't be drawn, and said so
    Placed,    // on its page
};

struct CachedGlyph {
    GlyphState state = GlyphState::Requested;
    PackedSlot slot{};
    u32 width = 0;
    u32 height = 0;
};

struct CachedLayout {
    Layout layout{};
    u64 lastUsed = 0;
    u64 landings = 0; // Impl::landings when it was made
};

} // namespace

struct TextLayout::Impl {
    explicit Impl(std::unique_ptr<Fonts> opened)
      : fonts{ std::move(opened) } {
    }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    // A running batch stops after the glyph in hand, nobody being left to take the rest; it's waited for before the
    // fonts go.
    ~Impl() {
        {
            const std::scoped_lock lock{ fonts->queueMutex };
            fonts->requests.clear();
        }
        batch.reset();
    }

    template<typename Visit>
    std::optional<Layout> Shape(std::string_view text, core::TextDirection direction, const Visit& visit);
    void WarnMissing(std::string_view text);
    Layout Make(std::string_view text, core::TextDirection direction);
    void Place(Field field);
    void Exchange(core::Jobs& jobs);

    std::unique_ptr<Fonts> fonts;
    ShelfPacker packer{ GLYPH_PAGE_SIZE, GLYPH_PADDING };
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::unordered_map<u32, CachedGlyph> glyphs{};           // by glyph index
    std::unordered_map<std::string, CachedLayout> layouts{}; // by direction and text
    std::string key{}; // Lay()'s lookup key, kept so a lookup allocates nothing once it has grown
    std::vector<GlyphUpload> pending{};
    std::vector<u32> wanted{};   // requested glyphs not yet handed to the worker
    std::vector<Field> landed{}; // the worker's fields, taken from it
    std::vector<u32> missing{};  // the characters warned about
    // NOLINTEND(readability-redundant-member-init)
    u64 frame = 0;
    u64 landings = 0; // how many times the worker's fields have landed
    // The batch on the worker, if any. Last: destroyed first, so it's done before the fonts go.
    std::optional<core::Jobs::Pending> batch = std::nullopt;
};

// `text` shaped running `direction` into its box (glyphs not yet placed), calling `visit` with each glyph that draws
// something; none, logged, when SDL3_ttf can't lay it out.
template<typename Visit>
std::optional<Layout> TextLayout::Impl::Shape(std::string_view text, core::TextDirection direction, const Visit& visit) {
    constexpr auto MARGINS = static_cast<f32>(2 * SDF_SPREAD);
    if (text.empty()) {
        return Layout{ .width = MARGINS, .height = MARGINS };
    }
    WarnMissing(text);
    // No text engine: the draw operations the layout leaves in the text are all that's needed, so it goes at once.
    const Text laid{ TTF_CreateText(nullptr, fonts->shaping.get(), text.data(), text.size()) };
    // Top to bottom asks for bottom to top: SDL3_ttf 3.2.2 adds HarfBuzz's vertical advances (negative, y up) as if y
    // ran down, so its glyphs climb; bottom to top reverses their order, which brings the first character back on top.
    const bool done = laid != nullptr &&
                      (direction == core::TextDirection::LeftToRight || TTF_SetTextDirection(laid.get(), TTF_DIRECTION_BTT)) &&
                      TTF_UpdateText(laid.get());
    if (!done) {
        PN_LOG_WARNING("\"{}\" wasn't laid out, so it isn't drawn: {}", text, SDL_GetError());
        return std::nullopt;
    }
    const TTF_TextData& data = *laid->internal;
    const std::span<const TTF_DrawOperation> operations =
        internal::SpanAt(static_cast<const TTF_DrawOperation*>(data.ops), static_cast<std::size_t>(data.num_ops));
    // Glyph 0 is the font's box for a missing character, warned about above.
    const auto draws = [](const TTF_DrawOperation& operation) {
        return operation.cmd == TTF_DRAW_COMMAND_COPY && operation.copy.glyph_index != 0 && operation.copy.dst.w > 0 &&
               operation.copy.dst.h > 0;
    };
    // A column's box runs from its top glyph's ink to its bottom one's. SDL3_ttf 3.2.2 does so from three characters
    // on, but pads a shorter column's top (a lone character sits an em down), which would hang a caption below its
    // anchor.
    i32 top = 0;
    i32 height = data.h;
    if (direction == core::TextDirection::TopToBottom) {
        std::optional<i32> highest = std::nullopt;
        i32 lowest = 0;
        for (const TTF_DrawOperation& operation : operations) {
            if (draws(operation)) {
                highest = std::min(highest.value_or(operation.copy.dst.y), operation.copy.dst.y);
                lowest = std::max(lowest, operation.copy.dst.y + operation.copy.dst.h);
            }
        }
        if (highest) {
            top = *highest;
            height = lowest - top;
        }
    }
    for (const TTF_DrawOperation& operation : operations) {
        if (draws(operation)) {
            TTF_CopyOperation copy = operation.copy;
            copy.dst.y -= top;
            visit(copy);
        }
    }
    return Layout{ .width = static_cast<f32>(data.w) + MARGINS, .height = static_cast<f32>(height) + MARGINS };
}

void TextLayout::Impl::WarnMissing(std::string_view text) {
    const char* next = text.data();
    std::size_t left = text.size();
    while (left > 0) {
        const u32 character = SDL_StepUTF8(&next, &left);
        if (!TTF_FontHasGlyph(fonts->shaping.get(), character) && !std::ranges::contains(missing, character)) {
            missing.push_back(character);
            PN_LOG_WARNING("the font has no U+{:04X} (in \"{}\"), so it isn't drawn", character, text);
        }
    }
}

Layout TextLayout::Impl::Make(std::string_view text, core::TextDirection direction) {
    PN_PROFILE_ZONE();
    std::vector<PlacedGlyph> placed;
    bool complete = true;
    // A field is its glyph with SDF_SPREAD around it, and the box has SDF_SPREAD around the shaped text: the field's
    // corner in the box is the glyph's in the shaped text.
    const auto visit = [&](const TTF_CopyOperation& copy) {
        const auto [cached, added] = glyphs.try_emplace(copy.glyph_index);
        if (added) {
            wanted.push_back(copy.glyph_index);
        }
        const CachedGlyph& glyph = cached->second;
        if (glyph.state == GlyphState::Requested) {
            complete = false;
        }
        else if (glyph.state == GlyphState::Placed) {
            placed.push_back(
                { .x = static_cast<f32>(copy.dst.x),
                  .y = static_cast<f32>(copy.dst.y),
                  .width = static_cast<f32>(glyph.width),
                  .height = static_cast<f32>(glyph.height),
                  .page = glyph.slot.page,
                  .u = glyph.slot.x,
                  .v = glyph.slot.y }
            );
        }
    };
    std::optional<Layout> layout = Shape(text, direction, visit);
    if (!layout) {
        return {};
    }
    layout->glyphs = std::move(placed);
    layout->complete = complete;
    return std::move(*layout);
}

void TextLayout::Impl::Place(Field field) {
    CachedGlyph& glyph = glyphs[field.glyph];
    if (glyph.state != GlyphState::Requested) {
        return; // pre-warmed while the worker had it too
    }
    glyph.state = GlyphState::Missing;
    if (field.texels.empty()) {
        return; // warned about by Rasterise()
    }
    const std::optional<PackedSlot> slot = packer.Pack(field.width, field.height);
    if (!slot) {
        PN_LOG_WARNING(
            "glyph {} is {}x{}, larger than a glyph page, so it isn't drawn", field.glyph, field.width, field.height
        );
        return;
    }
    glyph = { .state = GlyphState::Placed, .slot = *slot, .width = field.width, .height = field.height };
    pending.push_back(
        { .page = slot->page,
          .x = slot->x,
          .y = slot->y,
          .width = field.width,
          .height = field.height,
          .field = std::move(field.texels) }
    );
}

// Lands the fields the worker has finished, and hands it the glyphs asked for since; starts a batch for them when none
// is running. A batch is low-priority work (Jobs::Start), never queued behind a background job such as a save, nor
// holding one up.
void TextLayout::Impl::Exchange(core::Jobs& jobs) {
    if (batch && batch->Done()) {
        batch.reset();
    }
    bool start = false;
    {
        const std::scoped_lock lock{ fonts->queueMutex };
        std::swap(landed, fonts->done);
        fonts->requests.insert(fonts->requests.end(), wanted.begin(), wanted.end());
        // A batch found Done() has seen the queue empty, so requests it didn't take wait for the next one.
        start = !batch && !fonts->requests.empty();
    }
    wanted.clear();
    if (!landed.empty()) {
        for (Field& field : landed) {
            Place(std::move(field));
        }
        landed.clear();
        ++landings;
    }
    if (start) {
        batch.emplace(jobs.Start(1, [worker = fonts.get()](u32 /*chunk*/) { worker->RasteriseRequests(); }));
    }
}

std::expected<TextLayout, core::Error> TextLayout::Open(const std::filesystem::path& fonts) {
    auto loaded = std::make_unique<Fonts>();
    if (!TTF_Init()) {
        return std::unexpected{ core::Error{ std::format("SDL3_ttf didn't start: {}", SDL_GetError()) } };
    }
    loaded->library.open = true;
    const std::string file = (fonts / FONT_FILE).string();
    loaded->shaping.reset(TTF_OpenFont(file.c_str(), SDF_BASE_PIXELS));
    bool opened = loaded->shaping != nullptr;
    {
        const std::scoped_lock lock{ loaded->fieldMutex };
        loaded->field.reset(opened ? TTF_OpenFont(file.c_str(), SDF_BASE_PIXELS) : nullptr);
        opened = loaded->field != nullptr && TTF_SetFontSDF(loaded->field.get(), true);
    }
    if (!opened) {
        return std::unexpected{ core::Error{ std::format("the font {} wasn't opened: {}", file, SDL_GetError()) } };
    }
    PN_LOG_INFO("the font {} opened for distance-field text at {} pixels", file, SDF_BASE_PIXELS);
    return TextLayout{ std::make_unique<Impl>(std::move(loaded)) };
}

TextLayout::TextLayout(std::unique_ptr<Impl> impl)
  : m_impl{ std::move(impl) } {
}
TextLayout::TextLayout(TextLayout&& other) noexcept = default;
TextLayout& TextLayout::operator=(TextLayout&& other) noexcept = default;
TextLayout::~TextLayout() = default;

void TextLayout::Prewarm(const core::Strings& strings) {
    const auto start = std::chrono::steady_clock::now();
    std::vector<u32> unseen;
    for (const std::string& text : PrewarmTexts(strings)) {
        for (const core::TextDirection direction : DIRECTIONS) {
            static_cast<void>(m_impl->Shape(text, direction, [&](const TTF_CopyOperation& copy) {
                const auto cached = m_impl->glyphs.find(copy.glyph_index);
                if ((cached == m_impl->glyphs.end() || cached->second.state == GlyphState::Requested) &&
                    !std::ranges::contains(unseen, copy.glyph_index)) {
                    unseen.push_back(copy.glyph_index);
                }
            }));
        }
    }
    for (const u32 glyph : unseen) {
        m_impl->glyphs.try_emplace(glyph);
        m_impl->Place(m_impl->fonts->RasteriseField(glyph));
    }
    const std::chrono::duration<f64, std::milli> took = std::chrono::steady_clock::now() - start;
    PN_LOG_INFO("glyphs rasterised ahead of time as distance fields: {}, in {:.0f} ms", unseen.size(), took.count());
}

// No zone: it runs once per text, tens of times a frame, and a zone costs more than a hit; a miss's Make() has one.
const Layout& TextLayout::Lay(std::string_view text, core::TextDirection direction) {
    std::string& key = m_impl->key;
    key.assign(1, static_cast<char>(direction));
    key += text;
    auto cached = m_impl->layouts.find(key);
    if (cached == m_impl->layouts.end()) {
        cached = m_impl->layouts.emplace(key, CachedLayout{ .layout = m_impl->Make(text, direction) }).first;
        cached->second.landings = m_impl->landings;
    }
    else if (!cached->second.layout.complete && cached->second.landings != m_impl->landings) {
        cached->second.layout = m_impl->Make(text, direction);
        cached->second.landings = m_impl->landings;
    }
    cached->second.lastUsed = m_impl->frame;
    return cached->second.layout;
}

void TextLayout::EndFrame(core::Jobs& jobs) {
    PN_PROFILE_ZONE();
    Impl& impl = *m_impl;
    ++impl.frame;
    std::erase_if(impl.layouts, [&impl](const auto& entry) { return entry.second.lastUsed + LAYOUT_FRAMES < impl.frame; });
    impl.Exchange(jobs);
}

void TextLayout::Flush(core::Jobs& jobs) {
    PN_PROFILE_ZONE();
    m_impl->Exchange(jobs);
    while (m_impl->batch) {
        m_impl->batch.reset(); // waits for it
        m_impl->Exchange(jobs);
    }
}

u32 TextLayout::Pages() const {
    return m_impl->packer.Pages();
}

std::span<const GlyphUpload> TextLayout::Pending() const {
    return m_impl->pending;
}

void TextLayout::Uploaded(std::size_t count) {
    PN_ASSERT(count <= m_impl->pending.size(), "more glyph fields uploaded than were pending");
    m_impl->pending.erase(m_impl->pending.begin(), m_impl->pending.begin() + static_cast<std::ptrdiff_t>(count));
}

} // namespace gfx
