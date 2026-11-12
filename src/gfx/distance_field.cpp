#include "gfx/distance_field.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

#include "core/manifest.hpp"
#include "gfx/sprite_block.hpp"

namespace gfx {

namespace {

constexpr std::size_t ALPHA_OFFSET = 3;
constexpr u8 INSIDE_ALPHA = 128;
constexpr f32 ENCODED_SPAN = 2.0F * core::FIELD_REACH_TEXELS; // texels from the lowest byte to the highest
constexpr f32 HALF_TEXEL = 0.5F;
constexpr f32 EDGE_UNIT = 0.5F; // where the edge sits in the encoded range from 0 to 1: its middle
constexpr f32 PARABOLA_DENOMINATOR_SCALE = 2.0F;
constexpr f32 ENCODED_MAX = 255.0F;
constexpr f32 INFINITE = std::numeric_limits<f32>::infinity();
// Stands for "no such texel": finite, so that the parabolas' intersections stay numbers, and far past any
// squared distance in a block.
constexpr f32 FAR = 1.0e10F;

// Work space for one line of the transform, sized for the longest.
struct Scratch {
    std::vector<f32> column;
    std::vector<std::size_t> hull;
    std::vector<f32> starts;
    std::vector<f32> line;
};

// Felzenszwalb and Huttenlocher's lower envelope of the parabolas f[q] + (x - q)^2, in place along `values`, one line.
// `scratch` holds the envelope's parabolas and where each starts.
void Transform1D(std::span<f32> values, Scratch& scratch) {
    const std::size_t lineLength = values.size();
    const auto valueAt = [&](std::size_t index) { return values[index]; };
    std::size_t last = 0;
    scratch.hull[0] = 0;
    scratch.starts[0] = -INFINITE;
    scratch.starts[1] = INFINITE;
    for (std::size_t parabola = 1; parabola < lineLength; ++parabola) {
        const auto fromQ = static_cast<f32>(parabola);
        f32 cross = 0.0F;
        while (true) {
            const std::size_t other = scratch.hull[last];
            const auto fromP = static_cast<f32>(other);
            cross = ((valueAt(parabola) + (fromQ * fromQ)) - (valueAt(other) + (fromP * fromP))) /
                    (PARABOLA_DENOMINATOR_SCALE * (fromQ - fromP));
            if (cross > scratch.starts[last]) {
                break;
            }
            --last; // the first parabola starts at -infinity, so this stops before it underflows
        }
        ++last;
        scratch.hull[last] = parabola;
        scratch.starts[last] = cross;
        scratch.starts[last + 1] = INFINITE;
    }
    std::size_t active = 0;
    for (std::size_t index = 0; index < lineLength; ++index) {
        while (scratch.starts[active + 1] < static_cast<f32>(index)) {
            ++active;
        }
        const auto delta = static_cast<f32>(index) - static_cast<f32>(scratch.hull[active]);
        scratch.line[index] = (delta * delta) + valueAt(scratch.hull[active]);
    }
    for (std::size_t index = 0; index < lineLength; ++index) {
        values[index] = scratch.line[index];
    }
}

// Squared distances from every texel to the nearest texel whose `set` entry is true (FAR-ish when none).
std::vector<f32> SquaredDistances(const std::vector<bool>& set, std::size_t width, std::size_t height) {
    std::vector<f32> grid(width * height);
    for (std::size_t i = 0; i < grid.size(); ++i) {
        grid[i] = set[i] ? 0.0F : FAR;
    }
    const std::size_t longest = std::max(width, height);
    Scratch scratch{ .column = std::vector<f32>(longest),
                     .hull = std::vector<std::size_t>(longest + 1),
                     .starts = std::vector<f32>(longest + 2),
                     .line = std::vector<f32>(longest) };
    for (std::size_t column = 0; column < width; ++column) {
        for (std::size_t row = 0; row < height; ++row) {
            scratch.column[row] = grid[(row * width) + column];
        }
        Transform1D(std::span{ scratch.column }.first(height), scratch);
        for (std::size_t row = 0; row < height; ++row) {
            grid[(row * width) + column] = scratch.column[row];
        }
    }
    for (std::size_t row = 0; row < height; ++row) {
        Transform1D(std::span{ grid }.subspan(row * width, width), scratch);
    }
    return grid;
}

} // namespace

u8 EncodeDistance(f32 texels) {
    const f32 unit = std::clamp(EDGE_UNIT + (texels / ENCODED_SPAN), 0.0F, 1.0F);
    return static_cast<u8>(std::lround(ENCODED_MAX * unit));
}

std::vector<u8> DistanceField(std::span<const std::byte> rgba, std::size_t pitch, u32 width, u32 height) {
    constexpr std::size_t PAD = core::FIELD_REACH_TEXELS;
    const std::size_t blockWidth = width + (2 * PAD);
    const std::size_t blockHeight = height + (2 * PAD);
    std::vector<bool> inside(blockWidth * blockHeight);
    std::vector<bool> outside(blockWidth * blockHeight, true);
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t column = 0; column < width; ++column) {
            const auto alpha = std::to_integer<u8>(rgba[(row * pitch) + (column * BYTES_PER_TEXEL) + ALPHA_OFFSET]);
            if (alpha >= INSIDE_ALPHA) {
                const std::size_t texel = ((row + PAD) * blockWidth) + column + PAD;
                inside[texel] = true;
                outside[texel] = false;
            }
        }
    }
    const std::vector<f32> toInside = SquaredDistances(inside, blockWidth, blockHeight);
    const std::vector<f32> toOutside = SquaredDistances(outside, blockWidth, blockHeight);
    std::vector<u8> field(blockWidth * blockHeight);
    for (std::size_t i = 0; i < field.size(); ++i) {
        const f32 signedDistance = inside[i] ? -(std::sqrt(toOutside[i]) - HALF_TEXEL) : std::sqrt(toInside[i]) - HALF_TEXEL;
        field[i] = EncodeDistance(signedDistance);
    }
    return field;
}

} // namespace gfx
