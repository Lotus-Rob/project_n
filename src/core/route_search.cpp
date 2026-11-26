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
/// No slot: the end of a bucket's list.
constexpr u32 NO_SLOT = std::numeric_limits<u32>::max();
/// The bucket of the slots whose key is the front's.
constexpr u32 FRONT = 0;

/// How a node reached over `link` was reached: its edge and direction in one number, which fits as a graph has fewer
/// than half of u32's range of edges.
u32 ViaLink(const GraphLink& link) {
    return (link.edge << 1U) | (link.reversed ? 1U : 0U);
}

/// The node of the place on `tile`, or NO_NODE for a tile that is no place or whose place is no node.
u32 PlaceNodeAt(const WorldMap& map, const RouteGraph& graph, u32 tile) {
    // Only a place has ways of its own to the sectors round it; any other end, a gate's tile included, reaches them by
    // its legs.
    return map.PlaceAt(tile) != NO_PLACE ? graph.NodeAt(tile) : NO_NODE;
}

/// Whether `point` lies in `span`.
bool Contains(TileSpan span, TilePoint point) {
    return point.column >= span.firstColumn && point.column <= span.lastColumn && point.row >= span.firstRow &&
           point.row <= span.lastRow;
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
        m_next.resize(slots);
        m_prev.resize(slots);
        // A slot is queued at most once at a time, so this bounds the front.
        m_queue.reserve(slots);
    }
    ++m_search;
    if (m_search == 0) {
        // The stamps wrapped: a slot last reached four billion searches ago would read as reached by this one.
        std::ranges::fill(m_stamp, 0);
        m_search = 1;
    }
    m_queue.clear();
    m_filled = 0;
    m_least = 0;
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
    // The buckets sort keys only from the front's up. A link, or the walk on to the end, costs at least what it lowers
    // the estimate by, since every step costs at least the estimate's per-step floor, so no key offered falls below that.
    PN_ASSERT(key >= m_least, "a search's keys never fall below the front's");
    const bool queued = reached && m_place[slot] != SETTLED;
    m_stamp[slot] = m_search;
    m_cost[slot] = cost;
    m_key[slot] = key;
    m_via[slot] = via;
    ++m_counts.relaxed;
    if (queued) {
        PN_ASSERT(m_place[slot] != FRONT, "a slot at the front has the least key a search can offer");
        if (BucketOf(key) == m_place[slot]) {
            return;
        }
        Unlink(slot);
    }
    Enqueue(slot);
}

bool RouteScratch::Queued() const {
    return !m_queue.empty() || m_filled != 0;
}

u64 RouteScratch::LeastKey() {
    if (m_queue.empty()) {
        PN_ASSERT(m_filled != 0, "the queue holds a slot");
        // The lowest filled bucket holds the least keys; with the front's key moved up to its least, every slot in it falls
        // into a lower bucket, and those above keep theirs, as they differ from both keys in the same highest bit.
        const auto bucket = static_cast<u32>(std::countr_zero(m_filled)) + 1;
        m_filled &= ~(u64{ 1 } << (bucket - 1));
        const u32 first = m_first[bucket - 1];
        u64 least = std::numeric_limits<u64>::max();
        for (u32 slot = first; slot != NO_SLOT; slot = m_next[slot]) {
            least = std::min(least, m_key[slot]);
        }
        m_least = least;
        for (u32 slot = first; slot != NO_SLOT;) {
            const u32 next = m_next[slot];
            Enqueue(slot);
            slot = next;
        }
    }
    return m_least;
}

u32 RouteScratch::Pop() {
    static_cast<void>(LeastKey());
    std::ranges::pop_heap(m_queue, std::greater{});
    const u32 top = m_queue.back();
    m_queue.pop_back();
    m_place[top] = SETTLED;
    return top;
}

u32 RouteScratch::BucketOf(u64 key) const {
    return static_cast<u32>(std::bit_width(key ^ m_least));
}

void RouteScratch::Enqueue(u32 slot) {
    const u32 bucket = BucketOf(m_key[slot]);
    m_place[slot] = bucket;
    if (bucket == FRONT) {
        m_queue.push_back(slot);
        std::ranges::push_heap(m_queue, std::greater{});
        return;
    }
    const u64 bit = u64{ 1 } << (bucket - 1);
    const u32 next = (m_filled & bit) != 0 ? m_first[bucket - 1] : NO_SLOT;
    m_next[slot] = next;
    m_prev[slot] = NO_SLOT;
    if (next != NO_SLOT) {
        m_prev[next] = slot;
    }
    m_first[bucket - 1] = slot;
    m_filled |= bit;
}

