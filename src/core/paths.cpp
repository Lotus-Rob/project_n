#include "core/paths.hpp"

#include "core/assert.hpp"

namespace core {

namespace {

using RouteEntry = std::pair<u64, u32>; // (cost, tile)
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
        const u32 here = distances.CostFrom(tile);
        map.ForEachNeighbour(tile, [&](u32 next) {
            // Neighbours come in tile order, so the first that fits is the lowest. Only one cheaper than here can be a step
            // down (a step costs at least 1), so the step's cost, the dear part, is asked only of those.
            const u32 there = distances.CostFrom(next);
            if (best != UNREACHABLE || there >= here) {
                return;
            }
            const u32 step = map.StepCost(tile, next, distances.Profile());
            if (step != IMPASSABLE_COST && step + there == here) {
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

/// A search entry: (estimated length, aside so far, tile), so the queue pops by length, then aside, then tile index.
using SearchEntry = std::tuple<u64, u64, u32>;
using SearchQueue = std::priority_queue<SearchEntry, std::vector<SearchEntry>, std::greater<>>;

/// A* from `from` to `target` over the tiles `profile` can step onto, each step priced by `price(from, next)` (never
/// IMPASSABLE_COST for a step `profile` can take), `cheapest` the least any step costs, which keeps the estimate from
/// overshooting. Among routes of the least price it takes the one whose entered tiles' `aside(tile)` sum least, compared
/// only on equal price; ties beyond that go to the lower tile index.
template<typename Price, typename Aside>
std::vector<u32> Search(
    const WorldMap& map,
    ProfileIndex profile,
    u32 from,
    u32 target,
    u32 cheapest,
    const Price& price,
    const Aside& aside
) {
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
    std::vector<u64> astray(tiles, std::numeric_limits<u64>::max());
    std::vector<u32> parent(tiles, UNREACHABLE);
    cost.at(from) = 0;
    astray.at(from) = 0;
    SearchQueue queue;
    queue.emplace(estimate(from), 0, from);
    while (!queue.empty()) {
        const auto [estimated, strayed, tile] = queue.top();
        queue.pop();
        if (std::pair{ estimated, strayed } > std::pair{ u64{ cost[tile] } + estimate(tile), astray[tile] }) {
            continue; // a better way to this tile was found after this entry
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
            const u64 off = astray[tile] + aside(next);
            // The aside takes part in the relaxation, not only in the queue's order, so a later way of the same price
            // that strays less replaces the parent the first one set.
            if (std::pair{ total, off } < std::pair{ u64{ cost[next] }, astray[next] }) {
                cost[next] = static_cast<u32>(total);
                astray[next] = off;
                parent[next] = tile;
                queue.emplace(total + estimate(next), off, next);
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
    return Search(
        map,
        profile,
        from,
        target,
        CheapestEntry(map.Profiles()[profile]),
        [&](u32 tile, u32 next) { return map.StepCost(tile, next, profile); },
        [](u32 /*tile*/) { return u64{ 0 }; }
    );
}

std::vector<u32> FindShortestRoute(const WorldMap& map, u32 from, u32 target) {
    const TilePoint start = map.PointOf(from);
    const TilePoint end = map.PointOf(target);
    const i64 alongColumns = static_cast<i64>(end.column) - static_cast<i64>(start.column);
    const i64 alongRows = static_cast<i64>(end.row) - static_cast<i64>(start.row);
    const auto price = [&](u32 tile, u32 next) -> u32 {
        if (map.StepCost(tile, next, WALKING) == IMPASSABLE_COST) {
            return IMPASSABLE_COST;
        }
        const TilePoint stepFrom = map.PointOf(tile);
        const TilePoint stepTo = map.PointOf(next);
        return stepFrom.column != stepTo.column && stepFrom.row != stepTo.row
                   ? SHORTEST_STEP * DIAGONAL_NUMERATOR / DIAGONAL_DENOMINATOR
                   : SHORTEST_STEP;
    };
    // The cross product with the line from `from` to `target`: the tile's distance off the line times the line's length,
    // the same factor for every tile, so whole numbers order the tiles as their distances do.
    const auto aside = [&](u32 tile) -> u64 {
        const TilePoint point = map.PointOf(tile);
        const i64 column = static_cast<i64>(point.column) - static_cast<i64>(start.column);
        const i64 row = static_cast<i64>(point.row) - static_cast<i64>(start.row);
        return static_cast<u64>(std::abs((alongColumns * row) - (alongRows * column)));
    };
    return Search(map, WALKING, from, target, SHORTEST_STEP, price, aside);
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
