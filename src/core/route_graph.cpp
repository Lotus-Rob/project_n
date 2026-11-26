#include "core/route_graph.hpp"

#include "core/assert.hpp"
#include "core/jobs.hpp"
#include "core/paths.hpp"
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

/// What walking `way` back from its last tile to its first costs by `profile`.
u32 WalkedBack(const WorldMap& map, ProfileIndex profile, const std::vector<u32>& way) {
    u32 costBack = 0;
    for (std::size_t step = 1; step < way.size(); ++step) {
        const u32 back = map.StepCost(way[step], way[step - 1], profile);
        // Entering a tile and the corner rule are the same either way, so a step taken can be taken back.
        PN_ASSERT(back != IMPASSABLE_COST, "a way's steps can be walked back");
        costBack += back;
    }
    return costBack;
}

/// Sector `sector`'s node tiles by `profile` into `nodes`, ascending and each once: each side of a gate on its borders and
/// each place it holds that the profile can enter; each gate on its east and south borders also into `pending`.
void CollectNodes(
    const WorldMap& map,
    ProfileIndex profile,
    RoutingRules rules,
    u32 sector,
    std::vector<u32>& nodes,
    std::vector<PendingCrossing>& pending
) {
    const TileSpan span = SectorSpan(map, rules, sector);
    const u32 columns = map.Columns();
    const u32 width = span.lastColumn - span.firstColumn + 1;
    const u32 height = span.lastRow - span.firstRow + 1;
    const u32 topLeft = map.TileIndex({ .column = span.firstColumn, .row = span.firstRow });
    const auto addEastOrSouthTile = [&](u32 /*westOrNorth*/, u32 eastOrSouth) { nodes.push_back(eastOrSouth); };
    const auto addCrossing = [&](bool south) {
        return [&nodes, &pending, &map, profile, south](u32 inner, u32 outer) {
            nodes.push_back(inner);
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
                nodes.push_back(tile);
            }
        }
    }
    std::ranges::sort(nodes);
    const auto repeats = std::ranges::unique(nodes);
    nodes.erase(repeats.begin(), repeats.end());
}

/// Adds to `graph`, sector `sector`'s, the ways from each of its places to every node of the sectors round it and to
/// every other place of its own, found inside the block.
void AddPlaceEdges(
    const WorldMap& map,
    ProfileIndex profile,
    RoutingRules rules,
    u32 sector,
    SectorWalk& walk,
    SectorGraph& graph
) {
    std::vector<u16> places;
    for (std::size_t node = 0; node < graph.nodes.size(); ++node) {
        if (map.PlaceAt(graph.nodes[node]) != NO_PLACE) {
            places.push_back(static_cast<u16>(node));
        }
    }
    if (places.empty()) {
        return;
    }
    // A place near a sector's edge would otherwise leave only through its own sector's gates, however near a crossing
    // into the next sector lies; these ways let it step across where the cheapest way does.
    std::vector<u32> others;
    std::vector<u32> nodes;
    std::vector<PendingCrossing> unused;
    ForEachBlockSector(map, rules, sector, [&](u32 round) {
        if (round == sector) {
            return;
        }
        nodes.clear();
        unused.clear();
        CollectNodes(map, profile, rules, round, nodes, unused);
        others.insert(others.end(), nodes.begin(), nodes.end());
    });
    // Each place also gets its own way to every other place near it, whichever sector lists that one: the other's way
    // walked back may cost more than the cheapest way back, and a way kept inside one sector may miss a cheaper one
    // stepping out of it.
    for (const u16 place : places) {
        others.push_back(graph.nodes[place]);
    }
    std::ranges::sort(others);
    const TileSpan block = BlockSpan(map, rules, sector);
    std::vector<u32> way;
    for (const u16 place : places) {
        const u32 own = graph.nodes[place];
        walk.WalkFrom(map, profile, block, own);
        for (const u32 tile : others) {
            const u32 cost = tile == own ? UNREACHABLE : walk.Cost(tile);
            if (cost == UNREACHABLE) {
                continue;
            }
            walk.Way(tile, way);
            graph.placeEdges.push_back(
                { .place = place, .tile = tile, .cost = cost, .costBack = WalkedBack(map, profile, way) }
            );
        }
    }
}

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

