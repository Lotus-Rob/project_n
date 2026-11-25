#include "core/route_search.hpp"

#include "core/assert.hpp"
#include "core/paths.hpp"
#include "core/profile.hpp"

namespace core {

namespace {

/// How a node seeded from the start was reached: by the start's own stretch, no edge.
constexpr u32 FROM_START = std::numeric_limits<u32>::max();
/// The queue place of a slot the search has settled.
constexpr u32 SETTLED = std::numeric_limits<u32>::max();

/// How a node reached over `link` was reached: its edge and direction in one number, which fits as a graph has fewer
/// than half of u32's range of edges.
u32 ViaLink(const GraphLink& link) {
    return (link.edge << 1U) | (link.reversed ? 1U : 0U);
}

/// The bytes `vector` holds, by its capacity.
template<typename T>
std::size_t CapacityBytes(const std::vector<T>& vector) {
    return vector.capacity() * sizeof(T);
}

} // namespace

std::size_t GraphRoute::Bytes() const {
    return sizeof(GraphRoute) + CapacityBytes(head) + CapacityBytes(steps) + CapacityBytes(tail);
}

SearchCounts RouteScratch::Counts() const {
    return m_counts;
}

void RouteScratch::Begin(std::size_t slots) {
    if (m_stamp.size() < slots) {
        m_cost.resize(slots);
        m_via.resize(slots);
        m_stamp.resize(slots, 0);
        m_key.resize(slots);
        m_place.resize(slots);
        // A slot is queued at most once at a time, so this bounds the queue.
        m_queue.reserve(slots);
    }
    ++m_search;
    if (m_search == 0) {
        // The stamps wrapped: a slot last reached four billion searches ago would read as reached by this one.
        std::ranges::fill(m_stamp, 0);
        m_search = 1;
    }
    m_queue.clear();
    m_counts = {};
}

u32 RouteScratch::CostOf(u32 slot) const {
    return m_stamp[slot] == m_search ? m_cost[slot] : UNREACHABLE;
}

void RouteScratch::Offer(u32 slot, u32 cost, u64 key, u32 via) {
    const bool reached = m_stamp[slot] == m_search;
    if (reached && cost >= m_cost[slot]) {
        return;
    }
    m_stamp[slot] = m_search;
    m_cost[slot] = cost;
    m_key[slot] = key;
    m_via[slot] = via;
    ++m_counts.relaxed;
    if (!reached || m_place[slot] == SETTLED) {
        m_queue.push_back(slot);
        Place(m_queue.size() - 1, slot);
    }
    SiftUp(m_place[slot]);
}

u32 RouteScratch::Pop() {
    const u32 top = m_queue.front();
    const u32 last = m_queue.back();
    m_queue.pop_back();
    if (!m_queue.empty()) {
        Place(0, last);
        SiftDown(0);
    }
    m_place[top] = SETTLED;
    return top;
}

bool RouteScratch::Before(u32 first, u32 second) const {
    return std::pair{ m_key[first], first } < std::pair{ m_key[second], second };
}

void RouteScratch::SiftUp(std::size_t position) {
    const u32 slot = m_queue[position];
    while (position > 0) {
        const std::size_t parent = (position - 1) / 2;
        if (!Before(slot, m_queue[parent])) {
            break;
        }
        Place(position, m_queue[parent]);
        position = parent;
    }
    Place(position, slot);
}

void RouteScratch::SiftDown(std::size_t position) {
    const u32 slot = m_queue[position];
    const std::size_t size = m_queue.size();
    while (true) {
        const std::size_t left = (position * 2) + 1;
        if (left >= size) {
            break;
        }
        const std::size_t right = left + 1;
        const std::size_t child = right < size && Before(m_queue[right], m_queue[left]) ? right : left;
        if (!Before(m_queue[child], slot)) {
            break;
        }
        Place(position, m_queue[child]);
        position = child;
    }
    Place(position, slot);
}

void RouteScratch::Place(std::size_t position, u32 slot) {
    m_queue[position] = slot;
    m_place[slot] = static_cast<u32>(position);
}

void RouteScratch::OfferNode(const WorldMap& map, const RouteGraph& graph, const Ends& ends, u32 node, u64 cost, u32 via) {
    if (cost >= CostOf(node)) {
        return;
    }
    const TilePoint point = map.PointOf(graph.NodeTile(node));
    const u64 estimate = StepEstimate(
        ends.cheapest,
        static_cast<i64>(ends.end.column) - static_cast<i64>(point.column),
        static_cast<i64>(ends.end.row) - static_cast<i64>(point.row)
    );
    Offer(node, static_cast<u32>(cost), cost + estimate, via);
}

void RouteScratch::Seed(const WorldMap& map, const RouteGraph& graph, const Ends& ends) {
    if (ends.fromNode != NO_NODE) {
        OfferNode(map, graph, ends, ends.fromNode, 0, FROM_START);
        return;
    }
    const u32 first = graph.FirstNode(ends.fromSector);
    const std::span<const u32> starts = graph.SectorNodes(ends.fromSector);
    for (std::size_t index = 0; index < starts.size(); ++index) {
        const u32 cost = m_fromWalk.Cost(starts[index]);
        if (cost != UNREACHABLE) {
            OfferNode(map, graph, ends, first + static_cast<u32>(index), cost, FROM_START);
        }
    }
}

bool RouteScratch::Search(const WorldMap& map, const RouteGraph& graph, const Ends& ends) {
    const u32 nodes = graph.Nodes();
    const u32 endFirst = graph.FirstNode(ends.toSector);
    const auto endCount = static_cast<u32>(graph.SectorNodes(ends.toSector).size());
    while (!m_queue.empty()) {
        // Every way left costs at least the queue's least key, so once that reaches the way inside the sector, it wins.
        if (ends.inside != UNREACHABLE && m_key[m_queue.front()] >= ends.inside) {
            return false;
        }
        const u32 node = Pop();
        if (node < nodes) {
            ++m_counts.popped;
        }
        if (node == ends.goal) {
            return true;
        }
        const u32 reached = m_cost[node];
        for (const GraphLink& link : graph.LinksOf(node)) {
            OfferNode(map, graph, ends, link.node, u64{ reached } + link.cost, ViaLink(link));
        }
        // The end's sector's nodes, by their place among the graph's, step off the graph onto an end that is no node.
        const u32 last =
            ends.targetNode == NO_NODE && node - endFirst < endCount ? m_toWalk.Cost(graph.NodeTile(node)) : UNREACHABLE;
        const u64 total = u64{ reached } + last;
        if (last != UNREACHABLE && total < CostOf(ends.goal)) {
            Offer(ends.goal, static_cast<u32>(total), total, node);
        }
    }
    return false;
}

GraphRoute RouteScratch::Trace(const RouteGraph& graph, const Ends& ends) const {
    GraphRoute route;
    u32 node = ends.goal;
    if (ends.targetNode == NO_NODE) {
        node = m_via[ends.goal];
        m_toWalk.Way(graph.NodeTile(node), route.tail);
    }
    while (m_via[node] != FROM_START && route.steps.size() <= graph.Nodes()) {
        const u32 via = m_via[node];
        const RouteStep step{ .edge = via >> 1U, .reversed = (via & 1U) != 0 };
        route.steps.push_back(step);
        const GraphEdge& edge = graph.Edges()[step.edge];
        node = step.reversed ? edge.to : edge.from;
    }
    PN_ASSERT(m_via[node] == FROM_START, "a route's steps lead back to the start");
    std::ranges::reverse(route.steps);
    if (ends.fromNode == NO_NODE) {
        m_fromWalk.Way(graph.NodeTile(node), route.head);
    }
    route.cost = m_cost[ends.goal];
    return route;
}

std::optional<GraphRoute> FindGraphRoute(
    const WorldMap& map,
    const RouteGraph& graph,
    u32 from,
    u32 target,
    RouteScratch& scratch
) {
    PN_PROFILE_ZONE();
    const u64 tiles = u64{ map.Columns() } * map.Rows();
    PN_ASSERT(from < tiles && target < tiles, "a route runs between tiles of the map");
    scratch.m_counts = {};
    const ProfileIndex profile = graph.Profile();
    if (from >= tiles || target >= tiles || map.CostAt(target, profile) == IMPASSABLE_COST) {
        return std::nullopt;
    }
    if (from == target) {
        GraphRoute still;
        still.head.push_back(from);
        return still;
    }

    const RoutingRules rules = graph.Rules();
    RouteScratch::Ends ends{ .fromSector = SectorOf(map, rules, from),
                             .toSector = SectorOf(map, rules, target),
                             .fromNode = graph.NodeAt(from),
                             .targetNode = graph.NodeAt(target),
                             .goal = 0,
                             .inside = UNREACHABLE,
                             .cheapest = CheapestEntry(map.Profiles()[profile]),
                             .end = map.PointOf(target) };
    // Onto a tile that is no node the search ends on a slot of its own past the nodes, reached from the end's sector.
    ends.goal = ends.targetNode != NO_NODE ? ends.targetNode : graph.Nodes();
    scratch.Begin(std::size_t{ graph.Nodes() } + 1);

    // Between two nodes the graph alone routes, so a route and the graph's costs agree; with an end on open ground in the
    // same sector, the way inside it may be cheaper than any through the graph's nodes.
    const bool oneSector = ends.fromSector == ends.toSector && (ends.fromNode == NO_NODE || ends.targetNode == NO_NODE);
    if (ends.fromNode == NO_NODE || oneSector) {
        scratch.m_fromWalk.WalkFrom(map, profile, SectorSpan(map, rules, ends.fromSector), from);
    }
    if (oneSector) {
        ends.inside = scratch.m_fromWalk.Cost(target);
    }
    if (ends.targetNode == NO_NODE) {
        scratch.m_toWalk.WalkTo(map, profile, SectorSpan(map, rules, ends.toSector), target);
    }

    scratch.Seed(map, graph, ends);
    if (scratch.Search(map, graph, ends)) {
        return scratch.Trace(graph, ends);
    }
    if (ends.inside == UNREACHABLE) {
        return std::nullopt;
    }
    GraphRoute inside;
    scratch.m_fromWalk.Way(target, inside.head);
    inside.cost = ends.inside;
    return inside;
}

std::vector<u32> RouteTiles(const WorldMap& map, const RouteGraph& graph, const GraphRoute& route) {
    std::vector<u32> tiles = route.head;
    const auto append = [&](const std::vector<u32>& part) {
        if (part.empty()) {
            return;
        }
        PN_ASSERT(tiles.empty() || tiles.back() == part.front(), "a route's parts meet end to start");
        const auto start = !tiles.empty() && tiles.back() == part.front() ? part.begin() + 1 : part.begin();
        tiles.insert(tiles.end(), start, part.end());
    };
    SectorWalk walk;
    std::vector<u32> part;
    for (const RouteStep& step : route.steps) {
        EdgeTiles(map, graph, step.edge, walk, part);
        if (step.reversed) {
            std::ranges::reverse(part);
        }
        append(part);
    }
    append(route.tail);
    return tiles;
}

} // namespace core
