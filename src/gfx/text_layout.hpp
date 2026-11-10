#pragma once

/// @file
/// @ingroup gfx
/// Text laid out into glyphs whose signed distance fields sit on the glyph cache's pages (the phase 6 chunk 3 spec,
/// "The glyph cache: distance fields"; design doc 0002.4.2 §3): the CPU side of drawing game text, tested without a
/// device. SDL3_ttf shapes and lays out; each glyph is rasterised once, at SDF_BASE_PIXELS, as a distance field that
/// serves every size it's drawn at, and the renderer uploads what is new and draws the quads.

#include "core/error.hpp"
#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/strings.hpp"
#include "core/types.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace gfx {

/// The width and height of every glyph page, in texels (R8: one byte of distance field a texel).
inline constexpr u32 GLYPH_PAGE_SIZE = 2048;
/// A layout not asked for in this many frames is forgotten.
inline constexpr u64 LAYOUT_FRAMES = 300;
/// The size every glyph's field is rasterised at, in pixels: a layout's base pixels. Drawn at `pixels`, a layout scales
/// by `pixels / SDF_BASE_PIXELS`.
inline constexpr f32 SDF_BASE_PIXELS = 48.0F;
/// How far a field reaches beyond its glyph's outline, in texels (FreeType's default spread, which SDL3_ttf keeps): a
/// field is its glyph with this margin on every side, 128 on the outline, above inside and below outside, falling to
/// 0 at the margin's edge.
inline constexpr u32 SDF_SPREAD = 8;

/// One glyph of a laid-out text, and where its field is.
struct PlacedGlyph {
    /// Its field's left edge in base pixels from the left edge of the text's box.
    f32 x = 0.0F;
    /// Its field's top edge in base pixels from the top edge of the text's box.
    f32 y = 0.0F;
    /// Its field's width in base pixels, and on its page in texels: a texel a base pixel.
    f32 width = 0.0F;
    /// Its field's height likewise.
    f32 height = 0.0F;
    /// The page its field is on.
    u32 page = 0;
    /// Its field's left edge on the page, in texels.
    u32 u = 0;
    /// Its field's top edge on the page, in texels.
    u32 v = 0;
};

/// A text laid out in one direction, in base pixels. Its box is the shaped text's with SDF_SPREAD on every side, so its
/// glyphs' fields, margins and all, fall inside it: across, a line of the font's height and the text's advance; down,
/// a column as wide as its widest glyph, from its top glyph's outline to its bottom one's.
struct Layout {
    /// The width of the text's box.
    f32 width = 0.0F;
    /// Its height.
    f32 height = 0.0F;
    /// Its glyphs that draw anything and are rasterised, in drawing order.
    std::vector<PlacedGlyph> glyphs{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// Whether every glyph is there: false while one is still being rasterised on the worker (and left out until then).
    bool complete = true;
};

/// A glyph's field, rasterised and waiting to go up to its page.
struct GlyphUpload {
    /// Its page.
    u32 page = 0;
    /// Its left edge on the page.
    u32 x = 0;
    /// Its top edge on the page.
    u32 y = 0;
    /// Its width in texels.
    u32 width = 0;
    /// Its height in texels.
    u32 height = 0;
    /// One byte of distance field a texel, in rows from the top without gaps: 128 on the glyph's outline, higher
    /// inside it, lower outside, reaching 0 SDF_SPREAD texels out.
    std::vector<u8> field{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
};

/// The game text's font, the glyph fields rasterised from it and the texts laid out with it. A glyph is rasterised
/// once: at start-up by Prewarm(), or later on a job-system worker, the first time a layout needs it, so no frame waits
/// for one. A layout is made once and kept while it's asked for. Main thread only. Destroying it while the worker
/// rasterises for it waits for the glyph in hand, and the job system may run other queued work (a save, a decode,
/// parallel-for chunks) on this thread meanwhile; the job system it last gave work to must still be there.
class TextLayout {
public:
    /// Opens `NotoSansSC-Regular.otf` from `fonts` (`assets/fonts/`) at SDF_BASE_PIXELS, once to shape text on this
    /// thread and once to rasterise fields on the worker, or says why it couldn't.
    [[nodiscard]] static std::expected<TextLayout, core::Error> Open(const std::filesystem::path& fonts);

    TextLayout(const TextLayout&) = delete;
    TextLayout& operator=(const TextLayout&) = delete;
    /// Takes over `other`'s fonts and caches, leaving it empty.
    TextLayout(TextLayout&& other) noexcept;
    /// Takes over `other`'s fonts and caches, leaving it empty.
    TextLayout& operator=(TextLayout&& other) noexcept;
    ~TextLayout();

    /// Rasterises now, on this thread, every glyph of every string of `strings` in every language, laid out either way,
    /// of the label stack without its `{name}` and `{count}` and filled with each string of its language and every
    /// digit, and of the digits, signs and brackets numbers show: start-up's one designed pause, so the game's own text
    /// never waits for the worker. Logs how many glyphs it rasterised and how long that took.
    void Prewarm(const core::Strings& strings);

    /// `text` laid out running `direction`, from the cache or made now. A glyph not rasterised yet is left out
    /// (`complete` false) and asked of the worker at EndFrame(); the layout is made again by the first Lay() after it
    /// lands. A character the font lacks draws nothing and is logged once; a text that can't be laid out is empty, and
    /// logged. The reference holds until the next EndFrame().
    [[nodiscard]] const Layout& Lay(std::string_view text, core::TextDirection direction);

    /// Ends a frame: forgets the layouts not asked for in the last LAYOUT_FRAMES, moves the fields the worker has
    /// finished into Pending(), and if glyphs wait to be rasterised and no batch is running, starts one on `jobs` as
    /// low-priority work (Jobs::Start), which never waits behind a background job such as a save nor holds one up. One
    /// batch at a time: the worker's font is used by one thread. Never waits. `jobs` must outlive this cache.
    void EndFrame(core::Jobs& jobs);

    /// Waits until every glyph asked for so far is rasterised, and moves them all into Pending(): for a capture that
    /// must not miss a glyph, and for tests. While it waits, the job system may run other queued work (a save, a
    /// decode, parallel-for chunks) on this thread. A frame never calls it.
    void Flush(core::Jobs& jobs);

    /// How many glyph pages the rasterised glyphs use.
    [[nodiscard]] u32 Pages() const;

    /// The rasterised fields not yet uploaded, oldest first.
    [[nodiscard]] std::span<const GlyphUpload> Pending() const;

    /// The first `count` of Pending() are on their pages now.
    void Uploaded(std::size_t count);

private:
    struct Impl;
    explicit TextLayout(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace gfx
