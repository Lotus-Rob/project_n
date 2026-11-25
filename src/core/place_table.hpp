#pragma once

/// @file
/// @ingroup core
/// Each city's cost to every place in reach of one profile, searched over its route graph once, so that asking what a
/// way between two places costs is a lookup. Whole numbers with fixed tie-breaks: the table is a function of the map, the
/// graph and the bound alone, the same whatever job system built it.

#include "core/paths.hpp"
#include "core/route_graph.hpp"
#include "core/types.hpp"
#include "core/world_map.hpp"

#include <cstddef>
#include <vector>

namespace core {

class Jobs;

/// The furthest cost anyone moving by `profile` will go: the reach at the next band's power for each band naming it,
/// UNREACHABLE (no bound) for the last band, and for WALKING with no bands; 0 for a profile no band names.
[[nodiscard]] u32 ReachBound(const TravelRules& rules, ProfileIndex profile);

/// How many cities one chunk of a parallel build searches from, all with one workspace.
inline constexpr u32 CITIES_PER_CHUNK = 4;

/// The cost from each city to each place of one profile's route graph, within a bound.
class PlaceTable {
public:
    /// One search of `graph`, the graph of its profile over `map`, per city, stopped past `bound`; on `jobs` when given.
    [[nodiscard]] static PlaceTable Build(
        const WorldMap& map,
        const RouteGraph& graph,
        u32 bound = UNREACHABLE,
        Jobs* jobs = nullptr
    );

    /// What the way from `city` to `place` costs: 0 for itself; UNREACHABLE past the bound, with no way, for a place
    /// with no node (a city with none included), or when `city` isn't a city.
    [[nodiscard]] u32 Cost(u16 city, u16 place) const;

    /// The bytes it holds: itself and its arrays, by their capacity.
    [[nodiscard]] std::size_t Bytes() const;

    /// Whether both hold the same costs.
    [[nodiscard]] friend bool operator==(const PlaceTable&, const PlaceTable&) = default;

private:
    /// How many places the map has: a row's length.
    std::size_t m_places = 0;
    /// Each place's row in m_costs, or NO_NODE for a place that is no city.
    std::vector<u32> m_row;
    /// The rows, one per city in place order, each a cost to every place; UNREACHABLE where there is none.
    std::vector<u32> m_costs;
};

} // namespace core
