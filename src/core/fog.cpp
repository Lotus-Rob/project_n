#include "core/fog.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

namespace core {

namespace {

constexpr std::size_t WORD_BITS = 64;

u32 Gap(u32 first, u32 second) {
    return first > second ? first - second : second - first;
}

} // namespace

bool IsExplored(std::span<const u64> bits, u32 tile) {
    const std::size_t word = tile / WORD_BITS;
    return word < bits.size() && ((bits[word] >> (tile % WORD_BITS)) & 1U) != 0;
}

u32 TileDistance(TilePoint first, TilePoint second) {
    return Gap(first.column, second.column) + Gap(first.row, second.row);
}

bool InSight(const WorldMap& map, u32 from, u32 tile, u32 radius) {
    return TileDistance(map.PointOf(from), map.PointOf(tile)) <= radius;
}

u32 ExploreAround(std::span<u64> bits, const WorldMap& map, u32 centre, u32 radius) {
    const std::size_t tiles = static_cast<std::size_t>(map.Columns()) * map.Rows();
    PN_ASSERT(bits.size() == FogWords(tiles), "the bitset holds one bit per tile of the map");
    u32 added = 0;
    ForEachInSight(map, centre, radius, [&](u32 tile) {
        const u64 bit = u64{ 1 } << (tile % WORD_BITS);
        u64& word = bits[tile / WORD_BITS];
        added += (word & bit) == 0 ? 1U : 0U;
        word |= bit;
    });
    return added;
}

bool FogBitsFit(std::span<const u64> bits, std::size_t tiles) {
    if (bits.size() != FogWords(tiles)) {
        return false;
    }
    const std::size_t stray = tiles % WORD_BITS;
    return stray == 0 || bits.empty() || (bits.back() >> stray) == 0;
}

} // namespace core
