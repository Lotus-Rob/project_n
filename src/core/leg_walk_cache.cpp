#include "core/leg_walk_cache.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/random.hpp"

namespace core {

namespace {

constexpr u32 HALF_SHIFT = 32;

/// The key `leg`'s table is kept under: a hash of what tells most routes apart at a glance, its origin, ends and length.
u64 KeyOf(const Leg& leg) {
    const u64 origin = (u64{ std::bit_cast<u32>(leg.origin.x) } << HALF_SHIFT) | std::bit_cast<u32>(leg.origin.y);
    const u64 ends = (u64{ leg.tiles.front() } << HALF_SHIFT) | leg.tiles.back();
    return detail::Mix(detail::Mix(detail::Mix(origin) ^ ends) ^ u64{ leg.tiles.size() });
}

} // namespace

LegWalkCache::LegWalkCache(const WorldMap& map)
  : m_map{ &map } {
}

const LegWalk& LegWalkCache::Of(const Leg& leg) {
    PN_ASSERT(!leg.tiles.empty(), "a leg has a tile to start on");
    const u64 key = KeyOf(leg);
    {
        const SharedLock lock{ m_mutex };
        if (const LegWalk* const kept = Find(key, leg)) {
            return *kept;
        }
    }
    // Made outside the lock, so the others keep finding theirs meanwhile. A table is a pure function of the leg's points
    // and the map, so whichever thread's is kept when two make one at once, it is the same table.
    auto made = std::make_unique<const LegWalk>(MakeLegWalk(*m_map, leg));
    const std::scoped_lock lock{ m_mutex };
    if (const LegWalk* const kept = Find(key, leg)) {
        return *kept;
    }
    return *m_walks.emplace(key, std::move(made))->second;
}

void LegWalkCache::Clear() {
    const std::scoped_lock lock{ m_mutex };
    m_walks.clear();
}

std::size_t LegWalkCache::Size() const {
    const SharedLock lock{ m_mutex };
    return m_walks.size();
}

const LegWalk* LegWalkCache::Find(u64 key, const Leg& leg) const {
    const auto [first, last] = m_walks.equal_range(key);
    for (auto kept = first; kept != last; ++kept) {
        if (kept->second->Fits(leg)) {
            return kept->second.get();
        }
    }
    return nullptr;
}

} // namespace core