BlockSectors BlockSectorsOf(const WorldMap& map, RoutingRules rules, u32 sector) {
    const u32 columns = SectorColumns(map, rules);
    const u32 rows = SectorRows(map, rules);
    PN_ASSERT(sector < columns * rows, "a sector of the map");
    const u32 column = sector % columns;
    const u32 row = sector / columns;
    return { .firstColumn = std::max(column, 1U) - 1,
             .lastColumn = std::min(column + 1, columns - 1),
             .firstRow = std::max(row, 1U) - 1,
             .lastRow = std::min(row + 1, rows - 1),
             .columns = columns };
}

TileSpan BlockSpan(const WorldMap& map, RoutingRules rules, u32 sector) {
    const BlockSectors block = BlockSectorsOf(map, rules, sector);
    const TileSpan first = SectorSpan(map, rules, (block.firstRow * block.columns) + block.firstColumn);
    const TileSpan last = SectorSpan(map, rules, (block.lastRow * block.columns) + block.lastColumn);
    return {
        .firstColumn = first.firstColumn, .lastColumn = last.lastColumn, .firstRow = first.firstRow, .lastRow = last.lastRow
    };
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
    SectorGraph graph;
    // A crossing's node index is known only once every node is found, sorted and merged, so its tile waits here.
    std::vector<PendingCrossing> pending;
    CollectNodes(map, profile, rules, sector, graph.nodes, pending);
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
            graph.edges.push_back(
                { .from = static_cast<u16>(from),
                  .to = static_cast<u16>(to),
                  .cost = cost,
                  .costBack = WalkedBack(map, profile, way) }
            );
        }
    }
    AddPlaceEdges(map, profile, rules, sector, walk, graph);
    return graph;
}

RouteGraph RouteGraph::Assemble(
    const WorldMap& map,
    ProfileIndex profile,
    RoutingRules rules,
    std::span<const SectorGraph> sectors
) {
    const u32 sectorColumns = SectorColumns(map, rules);
    PN_ASSERT(sectors.size() == std::size_t{ sectorColumns } * SectorRows(map, rules), "one graph for each sector of the map");
    RouteGraph graph;
    graph.m_profile = profile;
    graph.m_rules = rules;
    graph.m_columns = map.Columns();
    graph.m_rows = map.Rows();
    graph.m_sectorColumns = sectorColumns;

    std::size_t nodes = 0;
    std::size_t edges = 0;
    for (const SectorGraph& sector : sectors) {
        nodes += sector.nodes.size();
        edges += sector.edges.size() + sector.crossings.size() + sector.placeEdges.size();
    }
    PN_ASSERT(nodes < NO_NODE && edges < NO_NODE / 2, "node, edge and link numbers fit in 32 bits");
    graph.m_nodeTiles.reserve(nodes);
    graph.m_firstNode.reserve(sectors.size() + 1);
    for (const SectorGraph& sector : sectors) {
        graph.m_firstNode.push_back(static_cast<u32>(graph.m_nodeTiles.size()));
        graph.m_nodeTiles.insert(graph.m_nodeTiles.end(), sector.nodes.begin(), sector.nodes.end());
    }
    graph.m_firstNode.push_back(static_cast<u32>(graph.m_nodeTiles.size()));

    graph.m_edges.reserve(edges);
    graph.m_firstCrossing.reserve(sectors.size());
    graph.m_firstBlockEdge.reserve(sectors.size());
    for (std::size_t index = 0; index < sectors.size(); ++index) {
        graph.AppendSector(static_cast<u32>(index), sectors[index]);
    }

    graph.m_firstLink.assign(nodes + 1, 0);
    for (const GraphEdge& edge : graph.m_edges) {
        ++graph.m_firstLink[edge.from + 1];
        ++graph.m_firstLink[edge.to + 1];
    }
    std::partial_sum(graph.m_firstLink.begin(), graph.m_firstLink.end(), graph.m_firstLink.begin());
    graph.m_links.resize(graph.m_firstLink.back());
    std::vector<u32> next(graph.m_firstLink.begin(), graph.m_firstLink.end() - 1);
    // Edges are laid out in ID order, so each node's links come out by edge ID.
    for (std::size_t index = 0; index < graph.m_edges.size(); ++index) {
        const GraphEdge& edge = graph.m_edges[index];
        const auto edgeId = static_cast<u32>(index);
        graph.m_links[next[edge.from]++] = { .node = edge.to, .edge = edgeId, .cost = edge.cost, .reversed = false };
        graph.m_links[next[edge.to]++] = { .node = edge.from, .edge = edgeId, .cost = edge.costBack, .reversed = true };
    }

    graph.m_placeNode.reserve(map.Places().size());
    for (const Place& place : map.Places()) {
        graph.m_placeNode.push_back(graph.NodeAt(map.TileIndex(place.tile)));
    }
    return graph;
}

