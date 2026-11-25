#pragma once

/// @file
/// @ingroup core
/// The walk tables of the legs walked on one map, kept and shared between journeys, so the many travellers along one
/// route have its legs walked through once rather than each time one is drawn. Presentation only: nothing saved, hashed
/// or simulated reads it, and it lives no longer than the map it was made over.

#include "core/journey.hpp"
#include "core/thread_safety.hpp"
#include "core/types.hpp"
#include "core/world_map.hpp"

#include <cstddef>
#include <memory>
#include <unordered_map>

namespace core {

/// The walk tables (core::LegWalk) of legs on one map, each made the first time a leg over its points asks for it and
/// shared by every leg over the same points after: as many as the distinct legs asked for, however many journeys walk
/// them. Several threads may ask at once; finding a table kept takes a shared lock, so they never wait on one another
/// but while a new table is put in. With the whole shipped map in view and the debug sight on, a month draws about
/// 2,600 distinct legs.
class LegWalkCache final : public LegWalks {
public:
    /// Keeps the tables of legs on `map`, which outlives it.
    explicit LegWalkCache(const WorldMap& map);

    /// The table of `leg`'s points, a leg on the map: the one kept when some leg over the same points asked before (its
    /// origin and tiles compared whole), else made and kept. Valid for the cache's life.
    [[nodiscard]] const LegWalk& Of(const Leg& leg) override PN_REQUIRES(!m_mutex);

    /// Drops every table kept. Only while no other thread asks for one and nothing holds one it gave.
    void Clear() PN_REQUIRES(!m_mutex);

    /// How many tables it keeps.
    [[nodiscard]] std::size_t Size() const PN_REQUIRES(!m_mutex);

private:
    /// The table kept for `leg` under `key`, or null.
    [[nodiscard]] const LegWalk* Find(u64 key, const Leg& leg) const PN_REQUIRES_SHARED(m_mutex);

    const WorldMap* m_map;
    mutable SharedMutex m_mutex;
    // Keyed by a hash of a leg's origin, ends and length, so routes alike in those share a key and are told apart whole.
    std::unordered_multimap<u64, std::unique_ptr<const LegWalk>> m_walks PN_GUARDED_BY(m_mutex);
};

} // namespace core
