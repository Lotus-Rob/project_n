#pragma once

/// @file
/// @ingroup core
/// The tiles, costs and walk marks of the route graph's edges, made once for each edge a traveller uses and kept, so
/// travellers hold a reference to a shared segment instead of a copy of its tiles.

#include "core/journey.hpp"
#include "core/route_graph.hpp"
#include "core/types.hpp"
#include "core/world_map.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace core {

/// One edge's tiles with what walking them each way costs and the walk tables of both ways.
struct Segment {
    /// The tiles from the edge's `from` node to its `to` node.
    std::vector<u32> tiles;
    /// What reaching tiles[i] costs walking forward; cost[0] is 0.
    std::vector<u32> cost;
    /// What reaching tiles[i] costs walking back from the last tile; costBack.back() is 0.
    std::vector<u32> costBack;
    /// The walk marks of a leg over `tiles` from the first tile's centre (MakeWalkMarks()).
    std::vector<LegWalkMark> walk;
    /// The walk marks of a leg over the tiles reversed, indexed along that walk.
    std::vector<LegWalkMark> walkBack;

    /// The bytes it holds, by capacity, itself included.
    [[nodiscard]] std::size_t Bytes() const;
    /// The share of Bytes() the two walk tables hold.
    [[nodiscard]] std::size_t WalkBytes() const;
};

/// The segment of edge `edge` of `graph`, made alone; `walk` is the caller's workspace.
[[nodiscard]] Segment MakeSegment(const WorldMap& map, const RouteGraph& graph, u32 edge, SectorWalk& walk);

/// The segments made so far, one slot per edge, sized once: a made segment never moves and a reader takes no lock. Add()
/// is called only where nothing reads.
class SegmentStore {
public:
    /// An empty store over `graph`'s edges; both arguments must outlive it.
    SegmentStore(const WorldMap& map, const RouteGraph& graph);

    /// Makes those of `edges` not yet made, in any order and with repeats allowed.
    void Add(std::span<const u32> edges);
    /// The segment of `edge`, or null until it is made.
    [[nodiscard]] const Segment* Find(u32 edge) const;
    /// How many segments are made.
    [[nodiscard]] u32 Made() const;
    /// The bytes the slots and every made segment hold.
    [[nodiscard]] std::size_t Bytes() const;
    /// The share of Bytes() the made segments' walk tables hold.
    [[nodiscard]] std::size_t WalkBytes() const;

private:
    /// The map the segments are over.
    const WorldMap* m_map;
    /// The graph whose edges they are.
    const RouteGraph* m_graph;
    /// One slot per edge, null until made.
    std::vector<std::unique_ptr<const Segment>> m_slots;
    /// How many slots are filled.
    u32 m_made = 0;
    /// The workspace Add() walks sectors in.
    SectorWalk m_walk;
    /// The tiles of the edge Add() is making, its buffer kept from call to call.
    std::vector<u32> m_tiles;
    /// The tiles reversed, the buffer Add() makes each walkBack table from.
    std::vector<u32> m_reversed;
};

} // namespace core