void RouteGraph::AppendSector(u32 sector, const SectorGraph& built) {
    const u32 first = m_firstNode[sector];
    for (const SectorEdge& edge : built.edges) {
        m_edges.push_back(
            { .from = first + edge.from, .to = first + edge.to, .cost = edge.cost, .costBack = edge.costBack, .sector = sector }
        );
    }
    m_firstCrossing.push_back(static_cast<u32>(m_edges.size()));
    for (const SectorCrossing& crossing : built.crossings) {
        const u32 tile = built.nodes[crossing.node];
        const u32 beside = NodeAt(tile + (crossing.south ? m_columns : 1));
        PN_ASSERT(beside != NO_NODE, "a crossing lands on a node of the sector it enters");
        if (beside == NO_NODE) {
            continue;
        }
        m_edges.push_back(
            { .from = first + crossing.node,
              .to = beside,
              .cost = crossing.cost,
              .costBack = crossing.costBack,
              .sector = sector }
        );
    }
    m_firstBlockEdge.push_back(static_cast<u32>(m_edges.size()));
    for (const SectorPlaceEdge& edge : built.placeEdges) {
        const u32 other = NodeAt(edge.tile);
        PN_ASSERT(other != NO_NODE, "a place's block edge ends on a node of its block");
        if (other == NO_NODE) {
            continue;
        }
        m_edges.push_back(
            { .from = first + edge.place, .to = other, .cost = edge.cost, .costBack = edge.costBack, .sector = sector }
        );
    }
}

ProfileIndex RouteGraph::Profile() const {
    return m_profile;
}

RoutingRules RouteGraph::Rules() const {
    return m_rules;
}

u32 RouteGraph::Nodes() const {
    return static_cast<u32>(m_nodeTiles.size());
}

u32 RouteGraph::NodeTile(u32 node) const {
    PN_ASSERT(node < m_nodeTiles.size(), "a node of the graph");
    return m_nodeTiles[node];
}

u32 RouteGraph::NodeAt(u32 tile) const {
    PN_ASSERT(u64{ tile } < u64{ m_columns } * m_rows, "a tile of the map");
    if (u64{ tile } >= u64{ m_columns } * m_rows) {
        return NO_NODE;
    }
    const u32 column = tile % m_columns;
    const u32 row = tile / m_columns;
    const u32 sector = ((row / m_rules.sectorSize) * m_sectorColumns) + (column / m_rules.sectorSize);
    const std::span<const u32> tiles = SectorNodes(sector);
    const auto found = std::ranges::lower_bound(tiles, tile);
    if (found == tiles.end() || *found != tile) {
        return NO_NODE;
    }
    return m_firstNode[sector] + static_cast<u32>(found - tiles.begin());
}

