#include "gfx/text_place.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/profile.hpp"

namespace gfx {

namespace {

constexpr u32 CHANNEL_BITS = 8;
constexpr u32 CHANNEL_MASK = 0xFFU;
constexpr u32 CHANNELS = 4;
constexpr f32 HALF = 0.5F;
constexpr u32 DECIMAL = 10;
constexpr u8 PLUS_CHARACTER = 10;  // in NUMBER_FACE_CHARACTERS
constexpr u8 MINUS_CHARACTER = 11; // likewise
constexpr auto TEXT_MARGINS = static_cast<f32>(2 * SDF_SPREAD);

// The helpers below are what the public functions and each number in PlaceNumbers() share, so both compute the same
// bits. They have no zones: a number's are measured by its chunk's, and a zone costs more than their work.

u32 Scaled(u32 colour, f32 opacity) {
    u32 scaled = 0;
    for (u32 channel = 0; channel < CHANNELS; ++channel) {
        const u32 shift = channel * CHANNEL_BITS;
        const auto value = static_cast<f32>((colour >> shift) & CHANNEL_MASK) * std::clamp(opacity, 0.0F, 1.0F);
        scaled |= static_cast<u32>(std::lround(value)) << shift;
    }
    return scaled;
}

u32 ColourOfText(const core::TextItem& item, bool outline, bool shadow) {
    u32 colour = item.colour;
    // A number's colour tells its kind: the damage numbers choose a dark one of their own when both are off.
    if (!outline && !shadow && item.kind != core::TextKind::Number) {
        // Light text needs the dark edge to read on the pale ground; without one it turns dark, as opaque as it was.
        constexpr u32 ALPHA_SHIFT = 24;
        colour = Scaled(TEXT_INK, static_cast<f32>(colour >> ALPHA_SHIFT) / static_cast<f32>(CHANNEL_MASK));
    }
    return item.opacity >= 1.0F ? colour : Scaled(colour, item.opacity);
}

core::Vec2 AnchorOfText(const core::TextItem& item, const ClipTransform& clip, f32 alpha, u32 width, u32 height) {
    const core::Vec2 world = item.previous + ((item.current - item.previous) * alpha) + item.offset;
    return {
        .x = ((world.x * clip.scaleX) + clip.offsetX + 1.0F) * HALF * static_cast<f32>(width),
        .y = (1.0F - ((world.y * clip.scaleY) + clip.offsetY)) * HALF * static_cast<f32>(height),
    };
}

core::Vec2 TopLeftOfText(core::Vec2 anchor, core::Vec2 size, core::TextAlign align) {
    return { .x = anchor.x - (size.x * HALF), .y = align == core::TextAlign::BottomCentre ? anchor.y - size.y : anchor.y };
}

// `glyph`'s quad, its box's top left at `topLeft` and each base pixel `scale` target pixels, into `out`'s four corners.
void WriteGlyphQuad(const PlacedGlyph& glyph, core::Vec2 topLeft, f32 scale, u32 colour, std::span<MeshVertex> out) {
    constexpr auto PAGE = static_cast<f32>(GLYPH_PAGE_SIZE);
    const f32 left = topLeft.x + (glyph.x * scale);
    const f32 top = topLeft.y + (glyph.y * scale);
    const f32 right = left + (glyph.width * scale);
    const f32 bottom = top + (glyph.height * scale);
    const auto pageLeft = static_cast<f32>(glyph.u);
    const auto pageTop = static_cast<f32>(glyph.v);
    const u16 uLeft = core::PackUnorm16(pageLeft / PAGE);
    const u16 vTop = core::PackUnorm16(pageTop / PAGE);
    const u16 uRight = core::PackUnorm16((pageLeft + glyph.width) / PAGE);
    const u16 vBottom = core::PackUnorm16((pageTop + glyph.height) / PAGE);
    out[0] = { .x = left, .y = top, .uv = core::PackPair(uLeft, vTop), .colour = colour };
    out[1] = { .x = right, .y = top, .uv = core::PackPair(uRight, vTop), .colour = colour };
    out[2] = { .x = left, .y = bottom, .uv = core::PackPair(uLeft, vBottom), .colour = colour };
    out[3] = { .x = right, .y = bottom, .uv = core::PackPair(uRight, vBottom), .colour = colour };
}

// The characters `value` signed by `sign` shows, as indices into NUMBER_FACE_CHARACTERS, into `out`; returns how many.
std::size_t NumberCharacters(i32 value, core::NumberSign sign, std::array<u8, NUMBER_MAX_CHARACTERS>& out) {
    std::size_t count = 0;
    if (sign == core::NumberSign::Plus) {
        out.at(count++) = PLUS_CHARACTER;
    }
    if (value < 0) {
        out.at(count++) = MINUS_CHARACTER;
    }
    // In u32, where the magnitude of the smallest i32 fits.
    u32 magnitude = value < 0 ? 0U - static_cast<u32>(value) : static_cast<u32>(value);
    const std::size_t first = count;
    for (bool more = true; more; more = magnitude > 0) { // 0 is a digit too
        out.at(count++) = static_cast<u8>(magnitude % DECIMAL);
        magnitude /= DECIMAL;
    }
    std::ranges::reverse(std::span{ out }.subspan(first, count - first));
    return count;
}

// Whether `item` is a number PlaceNumbers() places.
bool PlacedAsNumber(const core::TextItem& item) {
    return item.kind == core::TextKind::Number && item.direction == core::TextDirection::LeftToRight && item.opacity > 0.0F;
}

// How many quads `item` places as.
std::size_t NumberQuads(const NumberFace& face, const core::TextItem& item) {
    if (!PlacedAsNumber(item)) {
        return 0;
    }
    std::array<u8, NUMBER_MAX_CHARACTERS> characters{};
    const std::size_t count = NumberCharacters(item.value, item.sign, characters);
    std::size_t quads = 0;
    for (std::size_t character = 0; character < count; ++character) {
        quads += face.characters.at(characters.at(character))->glyphs.size();
    }
    return quads;
}

// The quads PlaceNumbers() places in each chunk.
void CountNumberChunk(const NumberFace& face, std::span<const core::TextItem> texts, std::size_t& quads) {
    PN_PROFILE_ZONE();
    quads = 0;
    for (const core::TextItem& item : texts) {
        quads += NumberQuads(face, item);
    }
}

void PlaceNumberChunk(
    const NumberPlacement& placement,
    std::span<const core::TextItem> texts,
    std::span<MeshVertex> vertices,
    std::span<u32> pages
) {
    PN_PROFILE_ZONE();
    const f32 pixels = placement.pixels;
    const f32 scale = pixels / SDF_BASE_PIXELS;
    std::size_t quad = 0;
    for (const core::TextItem& item : texts) {
        if (!PlacedAsNumber(item)) {
            continue;
        }
        const auto origin = std::ranges::lower_bound(placement.origins, item.entity, {}, &NumberOrigin::entity);
        core::Vec2 start = origin != placement.origins.end() && origin->entity == item.entity
                               ? origin->start
                               : AnchorOfText(item, placement.clip, placement.alpha, placement.width, placement.height);
        start.x += item.scatter.x * pixels;
        start.y += (item.scatter.y - item.lift) * pixels;
        const ComposedNumber number = ComposeNumber(placement.face, item.value, item.sign);
        const core::Vec2 topLeft = TopLeftOfText(start, { .x = number.width * scale, .y = number.height * scale }, item.align);
        const u32 colour = ColourOfText(item, placement.outline, placement.shadow);
        for (const PlacedGlyph& glyph : std::span{ number.glyphs }.first(number.count)) {
            WriteGlyphQuad(glyph, topLeft, scale, colour, vertices.subspan(quad * QUAD_VERTICES, QUAD_VERTICES));
            pages[quad] = glyph.page;
            ++quad;
        }
    }
}

// Calls `body(chunk)` for each of `chunks`: on `jobs` when there are several, else on this thread, sparing a frame
// without numbers the wake of every worker. A chunk's output is the same wherever it runs.
template<typename Body>
void ForEachChunk(core::Jobs& jobs, u32 chunks, const Body& body) {
    if (chunks == 1) {
        body(0U);
    }
    else if (chunks > 1) {
        jobs.ParallelFor(chunks, body);
    }
}

} // namespace

f32 TextPixels(core::TextKind kind, f32 pixelsPerUnit, f32 scale) {
    PN_PROFILE_ZONE();
    if (kind == core::TextKind::Number) {
        return std::max(NUMBER_WORLD_SIZE * pixelsPerUnit, NUMBER_MIN_PIXELS) * scale;
    }
    return (kind == core::TextKind::Caption ? CAPTION_WORLD_SIZE : LABEL_WORLD_SIZE) * pixelsPerUnit * scale;
}

bool TooSmallToSee(f32 entityPixels) {
    return entityPixels > 0.0F && entityPixels < VISIBLE_CREATURE_PIXELS;
}

u32 TextColour(const core::TextItem& item, bool outline, bool shadow) {
    PN_PROFILE_ZONE();
    return ColourOfText(item, outline, shadow);
}

core::Vec2 TextAnchor(const core::TextItem& item, const View& view, u32 width, u32 height) {
    PN_PROFILE_ZONE();
    return AnchorOfText(item, WorldToClip(view.camera, width, height), static_cast<f32>(view.alpha), width, height);
}

core::Vec2 TextTopLeft(core::Vec2 anchor, core::Vec2 size, core::TextAlign align) {
    PN_PROFILE_ZONE();
    return TopLeftOfText(anchor, size, align);
}

void PlaceGlyphs(const Layout& layout, core::Vec2 topLeft, f32 pixels, u32 colour, std::vector<MeshVertex>& out) {
    PN_PROFILE_ZONE();
    const f32 scale = pixels / SDF_BASE_PIXELS;
    std::size_t first = out.size();
    out.resize(first + (layout.glyphs.size() * QUAD_VERTICES));
    for (const PlacedGlyph& glyph : layout.glyphs) {
        WriteGlyphQuad(glyph, topLeft, scale, colour, std::span{ out }.subspan(first, QUAD_VERTICES));
        first += QUAD_VERTICES;
    }
}

NumberFace NumberFaceOf(TextLayout& layout) {
    PN_PROFILE_ZONE();
    NumberFace face{};
    for (std::size_t character = 0; character < NUMBER_FACE_CHARACTERS.size(); ++character) {
        const Layout& laid = layout.Lay(NUMBER_FACE_CHARACTERS.substr(character, 1), core::TextDirection::LeftToRight);
        PN_ASSERT(laid.glyphs.size() <= 1, "a number's character laid out as more than one glyph");
        face.characters.at(character) = &laid;
    }
    return face;
}

ComposedNumber ComposeNumber(const NumberFace& face, i32 value, core::NumberSign sign) {
    std::array<u8, NUMBER_MAX_CHARACTERS> characters{};
    const std::size_t count = NumberCharacters(value, sign, characters);
    ComposedNumber number{};
    f32 pen = 0.0F;
    for (std::size_t index = 0; index < count; ++index) {
        const Layout& character = *face.characters.at(characters.at(index));
        for (const PlacedGlyph& glyph : character.glyphs) {
            PlacedGlyph& placed = number.glyphs.at(number.count++);
            placed = glyph;
            placed.x += pen;
        }
        number.complete = number.complete && character.complete;
        number.height = character.height;
        pen += character.width - TEXT_MARGINS;
    }
    number.width = pen + TEXT_MARGINS;
    return number;
}

void PlaceNumbers(
    std::span<const core::TextItem> texts,
    const NumberPlacement& placement,
    core::Jobs& jobs,
    std::vector<MeshVertex>& vertices,
    std::vector<u32>& pages
) {
    PN_PROFILE_ZONE();
    const u32 chunks = core::ChunkCount(texts.size(), NUMBER_CHUNK);
    const auto chunkOf = [texts](u32 chunk) {
        const std::size_t begin = std::size_t{ chunk } * NUMBER_CHUNK;
        return texts.subspan(begin, std::min<std::size_t>(NUMBER_CHUNK, texts.size() - begin));
    };
    // Counted first, so each chunk writes its own part of output sized for it.
    std::vector<std::size_t> firstQuads(std::size_t{ chunks } + 1);
    ForEachChunk(jobs, chunks, [&](u32 chunk) { CountNumberChunk(placement.face, chunkOf(chunk), firstQuads[chunk + 1]); });
    std::partial_sum(firstQuads.begin(), firstQuads.end(), firstQuads.begin());
    const std::size_t vertexBase = vertices.size();
    const std::size_t pageBase = pages.size();
    vertices.resize(vertexBase + (firstQuads.back() * QUAD_VERTICES));
    pages.resize(pageBase + firstQuads.back());
    const std::span<MeshVertex> newVertices = std::span{ vertices }.subspan(vertexBase);
    const std::span<u32> newPages = std::span{ pages }.subspan(pageBase);
    if (firstQuads.back() == 0) {
        return;
    }
    ForEachChunk(jobs, chunks, [&](u32 chunk) {
        const std::size_t first = firstQuads[chunk];
        const std::size_t quads = firstQuads[chunk + 1] - first;
        PlaceNumberChunk(
            placement,
            chunkOf(chunk),
            newVertices.subspan(first * QUAD_VERTICES, quads * QUAD_VERTICES),
            newPages.subspan(first, quads)
        );
    });
}

} // namespace gfx
