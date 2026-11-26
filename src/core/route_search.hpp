#pragma once

/// @file
/// @ingroup core
/// Routes from any tile to any tile over a profile's route graph: a start or end that is no node joined to the nodes of
/// its block (its sector and the sectors round it) by a walk kept inside the block, the graph searched between them by
/// A*. Whole numbers with fixed tie-breaks, so a route is the same on every machine, whatever was searched before it.

#include "core/paths.hpp"
#include "core/route_graph.hpp"
#include "core/types.hpp"
#include "core/world_map.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

namespace core {

/// One edge of a route, in the direction it is walked.
struct RouteStep {
    /// The edge's ID in its graph.
    u32 edge = 0;
    /// Whether it is walked from its `to` node back to its `from` node.
    bool reversed = false;

    /// Whether both are equal.
    [[nodiscard]] friend constexpr bool operator==(RouteStep, RouteStep) = default;
};

/// A route over a graph: a stretch of its own from the start onto the graph, the graph's edges, and a stretch of its own
/// from the graph to the end. A route the graph can't beat between ends one block apart at most, and a route that goes
/// nowhere, are its head alone. Between two places it is the graph's edges alone.
struct GraphRoute {
    /// The tiles from the start to its first node, both included; empty when the start is its first node.
    std::vector<u32> head;
    /// The graph's edges in walking order.
    std::vector<RouteStep> steps;
    /// The tiles from the last node to the end, both included; empty when the end is its last node.
    std::vector<u32> tail;
    /// What walking it costs: its head, its steps (each the cost of its own direction) and its tail.
    u32 cost = 0;

    /// The bytes it holds: itself and its arrays, by their capacity.
    [[nodiscard]] std::size_t Bytes() const;

    /// Whether all are equal.
    [[nodiscard]] friend bool operator==(const GraphRoute&, const GraphRoute&) = default;
};

/// What the last search did, for measuring.
struct SearchCounts {
    /// How many nodes it settled.
    u32 popped = 0;
    /// How many times it set or bettered a node's way, or the end's.
    u32 relaxed = 0;
};

class RouteScratch;

/// The route from `from` to `target` over `graph`, the graph of its profile over `map`; none when `target` can't be
/// entered or reached, or when the route would cost UNREACHABLE or more, as costs are counted in 32 bits (no map the game
/// makes holds one). An end on a place that is a node starts or ends there alone: the graph's ways from a place already
/// reach every node of its block. Any other start, a gate's tile included, reaches every node of its block (its sector
/// and the up to eight round it) by the cheapest way kept inside the block, and any other end is reached from every node
/// of its block by the cheapest way inside that block, each priced in the direction walked. A route between two places
/// uses the graph alone; otherwise, when the end lies in the start's block, the cheapest way inside that block is the
/// route unless the graph's is strictly cheaper. It reads the map and the graph only, so threads may search at once,
/// each with its own `scratch`. It relies on every edge's cost and costBack being a walk of StepCost steps, so never
/// less than the estimate falls along it: its queue takes keys only in rising order and settles each node once. An edge
/// cheaper than that breaks the search, and only debug and releasedbg builds catch it.
[[nodiscard]] std::optional<GraphRoute> FindGraphRoute(
    const WorldMap& map,
    const RouteGraph& graph,
    u32 from,
    u32 target,
    RouteScratch& scratch
);

/// A search's working memory, kept by its caller across searches and graphs: per node a cost, the link it was reached
/// by and the stamp of the search that wrote them, the queue's storage, and the two walks over the ends' blocks of
/// sectors. A search writes only the nodes it reaches and clears nothing; it allocates only when a graph, or an end's
/// block, is larger than any it has served. One thread at a time.
class RouteScratch {
public:
    /// What the last search did.
    [[nodiscard]] SearchCounts Counts() const;

private:
    friend std::optional<GraphRoute> FindGraphRoute(
        const WorldMap& map,
        const RouteGraph& graph,
        u32 from,
        u32 target,
        RouteScratch& scratch
    );

    /// What one search routes between, worked out before it starts.
    struct Ends {
        /// The start's sector, the middle of the block its leg reaches over.
        u32 fromSector = 0;
        /// The start's node when it is a place's, else NO_NODE.
        u32 fromNode = NO_NODE;
        /// The end's node when it is a place's, else NO_NODE.
        u32 targetNode = NO_NODE;
        /// The slot the search ends on: the end's node, or past the nodes for an end that is none.
        u32 goal = 0;
        /// What the way from the start to the end within the start's block costs; UNREACHABLE when both ends are places'
        /// nodes, when the end lies outside the start's block, or when no way inside the block reaches it.
        u32 direct = UNREACHABLE;
        /// The least any step costs by the graph's profile.
        u32 cheapest = 0;
        /// The end's tile, by column and row.
        TilePoint end{};
    };

