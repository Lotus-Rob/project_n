#include "core/paths.hpp"

#include "core/assert.hpp"

namespace core {

namespace {

using RouteEntry = std::pair<u64, u32>; // (cost or f, tile)
using RouteQueue = std::priority_queue<RouteEntry, std::vector<RouteEntry>, std::greater<>>;

/// The cheapest cost of entering any tile by `profile`: every tile's cost is one of its kinds' or features' costs, or a
/// cap of the terrain's by one, so none is lower. 0 when nothing is passable.
u32 CheapestEntry(const MovementProfile& profile) {
    u32 cheapest = IMPASSABLE_COST;
    for (const u16 cost : profile.kindCosts) {
        cheapest = std::min<u32>(cheapest, cost);
    }
    for (const u16 cost : { profile.features.road, profile.features.river, profile.features.bridge, profile.features.ford }) {
        cheapest = std::min<u32>(cheapest, cost);
    }
    return cheapest == IMPASSABLE_COST ? 0 : cheapest;
}

/// The cost of crossing `dColumn` by `dRow` tiles at best, with `cheapest` the least a step may cost: straight steps for
/// the difference, diagonals (seven fifths, rounded down as StepCost does) for the rest.
u64 Estimate(u32 cheapest, i64 dColumn, i64 dRow) {
    const u64 along = static_cast<u64>(std::abs(dColumn));
    const u64 across = static_cast<u64>(std::abs(dRow));
    const u64 shorter = std::min(along, across);
    const u64 longer = std::max(along, across);
    return (u64{ cheapest } * (longer - shorter)) + (u64{ cheapest } * DIAGONAL_NUMERATOR / DIAGONAL_DENOMINATOR * shorter);
}

} // namespace

DistanceMap DistanceMap::Build(const WorldMap& map, u32 goal, ProfileIndex profile) {
    std::vector<u32> cost(static_cast<std::size_t>(map.Columns()) * map.Rows(), UNREACHABLE);
    cost.at(goal) = 0;
    RouteQueue queue;
    queue.emplace(0, goal);
    while (!queue.empty()) {
        const auto [reached, tile] = queue.top();
        queue.pop();
        if (reached > cost[tile]) {
            continue; // a cheaper entry settled this tile
        }
        map.ForEachNeighbour(tile, [&](u32 next) {
            const u32 step = map.StepCost(next, tile, profile);
            if (step == IMPASSABLE_COST) {
                return;
            }
            const u64 total = reached + step;
            if (total < cost[next]) {
                cost[next] = static_cast<u32>(total);
                queue.emplace(total, next);
            }
        });
    }
    // Parentheses, not braces: MSVC can't promise left-to-right order for a braced list holding a moved vector.
    DistanceMap distances(std::move(cost), goal, profile);
    return distances;
}

std::vector<u32> RouteDown(const WorldMap& map, const DistanceMap& distances, u32 from) {
    if (distances.CostFrom(from) == UNREACHABLE) {
        return {};
    }
    std::vector<u32> route{ from };
    const std::size_t most = static_cast<std::size_t>(map.Columns()) * map.Rows();
    u32 tile = from;
    while (tile != distances.Goal() && route.size() <= most) {
        u32 best = UNREACHABLE;
        map.ForEachNeighbour(tile, [&](u32 next) {
            if (best != UNREACHABLE || distances.CostFrom(next) == UNREACHABLE) {
                return; // neighbours come in tile order: the first that fits is the lowest
            }
            const u32 step = map.StepCost(tile, next, distances.Profile());
            if (step != IMPASSABLE_COST && step + distances.CostFrom(next) == distances.CostFrom(tile)) {
                best = next;
            }
        });
        PN_ASSERT(best != UNREACHABLE, "a reachable tile has a neighbour one step down");
        if (best == UNREACHABLE) {
            route.clear();
            break;
        }
        tile = best;
        route.push_back(tile);
    }
    PN_ASSERT(route.empty() || tile == distances.Goal(), "a descent along a distance map ends on its goal");
    if (tile != distances.Goal()) {
        route.clear();
    }
    return route;
}

namespace {

/// A* from `from` to `target` over the tiles `profile` can step onto, each step priced by `price(from, next)` (never
/// IMPASSABLE_COST for a step `profile` can take), `cheapest` the least any step costs, which keeps the estimate from
/// overshooting. Ties go to the lower tile index.
template<typename Price>
std::vector<u32> Search(const WorldMap& map, ProfileIndex profile, u32 from, u32 target, u32 cheapest, const Price& price) {
    if (map.CostAt(target, profile) == IMPASSABLE_COST) {
        return {};
    }
    const std::size_t tiles = static_cast<std::size_t>(map.Columns()) * map.Rows();
    const TilePoint targetPoint = map.PointOf(target);
    const auto estimate = [&](u32 tile) {
        const TilePoint point = map.PointOf(tile);
        return Estimate(
            cheapest,
            static_cast<i64>(targetPoint.column) - static_cast<i64>(point.column),
            static_cast<i64>(targetPoint.row) - static_cast<i64>(point.row)
        );
    };
    std::vector<u32> cost(tiles, UNREACHABLE);
    std::vector<u32> parent(tiles, UNREACHABLE);
    cost.at(from) = 0;
    RouteQueue queue; // (f, tile)
    queue.emplace(estimate(from), from);
    while (!queue.empty()) {
        const auto [estimated, tile] = queue.top();
        queue.pop();
        if (estimated > u64{ cost[tile] } + estimate(tile)) {
            continue; // a cheaper way to this tile was found after this entry
        }
        if (tile == target) {
            std::vector<u32> route{ target };
            while (route.back() != from) {
                route.push_back(parent.at(route.back()));
            }
            std::ranges::reverse(route);
            return route;
        }
        map.ForEachNeighbour(tile, [&](u32 next) {
            const u32 step = price(tile, next);
            if (step == IMPASSABLE_COST) {
                return;
            }
            const u64 total = u64{ cost[tile] } + step;
            if (total < cost[next]) {
                cost[next] = static_cast<u32>(total);
                parent[next] = tile;
                queue.emplace(total + estimate(next), next);
            }
        });
    }
    return {};
}

/// The price of a straight step in FindShortestRoute: a multiple of DIAGONAL_DENOMINATOR, so the diagonal's price
/// (this times the numerator over the denominator) is exact and the two stay in the diagonal's ratio.
constexpr u32 SHORTEST_STEP = 5;
static_assert(SHORTEST_STEP % DIAGONAL_DENOMINATOR == 0, "a diagonal's price is a whole number of a straight one's");

} // namespace

std::vector<u32> FindRoute(const WorldMap& map, ProfileIndex profile, u32 from, u32 target) {
    return Search(map, profile, from, target, CheapestEntry(map.Profiles()[profile]), [&](u32 tile, u32 next) {
        return map.StepCost(tile, next, profile);
    });
}

std::vector<u32> FindShortestRoute(const WorldMap& map, u32 from, u32 target) {
    return Search(map, WALKING, from, target, SHORTEST_STEP, [&](u32 tile, u32 next) -> u32 {
        if (map.StepCost(tile, next, WALKING) == IMPASSABLE_COST) {
            return IMPASSABLE_COST;
        }
        const TilePoint start = map.PointOf(tile);
        const TilePoint end = map.PointOf(next);
        return start.column != end.column && start.row != end.row ? SHORTEST_STEP * DIAGONAL_NUMERATOR / DIAGONAL_DENOMINATOR
                                                                  : SHORTEST_STEP;
    });
}

Paths::Paths(std::shared_ptr<const WorldMap> map)
  : m_map{ std::move(map) }
  , m_maps(m_map->Profiles().size()) {
    for (auto& slots : m_maps) {
        slots.resize(m_map->Places().size());
    }
}

const DistanceMap& Paths::To(u16 city, ProfileIndex profile) {
    PN_ASSERT(city < m_map->Places().size(), "a distance map is asked for a place the map has");
    PN_ASSERT(m_map->Places()[city].kind == PlaceKind::City, "a distance map is kept for cities only");
    std::optional<DistanceMap>& slot = m_maps.at(profile).at(city);
    if (!slot) {
        slot = DistanceMap::Build(*m_map, m_map->TileIndex(m_map->Places()[city].tile), profile);
    }
    return *slot;
}

void Paths::Warm(ProfileIndex profile) {
    for (std::size_t place = 0; place < m_map->Places().size(); ++place) {
        if (m_map->Places()[place].kind == PlaceKind::City) {
            static_cast<void>(To(static_cast<u16>(place), profile));
        }
    }
}

} // namespace core
