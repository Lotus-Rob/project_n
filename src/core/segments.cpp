#include "core/segments.hpp"

#include "core/assert.hpp"
#include "core/profile.hpp"

namespace core {

namespace {

/// The segment over `tiles`, walked forward from the first; `reversed` is the caller's buffer for the tiles back to front.
Segment Fill(const WorldMap& map, const RouteGraph& graph, const std::vector<u32>& tiles, std::vector<u32>& reversed) {
    PN_ASSERT(!tiles.empty(), "an edge has tiles");
    const ProfileIndex profile = graph.Profile();
    Segment segment;
    segment.tiles = tiles;
    segment.cost.resize(tiles.size());
    segment.costBack.resize(tiles.size());
    for (std::size_t index = 1; index < tiles.size(); ++index) {
        segment.cost[index] = segment.cost[index - 1] + map.StepCost(tiles[index - 1], tiles[index], profile);
    }
    for (std::size_t index = tiles.size() - 1; index > 0; --index) {
        segment.costBack[index - 1] = segment.costBack[index] + map.StepCost(tiles[index], tiles[index - 1], profile);
    }
    segment.walk = MakeWalkMarks(map, map.TileCentre(tiles.front()), tiles);
    reversed.assign(tiles.rbegin(), tiles.rend());
    segment.walkBack = MakeWalkMarks(map, map.TileCentre(reversed.front()), reversed);
    return segment;
}

/// Where a walk started and the tiles it covered: two edges with the same come from one walk.
struct WalkKey {
    /// The node walked from.
    u32 from = 0;
    /// The tiles walked over.
    TileSpan span{};

    /// Whether all are equal.
    [[nodiscard]] friend constexpr bool operator==(const WalkKey&, const WalkKey&) = default;
};

} // namespace

std::size_t Segment::Bytes() const {
    return sizeof(Segment) + CapacityBytes(tiles) + CapacityBytes(cost) + CapacityBytes(costBack) + WalkBytes();
}

std::size_t Segment::WalkBytes() const {
    return CapacityBytes(walk) + CapacityBytes(walkBack);
}

Segment MakeSegment(const WorldMap& map, const RouteGraph& graph, u32 edge, SectorWalk& walk) {
    std::vector<u32> tiles;
    EdgeTiles(map, graph, edge, walk, tiles);
    std::vector<u32> reversed;
    return Fill(map, graph, tiles, reversed);
}

SegmentStore::SegmentStore(const WorldMap& map, const RouteGraph& graph)
  : m_map(&map)
  , m_graph(&graph)
  , m_slots(graph.Edges().size()) {
}

void SegmentStore::Add(std::span<const u32> edges) {
    PN_PROFILE_ZONE();
    std::vector<u32> todo;
    for (const u32 edge : edges) {
        PN_ASSERT(edge < m_slots.size(), "an edge of the graph");
        if (m_slots[edge] == nullptr) {
            todo.push_back(edge);
        }
    }
    // Edge IDs run by sector, then kind, then the node walked from, so sorted they gather the edges that share one walk.
    std::ranges::sort(todo);
    todo.erase(std::ranges::unique(todo).begin(), todo.end());
    std::optional<WalkKey> walked;
    for (const u32 edge : todo) {
        const GraphEdge& way = m_graph->Edges()[edge];
        if (m_graph->IsCrossing(edge)) {
            m_tiles.assign({ m_graph->NodeTile(way.from), m_graph->NodeTile(way.to) });
        }
        else {
            const WalkKey key{ .from = way.from, .span = m_graph->WalkSpan(*m_map, edge) };
            if (walked != key) {
                m_walk.WalkFrom(*m_map, m_graph->Profile(), key.span, m_graph->NodeTile(way.from));
                walked = key;
            }
            m_walk.Way(m_graph->NodeTile(way.to), m_tiles);
        }
        m_slots[edge] = std::make_unique<const Segment>(Fill(*m_map, *m_graph, m_tiles, m_reversed));
        ++m_made;
    }
}

const Segment* SegmentStore::Find(u32 edge) const {
    return edge < m_slots.size() ? m_slots[edge].get() : nullptr;
}

u32 SegmentStore::Made() const {
    return m_made;
}

std::size_t SegmentStore::Bytes() const {
    std::size_t bytes = CapacityBytes(m_slots);
    for (const auto& slot : m_slots) {
        bytes += slot != nullptr ? slot->Bytes() : 0;
    }
    return bytes;
}

std::size_t SegmentStore::WalkBytes() const {
    std::size_t bytes = 0;
    for (const auto& slot : m_slots) {
        bytes += slot != nullptr ? slot->WalkBytes() : 0;
    }
    return bytes;
}

} // namespace core