void RouteScratch::Unlink(u32 slot) {
    const u32 bucket = m_place[slot];
    const u32 next = m_next[slot];
    const u32 prev = m_prev[slot];
    if (prev != NO_SLOT) {
        m_next[prev] = next;
    }
    else if (next != NO_SLOT) {
        m_first[bucket - 1] = next;
    }
    else {
        m_filled &= ~(u64{ 1 } << (bucket - 1));
    }
    if (next != NO_SLOT) {
        m_prev[next] = prev;
    }
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
    // The walk from the start covers BlockSpan, so these are exactly the sectors whose nodes it may reach.
    ForEachBlockSector(map, graph.Rules(), ends.fromSector, [&](u32 sector) {
        const u32 first = graph.FirstNode(sector);
        const std::span<const u32> starts = graph.SectorNodes(sector);
        for (std::size_t index = 0; index < starts.size(); ++index) {
            const u32 cost = m_fromWalk.Cost(starts[index]);
            if (cost != UNREACHABLE) {
                OfferNode(map, graph, ends, first + static_cast<u32>(index), cost, FROM_START);
            }
        }
    });
}

bool RouteScratch::Search(const WorldMap& map, const RouteGraph& graph, const Ends& ends) {
    const u32 nodes = graph.Nodes();
    while (Queued()) {
        // Every way left costs at least the queue's least key, so once that reaches the direct way, it wins.
        if (ends.direct != UNREACHABLE && LeastKey() >= ends.direct) {
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
        // The walk to an end that is no place's node reaches at most the nodes of the block round it.
        const u32 last = ends.targetNode == NO_NODE ? m_toWalk.Cost(graph.NodeTile(node)) : UNREACHABLE;
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
        if (route.tail.size() == 1) {
            route.tail.clear(); // the end is that node
        }
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
        if (route.head.size() == 1) {
            route.head.clear(); // the start is that node
        }
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
    // A place the profile can't enter has no node, so no route from it: the place table, which reads only the graph,
    // must agree with the routes it prices.
    const u16 fromPlace = map.PlaceAt(from);
    if (fromPlace != NO_PLACE && graph.PlaceNode(fromPlace) == NO_NODE) {
        return std::nullopt;
    }
    if (from == target) {
        GraphRoute still;
        still.head.push_back(from);
        return still;
    }

    const RoutingRules rules = graph.Rules();
    RouteScratch::Ends ends{ .fromSector = SectorOf(map, rules, from),
                             .fromNode = PlaceNodeAt(map, graph, from),
                             .targetNode = PlaceNodeAt(map, graph, target),
                             .goal = 0,
                             .direct = UNREACHABLE,
                             .cheapest = CheapestEntry(map.Profiles()[profile]),
                             .end = map.PointOf(target) };
    // Onto any end but a place's node the search ends on a slot of its own past the nodes, reached from the block round it.
    ends.goal = ends.targetNode != NO_NODE ? ends.targetNode : graph.Nodes();
    scratch.Begin(std::size_t{ graph.Nodes() } + 1);

    // An end near a sector border would otherwise have to go round through a gate of its own sector, however near the
    // crossing it faces; the legs reach over the sectors round each end so that crossing is among the ways out.
    // Between two places the graph alone routes, so a route and the graph's costs agree; with either end elsewhere and the
    // other within its block, the way within the block may be cheaper than any through the graph's nodes.
    const TileSpan fromBlock = BlockSpan(map, rules, ends.fromSector);
    const bool direct = (ends.fromNode == NO_NODE || ends.targetNode == NO_NODE) && Contains(fromBlock, ends.end);
    if (ends.fromNode == NO_NODE || direct) {
        scratch.m_fromWalk.WalkFrom(map, profile, fromBlock, from);
    }
    if (direct) {
        ends.direct = scratch.m_fromWalk.Cost(target);
    }
    if (ends.targetNode == NO_NODE) {
        scratch.m_toWalk.WalkTo(map, profile, BlockSpan(map, rules, SectorOf(map, rules, target)), target);
    }

    scratch.Seed(map, graph, ends);
    if (scratch.Search(map, graph, ends)) {
        return scratch.Trace(graph, ends);
    }
    if (ends.direct == UNREACHABLE) {
        return std::nullopt;
    }
    GraphRoute route;
    scratch.m_fromWalk.Way(target, route.head);
    route.cost = ends.direct;
    return route;
}

std::vector<u32> RouteTiles(const WorldMap& map, const RouteGraph& graph, const GraphRoute& route, SectorWalk& walk) {
    std::vector<u32> tiles = route.head;
    const auto append = [&](const std::vector<u32>& part) {
        if (part.empty()) {
            return;
        }
        PN_ASSERT(tiles.empty() || tiles.back() == part.front(), "a route's parts meet end to start");
        const auto start = !tiles.empty() && tiles.back() == part.front() ? part.begin() + 1 : part.begin();
        tiles.insert(tiles.end(), start, part.end());
    };
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

std::vector<u32> RouteTiles(const WorldMap& map, const RouteGraph& graph, const GraphRoute& route) {
    SectorWalk walk;
    return RouteTiles(map, graph, route, walk);
}

} // namespace core