bool RouteGraph::IsCrossing(u32 edge) const {
    PN_ASSERT(edge < m_edges.size(), "an edge of the graph");
    const u32 sector = m_edges[edge].sector;
    return edge >= m_firstCrossing[sector] && edge < m_firstBlockEdge[sector];
}

bool RouteGraph::IsBlockEdge(u32 edge) const {
    PN_ASSERT(edge < m_edges.size(), "an edge of the graph");
    // A sector's block edges come last among its edges.
    return edge >= m_firstBlockEdge[m_edges[edge].sector];
}

TileSpan RouteGraph::WalkSpan(const WorldMap& map, u32 edge) const {
    PN_ASSERT(edge < m_edges.size(), "an edge of the graph");
    const u32 sector = m_edges[edge].sector;
    return IsBlockEdge(edge) ? BlockSpan(map, m_rules, sector) : SectorSpan(map, m_rules, sector);
}

u32 RouteGraph::PlaceNode(u16 place) const {
    PN_ASSERT(place < m_placeNode.size(), "a place of the map");
    return m_placeNode[place];
}

std::span<const u32> RouteGraph::SectorNodes(u32 sector) const {
    PN_ASSERT(std::size_t{ sector } + 1 < m_firstNode.size(), "a sector of the map");
    return std::span{ m_nodeTiles }.subspan(m_firstNode[sector], m_firstNode[sector + 1] - m_firstNode[sector]);
}

u32 RouteGraph::FirstNode(u32 sector) const {
    PN_ASSERT(std::size_t{ sector } + 1 < m_firstNode.size(), "a sector of the map");
    return m_firstNode[sector];
}

std::span<const GraphEdge> RouteGraph::Edges() const {
    return m_edges;
}

std::span<const GraphLink> RouteGraph::LinksOf(u32 node) const {
    PN_ASSERT(node < m_nodeTiles.size(), "a node of the graph");
    return std::span{ m_links }.subspan(m_firstLink[node], m_firstLink[node + 1] - m_firstLink[node]);
}

std::size_t RouteGraph::Bytes() const {
    return CapacityBytes(m_nodeTiles) + CapacityBytes(m_firstNode) + CapacityBytes(m_placeNode) + CapacityBytes(m_edges) +
           CapacityBytes(m_firstCrossing) + CapacityBytes(m_firstBlockEdge) + CapacityBytes(m_firstLink) +
           CapacityBytes(m_links);
}

bool RouteGraph::SameAs(const RouteGraph& other) const {
    return m_rules == other.m_rules && m_columns == other.m_columns && m_rows == other.m_rows &&
           m_nodeTiles == other.m_nodeTiles && m_firstNode == other.m_firstNode && m_placeNode == other.m_placeNode &&
           m_edges == other.m_edges && m_firstCrossing == other.m_firstCrossing && m_firstBlockEdge == other.m_firstBlockEdge &&
           m_firstLink == other.m_firstLink && m_links == other.m_links;
}

