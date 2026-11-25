#include "core/route_graph.hpp"

#include "core/assert.hpp"
#include "core/profile.hpp"

namespace core {

namespace {

/// The most neighbours a tile has: each tile a walk settles pushes at most this many entries.
constexpr std::size_t MOST_NEIGHBOURS = 8;

/// The pairs of tiles side by side across one border: pair `offset` is `firstA + offset * stride` on the west or north
/// side and `firstB + offset * stride` on the east or south one.
struct Border {
    /// The first pair's west or north tile.
    u32 firstA = 0;
    /// The first pair's east or south tile.
    u32 firstB = 0;
    /// The step in tile index from one pair to the next.
    u32 stride = 0;
    /// How many pairs it has.
    u32 length = 0;
};

/// The tiles in `span`.
std::size_t TilesIn(TileSpan span) {
    return static_cast<std::size_t>(span.lastColumn - span.firstColumn + 1) * (span.lastRow - span.firstRow + 1);
}

/// Calls `visit(a, b)` with each gate's pair of `border` by `profile`, in offset order: each run of pairs whose tiles
/// the profile can both enter is cut into as few pieces as keep each within `gateSpacing` pairs, and each piece's gate is
/// its cheapest pair to enter both ways, the lowest offset on a tie. The pairs alone decide, so the sectors either side
/// find the same gates.
template<typename Visit>
void ForEachGate(const WorldMap& map, ProfileIndex profile, u32 gateSpacing, Border border, const Visit& visit) {
    const auto pairCost = [&](u32 offset) -> u32 {
        const u32 costA = map.CostAt(border.firstA + (offset * border.stride), profile);
        const u32 costB = map.CostAt(border.firstB + (offset * border.stride), profile);
        return costA == IMPASSABLE_COST || costB == IMPASSABLE_COST ? UNREACHABLE : costA + costB;
    };
    u32 offset = 0;
    while (offset < border.length) {
        if (pairCost(offset) == UNREACHABLE) {
            ++offset;
            continue;
        }
        u32 end = offset + 1;
        while (end < border.length && pairCost(end) != UNREACHABLE) {
            ++end;
        }
        const u64 run = end - offset;
        const u64 pieces = (run + gateSpacing - 1) / gateSpacing;
        for (u64 piece = 0; piece < pieces; ++piece) {
            const auto first = static_cast<u32>(offset + (piece * run / pieces));
            const auto last = static_cast<u32>(offset + ((piece + 1) * run / pieces));
            u32 best = first;
            u32 bestCost = pairCost(first);
            for (u32 at = first + 1; at < last; ++at) {
                const u32 cost = pairCost(at);
                if (cost < bestCost) {
                    best = at;
                    bestCost = cost;
                }
            }
            visit(border.firstA + (best * border.stride), border.firstB + (best * border.stride));
        }
        offset = end;
    }
}

/// A crossing found on a border before its sector's nodes are final.
struct PendingCrossing {
    /// Its node's tile.
    u32 tile = 0;
    /// Whether it crosses the south border rather than the east one.
    bool south = false;
    /// What stepping over costs.
    u32 cost = 0;
};

/// The index of `tile` in `nodes`, which holds it.
u16 NodeIndex(const std::vector<u32>& nodes, u32 tile) {
    const auto found = std::ranges::lower_bound(nodes, tile);
    PN_ASSERT(found != nodes.end() && *found == tile, "a gate's tile is a node of its sector");
    return static_cast<u16>(found - nodes.begin());
}

} // namespace

u32 SectorColumns(const WorldMap& map, RoutingRules rules) {
    PN_ASSERT(rules.sectorSize > 0, "a sector holds tiles");
    return (map.Columns() + rules.sectorSize - 1) / rules.sectorSize;
}

u32 SectorRows(const WorldMap& map, RoutingRules rules) {
    PN_ASSERT(rules.sectorSize > 0, "a sector holds tiles");
    return (map.Rows() + rules.sectorSize - 1) / rules.sectorSize;
}

TileSpan SectorSpan(const WorldMap& map, RoutingRules rules, u32 sector) {
    const u32 columns = SectorColumns(map, rules);
    PN_ASSERT(sector < columns * SectorRows(map, rules), "a sector of the map");
    const u32 firstColumn = (sector % columns) * rules.sectorSize;
    const u32 firstRow = (sector / columns) * rules.sectorSize;
    return { .firstColumn = firstColumn,
             .lastColumn = std::min(firstColumn + rules.sectorSize, map.Columns()) - 1,
             .firstRow = firstRow,
             .lastRow = std::min(firstRow + rules.sectorSize, map.Rows()) - 1 };
}

u32 SectorOf(const WorldMap& map, RoutingRules rules, u32 tile) {
    PN_ASSERT(rules.sectorSize > 0, "a sector holds tiles");
    const TilePoint point = map.PointOf(tile);
    return ((point.row / rules.sectorSize) * SectorColumns(map, rules)) + (point.column / rules.sectorSize);
}

void SectorWalk::WalkFrom(const WorldMap& map, ProfileIndex profile, TileSpan span, u32 tile) {
    Walk(map, profile, span, tile, false);
}

void SectorWalk::WalkTo(const WorldMap& map, ProfileIndex profile, TileSpan span, u32 tile) {
    Walk(map, profile, span, tile, true);
}

void SectorWalk::Walk(const WorldMap& map, ProfileIndex profile, TileSpan span, u32 tile, bool towards) {
    PN_ASSERT(
        span.firstColumn <= span.lastColumn && span.lastColumn < map.Columns() && span.firstRow <= span.lastRow &&
            span.lastRow < map.Rows(),
        "a walk's span is a block of the map's tiles"
    );
    const std::size_t tiles = TilesIn(span);
    if (m_stamp.size() < tiles) {
        m_cost.resize(tiles);
        m_step.resize(tiles);
        m_stamp.resize(tiles, 0);
        // A tile is pushed only by a strictly cheaper way from a tile settled once, so this bounds the queue.
        m_queue.reserve((tiles * MOST_NEIGHBOURS) + 1);
    }
    ++m_walk;
    if (m_walk == 0) {
        // The stamps wrapped: an entry last reached four billion walks ago would read as reached by this one.
        std::ranges::fill(m_stamp, 0);
        m_walk = 1;
    }
    m_span = span;
    m_columns = map.Columns();
    m_tile = tile;
    m_towards = towards;
    PN_ASSERT(Inside(tile), "a walk starts inside its span");

    const std::size_t start = Local(tile);
    m_stamp[start] = m_walk;
    m_cost[start] = 0;
    m_queue.clear();
    m_queue.emplace_back(0, tile);
    while (!m_queue.empty()) {
        std::ranges::pop_heap(m_queue, std::greater<>{});
        const auto [reached, current] = m_queue.back();
        m_queue.pop_back();
        if (reached > m_cost[Local(current)]) {
            continue; // a cheaper entry settled this tile
        }
        map.ForEachNeighbour(current, [&](u32 next) {
            if (!Inside(next)) {
                return;
            }
            const u32 step = towards ? map.StepCost(next, current, profile) : map.StepCost(current, next, profile);
            if (step == IMPASSABLE_COST) {
                return;
            }
            const u64 total = reached + step;
            const std::size_t local = Local(next);
            if (m_stamp[local] != m_walk || total < m_cost[local]) {
                m_stamp[local] = m_walk;
                m_cost[local] = static_cast<u32>(total);
                m_step[local] = current;
                m_queue.emplace_back(total, next);
                std::ranges::push_heap(m_queue, std::greater<>{});
            }
        });
    }
}

bool SectorWalk::Inside(u32 tile) const {
    if (m_columns == 0) {
        return false;
    }
    const u32 column = tile % m_columns;
    const u32 row = tile / m_columns;
    return column >= m_span.firstColumn && column <= m_span.lastColumn && row >= m_span.firstRow && row <= m_span.lastRow;
}

std::size_t SectorWalk::Local(u32 tile) const {
    const u32 column = tile % m_columns;
    const u32 row = tile / m_columns;
    const std::size_t width = m_span.lastColumn - m_span.firstColumn + 1;
    return ((row - m_span.firstRow) * width) + (column - m_span.firstColumn);
}

u32 SectorWalk::Cost(u32 tile) const {
    if (!Inside(tile)) {
        return UNREACHABLE;
    }
    const std::size_t local = Local(tile);
    return m_stamp[local] == m_walk ? m_cost[local] : UNREACHABLE;
}

void SectorWalk::Way(u32 tile, std::vector<u32>& into) const {
    into.clear();
    if (Cost(tile) == UNREACHABLE) {
        return;
    }
    into.push_back(tile);
    const std::size_t most = TilesIn(m_span);
    for (u32 at = tile; at != m_tile && into.size() <= most;) {
        at = m_step[Local(at)];
        into.push_back(at);
    }
    PN_ASSERT(into.back() == m_tile, "a way's steps lead to its walk's tile");
    if (into.back() != m_tile) {
        into.clear();
        return;
    }
    // The steps lead towards the walk's tile, which a walk from it reaches last.
    if (!m_towards) {
        std::ranges::reverse(into);
    }
}

SectorGraph BuildSector(const WorldMap& map, ProfileIndex profile, RoutingRules rules, u32 sector, SectorWalk& walk) {
    PN_PROFILE_ZONE();
    PN_ASSERT(rules.gateSpacing > 0, "a gate serves some of its border");
    const TileSpan span = SectorSpan(map, rules, sector);
    const u32 columns = map.Columns();
    const u32 width = span.lastColumn - span.firstColumn + 1;
    const u32 height = span.lastRow - span.firstRow + 1;
    const u32 topLeft = map.TileIndex({ .column = span.firstColumn, .row = span.firstRow });

    SectorGraph graph;
    // A crossing's node index is known only once every node is found, sorted and merged, so its tile waits here.
    std::vector<PendingCrossing> pending;
    const auto addEastOrSouthTile = [&](u32 /*westOrNorth*/, u32 eastOrSouth) { graph.nodes.push_back(eastOrSouth); };
    const auto addCrossing = [&](bool south) {
        return [&graph, &pending, &map, profile, south](u32 inner, u32 outer) {
            graph.nodes.push_back(inner);
            pending.push_back({ .tile = inner, .south = south, .cost = map.CostAt(outer, profile) });
        };
    };
    if (span.firstColumn > 0) {
        const Border west{ .firstA = topLeft - 1, .firstB = topLeft, .stride = columns, .length = height };
        ForEachGate(map, profile, rules.gateSpacing, west, addEastOrSouthTile);
    }
    if (span.lastColumn + 1 < columns) {
        const u32 topRight = map.TileIndex({ .column = span.lastColumn, .row = span.firstRow });
        const Border east{ .firstA = topRight, .firstB = topRight + 1, .stride = columns, .length = height };
        ForEachGate(map, profile, rules.gateSpacing, east, addCrossing(false));
    }
    if (span.firstRow > 0) {
        const Border north{ .firstA = topLeft - columns, .firstB = topLeft, .stride = 1, .length = width };
        ForEachGate(map, profile, rules.gateSpacing, north, addEastOrSouthTile);
    }
    if (span.lastRow + 1 < map.Rows()) {
        const u32 bottomLeft = map.TileIndex({ .column = span.firstColumn, .row = span.lastRow });
        const Border south{ .firstA = bottomLeft, .firstB = bottomLeft + columns, .stride = 1, .length = width };
        ForEachGate(map, profile, rules.gateSpacing, south, addCrossing(true));
    }
    for (u32 row = span.firstRow; row <= span.lastRow; ++row) {
        for (u32 column = span.firstColumn; column <= span.lastColumn; ++column) {
            const u32 tile = map.TileIndex({ .column = column, .row = row });
            if (map.PlaceAt(tile) != NO_PLACE && map.CostAt(tile, profile) != IMPASSABLE_COST) {
                graph.nodes.push_back(tile);
            }
        }
    }
    std::ranges::sort(graph.nodes);
    const auto repeats = std::ranges::unique(graph.nodes);
    graph.nodes.erase(repeats.begin(), repeats.end());
    PN_ASSERT(
        graph.nodes.size() <= std::size_t{ std::numeric_limits<u16>::max() } + 1, "a sector's node indices fit in 16 bits"
    );

    graph.crossings.reserve(pending.size());
    for (const PendingCrossing& crossing : pending) {
        graph.crossings.push_back(
            { .node = NodeIndex(graph.nodes, crossing.tile),
              .south = crossing.south,
              .cost = crossing.cost,
              .costBack = map.CostAt(crossing.tile, profile) }
        );
    }
    std::ranges::sort(graph.crossings, {}, [](const SectorCrossing& crossing) {
        return std::pair{ crossing.node, crossing.south };
    });

    std::vector<u32> way;
    for (std::size_t from = 0; from + 1 < graph.nodes.size(); ++from) {
        walk.WalkFrom(map, profile, span, graph.nodes[from]);
        for (std::size_t to = from + 1; to < graph.nodes.size(); ++to) {
            const u32 cost = walk.Cost(graph.nodes[to]);
            if (cost == UNREACHABLE) {
                continue;
            }
            walk.Way(graph.nodes[to], way);
            u32 costBack = 0;
            for (std::size_t step = 1; step < way.size(); ++step) {
                const u32 back = map.StepCost(way[step], way[step - 1], profile);
                // Entering a tile and the corner rule are the same either way, so a step taken can be taken back.
                PN_ASSERT(back != IMPASSABLE_COST, "a way's steps can be walked back");
                costBack += back;
            }
            graph.edges.push_back(
                { .from = static_cast<u16>(from), .to = static_cast<u16>(to), .cost = cost, .costBack = costBack }
            );
        }
    }
    return graph;
}

} // namespace core
