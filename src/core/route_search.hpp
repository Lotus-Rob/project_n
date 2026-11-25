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

#include <cstddef>
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
/// nowhere, are its head alone.
struct GraphRoute {
    /// The tiles from the start to its first node, both included; empty from a node.
    std::vector<u32> head;
    /// The graph's edges in walking order.
    std::vector<RouteStep> steps;
    /// The tiles from the last node to the end, both included; empty onto a node.
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
/// entered or reached. An end that is a node starts or ends there alone. A start that is no node reaches every node of its
/// block (its sector and the up to eight round it) by the cheapest way kept inside the block, and every node of the
/// end's block may end by the cheapest way inside that block to an end that is no node, each priced in the direction
/// walked. A route between two nodes uses the graph alone; when either end is no node and the end lies in the start's
/// block, the cheapest way inside that block is the route unless the graph's is strictly cheaper. It reads the map and
/// the graph only, so threads may search at once, each with its own `scratch`.
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
        /// The start's node, or NO_NODE.
        u32 fromNode = NO_NODE;
        /// The end's node, or NO_NODE.
        u32 targetNode = NO_NODE;
        /// The slot the search ends on: the end's node, or past the nodes for an end that is none.
        u32 goal = 0;
        /// What the way from the start to the end within the start's block costs, when either end is no node; else
        /// UNREACHABLE.
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
    /// Sets `slot`'s way to `cost`, by `via`, queued by `key`, if it betters what this search had for it.
    void Offer(u32 slot, u32 cost, u64 key, u32 via);
    /// Offers node `node` the way of `cost` by `via`, queued by that cost plus its estimate to the end.
    void OfferNode(const WorldMap& map, const RouteGraph& graph, const Ends& ends, u32 node, u64 cost, u32 via);
    /// Queues the start's node, or each node of its block the walk from it reaches.
    void Seed(const WorldMap& map, const RouteGraph& graph, const Ends& ends);
    /// Searches the graph from the seeded nodes; whether it reached the end more cheaply than the direct way.
    [[nodiscard]] bool Search(const WorldMap& map, const RouteGraph& graph, const Ends& ends);
    /// The route the search found, back from the end to the start.
    [[nodiscard]] GraphRoute Trace(const RouteGraph& graph, const Ends& ends) const;
    /// Takes the queued slot of the least (key, slot) off the queue and marks it settled.
    [[nodiscard]] u32 Pop();
    /// Whether `first` pops before `second`.
    [[nodiscard]] bool Before(u32 first, u32 second) const;
    /// Moves the queued entry at `position` up towards the root until its parent pops first.
    void SiftUp(std::size_t position);
    /// Moves the queued entry at `position` down until both its children pop after it.
    void SiftDown(std::size_t position);
    /// Puts `slot` at `position` in the queue and records where it is.
    void Place(std::size_t position, u32 slot);

    /// Each slot's cost so far, valid where its stamp is the search's.
    std::vector<u32> m_cost;
    /// How each slot was reached (an edge and its direction, or for the end, the node it was reached from), valid where
    /// its stamp is the search's.
    std::vector<u32> m_via;
    /// The search that last reached each slot.
    std::vector<u32> m_stamp;
    /// Each slot's queue key, its cost so far plus its estimate, valid where its stamp is the search's.
    std::vector<u64> m_key;
    /// Each slot's place in the queue, or a mark that it is settled, valid where its stamp is the search's.
    std::vector<u32> m_place;
    /// The queue: a binary heap of slots by (key, slot), at most one entry a slot.
    std::vector<u32> m_queue;
    /// The walk inside the start's block, from the start.
    SectorWalk m_fromWalk;
    /// The walk inside the end's block, to the end.
    SectorWalk m_toWalk;
    /// The last search's stamp; 0 marks none.
    u32 m_search = 0;
    /// What the last search did.
    SearchCounts m_counts{};
};

/// The route's tiles from its start to its end, each joint between its parts once.
[[nodiscard]] std::vector<u32> RouteTiles(const WorldMap& map, const RouteGraph& graph, const GraphRoute& route);

} // namespace core