RouteGraphs RouteGraphs::Build(const WorldMap& map, RoutingRules rules, BuildOptions options) {
    PN_PROFILE_ZONE();
    PN_ASSERT(options.sectorsPerChunk > 0, "a chunk builds some sectors");
    const std::size_t profiles = map.Profiles().size();
    const u32 sectors = SectorColumns(map, rules) * SectorRows(map, rules);
    // Each profile's sectors lie together, so each profile's are one span to assemble.
    std::vector<SectorGraph> built(profiles * sectors);
    const u32 perChunk = std::max(options.sectorsPerChunk, 1U);
    const u32 chunks = ChunkCount(sectors, perChunk);
    std::vector<u32> builtPerChunk(chunks, 0);
    const auto buildChunk = [&](u32 chunk) {
        SectorWalk walk;
        const u64 first = u64{ chunk } * perChunk;
        const u64 last = std::min(first + perChunk, u64{ sectors });
        for (auto sector = static_cast<u32>(first); sector < last; ++sector) {
            for (std::size_t profile = 0; profile < profiles; ++profile) {
                const auto index = static_cast<ProfileIndex>(profile);
                SectorGraph& slot = built[(profile * sectors) + sector];
                std::size_t twin = 0;
                while (options.share && twin < profile &&
                       !SectorCostsAgree(map, rules, sector, static_cast<ProfileIndex>(twin), index)) {
                    ++twin;
                }
                if (options.share && twin < profile) {
                    slot = built[(twin * sectors) + sector];
                }
                else {
                    slot = BuildSector(map, index, rules, sector, walk);
                    ++builtPerChunk[chunk];
                }
            }
        }
    };
    if (options.jobs != nullptr) {
        options.jobs->ParallelFor(chunks, buildChunk);
    }
    else {
        for (u32 chunk = 0; chunk < chunks; ++chunk) {
            buildChunk(chunk);
        }
    }

    RouteGraphs graphs;
    graphs.m_graphs.reserve(profiles);
    for (std::size_t profile = 0; profile < profiles; ++profile) {
        graphs.m_graphs.push_back(
            RouteGraph::Assemble(
                map, static_cast<ProfileIndex>(profile), rules, std::span{ built }.subspan(profile * sectors, sectors)
            )
        );
    }
    graphs.m_sectorsBuilt = std::accumulate(builtPerChunk.begin(), builtPerChunk.end(), u32{ 0 });
    return graphs;
}

const RouteGraph& RouteGraphs::Of(ProfileIndex profile) const {
    PN_ASSERT(profile < m_graphs.size(), "a profile of the map");
    return m_graphs[profile];
}

u32 RouteGraphs::SectorsBuilt() const {
    return m_sectorsBuilt;
}

std::size_t RouteGraphs::Bytes() const {
    std::size_t bytes = CapacityBytes(m_graphs);
    for (const RouteGraph& graph : m_graphs) {
        bytes += graph.Bytes();
    }
    return bytes;
}

bool SectorCostsAgree(const WorldMap& map, RoutingRules rules, u32 sector, ProfileIndex first, ProfileIndex second) {
    const TileSpan own = SectorSpan(map, rules, sector);
    bool holdsPlace = false;
    for (u32 row = own.firstRow; row <= own.lastRow && !holdsPlace; ++row) {
        for (u32 column = own.firstColumn; column <= own.lastColumn && !holdsPlace; ++column) {
            holdsPlace = map.PlaceAt(map.TileIndex({ .column = column, .row = row })) != NO_PLACE;
        }
    }
    // A place's block edges walk its block and end on the gates of the sectors round it, which read their own rings.
    const TileSpan span = holdsPlace ? BlockSpan(map, rules, sector) : own;
    const u32 firstColumn = span.firstColumn == 0 ? 0 : span.firstColumn - 1;
    const u32 lastColumn = std::min(span.lastColumn + 1, map.Columns() - 1);
    const u32 firstRow = span.firstRow == 0 ? 0 : span.firstRow - 1;
    const u32 lastRow = std::min(span.lastRow + 1, map.Rows() - 1);
    for (u32 row = firstRow; row <= lastRow; ++row) {
        for (u32 column = firstColumn; column <= lastColumn; ++column) {
            const u32 tile = map.TileIndex({ .column = column, .row = row });
            if (map.CostAt(tile, first) != map.CostAt(tile, second)) {
                return false;
            }
        }
    }
    return true;
}

void EdgeTiles(const WorldMap& map, const RouteGraph& graph, u32 edge, SectorWalk& walk, std::vector<u32>& into) {
    PN_ASSERT(edge < graph.Edges().size(), "an edge of the graph");
    const GraphEdge& way = graph.Edges()[edge];
    const u32 start = graph.NodeTile(way.from);
    const u32 end = graph.NodeTile(way.to);
    if (graph.IsCrossing(edge)) {
        into.clear();
        into.push_back(start);
        into.push_back(end);
        return;
    }
    walk.WalkFrom(map, graph.Profile(), graph.WalkSpan(map, edge), start);
    walk.Way(end, into);
}

} // namespace core