    /// Readies the workspace for a search over `slots` slots: a new stamp, room for them, nothing counted.
    void Begin(std::size_t slots);
    /// The cost so far of `slot`, or UNREACHABLE if this search hasn't reached it.
    [[nodiscard]] u32 CostOf(u32 slot) const;
    /// Sets `slot`'s way to `cost`, by `via`, queued by `key`, if it betters what this search had for it. `key` is at
    /// least the queue's least key.
    void Offer(u32 slot, u32 cost, u64 key, u32 via);
    /// Offers node `node` the way of `cost` by `via`, queued by that cost plus its estimate to the end.
    void OfferNode(const WorldMap& map, const RouteGraph& graph, const Ends& ends, u32 node, u64 cost, u32 via);
    /// Queues the start's node, or each node of its block the walk from it reaches.
    void Seed(const WorldMap& map, const RouteGraph& graph, const Ends& ends);
    /// Searches the graph from the seeded nodes; whether it reached the end more cheaply than the direct way.
    [[nodiscard]] bool Search(const WorldMap& map, const RouteGraph& graph, const Ends& ends);
    /// The route the search found, back from the end to the start.
    [[nodiscard]] GraphRoute Trace(const RouteGraph& graph, const Ends& ends) const;
    /// Whether any slot is queued.
    [[nodiscard]] bool Queued() const;
    /// The least key queued, its slots moved to the front first; the queue must not be empty.
    [[nodiscard]] u64 LeastKey();
    /// Takes the queued slot of the least (key, slot) off the queue and marks it settled.
    [[nodiscard]] u32 Pop();
    /// The bucket `key` falls in: 0, the front, for the front's key, else one more than the highest bit in which it
    /// differs from that key.
    [[nodiscard]] u32 BucketOf(u64 key) const;
    /// Queues `slot` by its key, in the bucket that key falls in.
    void Enqueue(u32 slot);
    /// Takes the queued `slot` out of its bucket above the front.
    void Unlink(u32 slot);

    /// Each slot's cost so far, valid where its stamp is the search's.
    std::vector<u32> m_cost;
    /// How each slot was reached (an edge and its direction, or for the end, the node it was reached from), valid where
    /// its stamp is the search's.
    std::vector<u32> m_via;
    /// The search that last reached each slot.
    std::vector<u32> m_stamp;
    /// Each slot's queue key, its cost so far plus its estimate, valid where its stamp is the search's.
    std::vector<u64> m_key;
    /// Each slot's bucket while it is queued, or a mark that it is settled, valid where its stamp is the search's.
    std::vector<u32> m_place;
    /// The next slot in each queued slot's bucket above the front, or none at the bucket's end.
    std::vector<u32> m_next;
    /// The previous slot in each queued slot's bucket above the front, or none at the bucket's start.
    std::vector<u32> m_prev;
    /// The queue's front: the queued slots whose key is m_least, a binary min-heap of slots, at most one entry a slot.
    std::vector<u32> m_queue;
    /// The first slot of each bucket above the front, one for each bit a key may differ in, bucket b at b - 1, valid
    /// where m_filled says it holds any.
    std::array<u32, std::numeric_limits<u64>::digits> m_first{};
    /// Which buckets above the front hold slots, bucket b as bit b - 1.
    u64 m_filled = 0;
    /// The front's key: no slot queued since it was set has a lesser one, as a search's keys never decrease.
    u64 m_least = 0;
    /// The walk inside the start's block, from the start.
    SectorWalk m_fromWalk;
    /// The walk inside the end's block, to the end.
    SectorWalk m_toWalk;
    /// The last search's stamp; 0 marks none.
    u32 m_search = 0;
    /// What the last search did.
    SearchCounts m_counts{};
};

/// The route's tiles from its start to its end, each joint between its parts once. `walk` is the caller's workspace for
/// the edges' ways, kept across calls so that only a block larger than any it has walked allocates.
[[nodiscard]] std::vector<u32> RouteTiles(
    const WorldMap& map,
    const RouteGraph& graph,
    const GraphRoute& route,
    SectorWalk& walk
);
/// The same with a workspace of its own, for a caller that asks once.
[[nodiscard]] std::vector<u32> RouteTiles(const WorldMap& map, const RouteGraph& graph, const GraphRoute& route);

} // namespace core
