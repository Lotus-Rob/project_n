#include "gfx/ribbon_band.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace gfx {

namespace {

constexpr u32 BYTE_BITS = 8;
constexpr u32 BYTE_MASK = 0xFFU;

/// The premultiplied colour `colour` (RibbonVertex::colour's layout) at `share` of its strength, from 0 to 1.
u32 Faded(u32 colour, f32 share) {
    u32 faded = 0;
    for (u32 shift = 0; shift < 4 * BYTE_BITS; shift += BYTE_BITS) {
        const auto channel = static_cast<f32>((colour >> shift) & BYTE_MASK);
        faded |= static_cast<u32>(std::lround(channel * share)) << shift;
    }
    return faded;
}

/// How much of the band shows at each point of `laid`'s line: all of it, but over the last `fade` world units before its
/// end, where it falls to nothing at the end.
std::vector<f32> Shares(const Laid& laid) {
    std::vector<f32> shares(laid.line.size(), 1.0F);
    f32 fromEnd = 0.0F;
    for (std::size_t point = laid.line.size() - 1; laid.fade > 0.0F && point > 0; --point) {
        shares[point] = std::min(1.0F, fromEnd / laid.fade);
        fromEnd += Length(laid.line[point] - laid.line[point - 1]);
    }
    return shares;
}

} // namespace

void AddBand(const Laid& laid, f32 width, u32 colour, std::vector<RibbonVertex>& into) {
    const std::vector<core::Vec2>& line = laid.line;
    const std::size_t last = line.size() - 1;
    std::vector<core::Vec2> lefts;
    std::vector<core::Vec2> rights;
    lefts.reserve(line.size());
    rights.reserve(line.size());
    core::Vec2 heading{ .x = 1.0F, .y = 0.0F };
    for (std::size_t point = 0; point <= last; ++point) {
        // From the neighbours, so the band bends with the line without twisting; a stall keeps the last direction.
        const core::Vec2 runs = line[std::min(point + 1, last)] - line[point == 0 ? 0 : point - 1];
        heading = HeadingOf(runs, heading);
        const core::Vec2 side = SideOf(heading, width);
        lefts.push_back(line[point] + side);
        rights.push_back(line[point] - side);
    }
    const std::size_t first = into.size();
    into.resize(first + (last * QUAD_VERTICES));
    for (std::size_t quad = 0; quad < last; ++quad) {
        WriteRibbonQuad(
            std::span{ into }.subspan(first + (quad * QUAD_VERTICES), QUAD_VERTICES),
            { .left = lefts[quad], .right = rights[quad], .along = static_cast<f32>(quad) / static_cast<f32>(last) },
            { .left = lefts[quad + 1],
              .right = rights[quad + 1],
              .along = static_cast<f32>(quad + 1) / static_cast<f32>(last) },
            colour
        );
    }
    if (laid.fade <= 0.0F) {
        return;
    }
    const std::vector<f32> shares = Shares(laid);
    for (std::size_t quad = 0; quad < last; ++quad) {
        const std::span<RibbonVertex> corners = std::span{ into }.subspan(first + (quad * QUAD_VERTICES), QUAD_VERTICES);
        // Corners 0 and 1 stand at the quad's near end, 2 and 3 at its far end (WriteRibbonQuad()).
        corners[0].colour = Faded(colour, shares[quad]);
        corners[1].colour = corners[0].colour;
        corners[2].colour = Faded(colour, shares[quad + 1]);
        corners[3].colour = corners[2].colour;
    }
}

} // namespace gfx
