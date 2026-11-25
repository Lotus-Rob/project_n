#pragma once

/// @file
/// @ingroup core
/// Routing over sectors: the world map cut into square sectors, the gates where two sectors touch and a profile can cross
/// from one to the other, and each sector's graph, whose nodes are its gates' and places' tiles and whose edges are the
/// cheapest ways between them inside the sector; the sectors joined into each profile's graph of the whole map. Whole numbers
/// with fixed tie-breaks, so every graph is the same on every machine, whatever was built before it.

#include "core/types.hpp"
#include "core/world_map.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace core {

class Jobs;

/// The version of the routing rules: changes whenever a graph built from the same map would differ.
inline constexpr u32 ROUTING_VERSION = 1;
/// A sector's side in tiles.
inline constexpr u32 SECTOR_SIZE = 16;
/// The longest stretch of a border one gate serves, in tiles.
inline constexpr u32 GATE_SPACING = 8;
/// The node of a tile that is none.
inline constexpr u32 NO_NODE = std::numeric_limits<u32>::max();
/// How many sectors one chunk of a parallel build builds, every profile's.
inline constexpr u32 SECTORS_PER_CHUNK = 32;

/// The bytes `vector` holds, by its capacity: what the routing structures count as their size.
template<typename T>
[[nodiscard]] std::size_t CapacityBytes(const std::vector<T>& vector) {
    return vector.capacity() * sizeof(T);
}

/// How the map is cut into sectors and their borders into gates.
struct RoutingRules {
    /// A sector's side in tiles; at least 1.
    u32 sectorSize = SECTOR_SIZE;
    /// The longest stretch of a border one gate serves, in tiles; at least 1.
    u32 gateSpacing = GATE_SPACING;

    /// Whether both are equal.
    [[nodiscard]] friend constexpr bool operator==(RoutingRules, RoutingRules) = default;
};

/// Sectors across, the last short when the map's width isn't a multiple of the sector size.
[[nodiscard]] u32 SectorColumns(const WorldMap& map, RoutingRules rules);
/// Sectors down, the last short when the map's height isn't a multiple of the sector size.
[[nodiscard]] u32 SectorRows(const WorldMap& map, RoutingRules rules);
/// Sector `sector`'s tiles (sectors numbered row by row).
[[nodiscard]] TileSpan SectorSpan(const WorldMap& map, RoutingRules rules, u32 sector);
/// The sectors of a block: a sector and the up to eight round it, by their sector columns and rows, both included, the
/// map's edges clipping them.
struct BlockSectors {
    /// Its first sector column.
    u32 firstColumn = 0;
    /// Its last sector column.
    u32 lastColumn = 0;
    /// Its first sector row.
    u32 firstRow = 0;
    /// Its last sector row.
    u32 lastRow = 0;
    /// Sectors across the whole map, which numbers them row by row.
    u32 columns = 0;
};
/// The sectors of sector `sector`'s block.
[[nodiscard]] BlockSectors BlockSectorsOf(const WorldMap& map, RoutingRules rules, u32 sector);
/// The tiles of sector `sector`'s block: the tiles of the sectors BlockSectorsOf gives.
[[nodiscard]] TileSpan BlockSpan(const WorldMap& map, RoutingRules rules, u32 sector);
/// Calls `visit(block)` for each sector of sector `sector`'s block, row by row: the sectors BlockSpan covers.
template<typename Visit>
void ForEachBlockSector(const WorldMap& map, RoutingRules rules, u32 sector, const Visit& visit) {
    const BlockSectors block = BlockSectorsOf(map, rules, sector);
    for (u32 row = block.firstRow; row <= block.lastRow; ++row) {
        for (u32 column = block.firstColumn; column <= block.lastColumn; ++column) {
            visit((row * block.columns) + column);
        }
    }
}
/// The sector holding `tile`.
[[nodiscard]] u32 SectorOf(const WorldMap& map, RoutingRules rules, u32 tile);

/// A search inside a block of tiles (a sector, or the sectors round one) from or to one of its tiles, and the workspace
/// it runs in: kept by its caller and walked again and again, it allocates nothing once it has held a block of the
/// largest size. One thread at a time.
/// Steps are priced by WorldMap::StepCost; the search settles tiles by (cost, tile index), and a tile's step is set only
/// by a strictly cheaper way, so its ways are the same whatever the workspace held before.
class SectorWalk {
public:
    /// The costs from `tile` to each tile of `span`, by ways that stay inside it; `tile` must lie in `span`.
    void WalkFrom(const WorldMap& map, ProfileIndex profile, TileSpan span, u32 tile);
    /// The costs from each tile of `span` to `tile`, by ways that stay inside it; `tile` must lie in `span`.
    void WalkTo(const WorldMap& map, ProfileIndex profile, TileSpan span, u32 tile);
    /// The last walk's cost for `tile`, or UNREACHABLE (also for a tile outside its span, or before any walk).
    [[nodiscard]] u32 Cost(u32 tile) const;
    /// The tiles of the last walk's way between its tile and `tile`, in walking order, both included; empty if
    /// unreachable. Appended to `into` after clearing it, so a caller's vector is reused.
    void Way(u32 tile, std::vector<u32>& into) const;

private:
    /// A walk from (`towards` false) or to (`towards` true) `tile`.
    void Walk(const WorldMap& map, ProfileIndex profile, TileSpan span, u32 tile, bool towards);
    /// Whether `tile` lies in the last walk's span.
    [[nodiscard]] bool Inside(u32 tile) const;
    /// The entry of `tile`, which lies in the last walk's span, in the workspace's arrays.
    [[nodiscard]] std::size_t Local(u32 tile) const;

    /// Each entry's cost, valid where its stamp is the walk's.
    std::vector<u32> m_cost;
    /// Each entry's neighbour one step nearer the walk's tile, valid where its stamp is the walk's.
    std::vector<u32> m_step;
    /// The walk that last reached each entry.
    std::vector<u32> m_stamp;
    /// The search's queue as a heap of (cost, tile), its storage kept from walk to walk.
    std::vector<std::pair<u64, u32>> m_queue;
    /// The last walk's span.
    TileSpan m_span{};
    /// The last walk's map's width; 0 before any walk.
    u32 m_columns = 0;
    /// The last walk's tile.
    u32 m_tile = 0;
    /// The last walk's stamp; 0 marks no walk.
    u32 m_walk = 0;
    /// Whether the last walk was to its tile rather than from it.
    bool m_towards = false;
};

/// A way inside a sector between two of its nodes.
struct SectorEdge {
    /// The lower node's index in SectorGraph::nodes, where the way starts.
    u16 from = 0;
    /// The higher node's index.
    u16 to = 0;
    /// What walking the way from `from` to `to` costs: the cheapest inside the sector.
    u32 cost = 0;
    /// What walking the same tiles back from `to` to `from` costs, which may exceed the cheapest way back.
    u32 costBack = 0;

    /// Whether all are equal.
    [[nodiscard]] friend constexpr bool operator==(const SectorEdge&, const SectorEdge&) = default;
};

/// A step from a node over the sector's east or south border onto the tile beside it.
struct SectorCrossing {
    /// The node's index in SectorGraph::nodes.
    u16 node = 0;
    /// Whether it crosses the south border rather than the east one.
    bool south = false;
    /// What stepping over costs: the cost of the tile beside the node.
    u32 cost = 0;
    /// What stepping back costs: the cost of the node's tile.
    u32 costBack = 0;

    /// Whether all are equal.
    [[nodiscard]] friend constexpr bool operator==(const SectorCrossing&, const SectorCrossing&) = default;
};

/// A way from one of a sector's places to a node of a sector round it: the cheapest inside the place's block.
struct SectorPlaceEdge {
    /// The place's index in SectorGraph::nodes, where the way starts.
    u16 place = 0;
    /// The other node's tile, in one of the sectors round the place's.
    u32 tile = 0;
    /// What walking the way from the place costs: the cheapest inside the block.
    u32 cost = 0;
    /// What walking the same tiles back to the place costs, which may exceed the cheapest way back.
    u32 costBack = 0;

    /// Whether all are equal.
    [[nodiscard]] friend constexpr bool operator==(const SectorPlaceEdge&, const SectorPlaceEdge&) = default;
};

/// One sector's graph by one profile.
struct SectorGraph {
    /// The node tiles, ascending: each side of a gate on any of the sector's borders and each place the profile can
    /// enter, a tile that is several of these once.
    std::vector<u32> nodes;
    /// The ways inside the sector between pairs of nodes, by (from, to); none for a pair no way inside joins.
    std::vector<SectorEdge> edges;
    /// The steps over the east and south borders, by (node, east before south).
    std::vector<SectorCrossing> crossings;
    /// The ways from each place to every node of the sectors round its own, by (place, tile); none to a place of a lower
    /// sector, whose own ways reach this one.
    std::vector<SectorPlaceEdge> placeEdges;

    /// Whether all are equal.
    [[nodiscard]] friend bool operator==(const SectorGraph&, const SectorGraph&) = default;
};

/// Sector `sector`'s graph by `profile`, from its own tiles and the ring of tiles round it, and for a sector holding a
/// place, from its block and the ring round that. `walk` is the caller's
/// workspace: what it held before changes nothing.
[[nodiscard]] SectorGraph BuildSector(
    const WorldMap& map,
    ProfileIndex profile,
    RoutingRules rules,
    u32 sector,
    SectorWalk& walk
);

/// A way between two nodes of a route graph: inside one sector, a step over a border onto the node beside it, or a
/// place's way to a node of a sector round its own (a block edge).
struct GraphEdge {
    /// The node where the way starts: the lower one, but a block edge's place.
    u32 from = 0;
    /// The node where it ends.
    u32 to = 0;
    /// What walking it from `from` to `to` costs.
    u32 cost = 0;
    /// What walking the same tiles back from `to` to `from` costs.
    u32 costBack = 0;
    /// The sector that lists it: the one holding its tiles, for a step over a border the one it leaves, and for a block
    /// edge its place's.
    u32 sector = 0;

    /// Whether all are equal.
    [[nodiscard]] friend constexpr bool operator==(const GraphEdge&, const GraphEdge&) = default;
};

/// One way out of a node: an edge and the node at its other end.
struct GraphLink {
    /// The node at the edge's other end.
    u32 node = 0;
    /// The edge's ID.
    u32 edge = 0;
    /// What walking the edge this way costs.
    u32 cost = 0;
    /// Whether this way runs from the edge's `to` back to its `from`.
    bool reversed = false;

    /// Whether all are equal.
    [[nodiscard]] friend constexpr bool operator==(const GraphLink&, const GraphLink&) = default;
};

/// One profile's graph over the whole map: every sector's nodes and edges joined by the steps over their borders.
/// Numbers are positions: sectors row by row, a sector's nodes in tile order, so its nodes are FirstNode(sector) plus
/// their index in the sector; edge IDs run sector by sector, a sector's edges, then its crossings, then its places' block
/// edges.
class RouteGraph {
public:
    /// The graph of sectors built one by one; `sectors[i]` is BuildSector(..., i, ...).
    [[nodiscard]] static RouteGraph Assemble(
        const WorldMap& map,
        ProfileIndex profile,
        RoutingRules rules,
        std::span<const SectorGraph> sectors
    );

    /// The profile it was built for.
    [[nodiscard]] ProfileIndex Profile() const;
    /// The rules its sectors and gates follow.
    [[nodiscard]] RoutingRules Rules() const;
    /// How many nodes it has.
    [[nodiscard]] u32 Nodes() const;
    /// The tile of `node`.
    [[nodiscard]] u32 NodeTile(u32 node) const;
    /// The node on `tile`, or NO_NODE for a tile that is none.
    [[nodiscard]] u32 NodeAt(u32 tile) const;
    /// The node on place `place`'s tile, or NO_NODE when its profile can't enter that tile.
    [[nodiscard]] u32 PlaceNode(u16 place) const;
    /// The tiles of sector `sector`'s nodes, ascending; the first is node FirstNode(sector).
    [[nodiscard]] std::span<const u32> SectorNodes(u32 sector) const;
    /// The node of sector `sector`'s first node tile.
    [[nodiscard]] u32 FirstNode(u32 sector) const;
    /// Every edge, by ID.
    [[nodiscard]] std::span<const GraphEdge> Edges() const;
    /// Whether edge `edge` is a step over a border onto the node beside it, rather than a walked way.
    [[nodiscard]] bool IsCrossing(u32 edge) const;
    /// Whether edge `edge` is a place's way to a node of a sector round its own.
    [[nodiscard]] bool IsBlockEdge(u32 edge) const;
    /// The tiles of `map`, the graph's map, that edge `edge`'s way was found in: its place's block for a block edge,
    /// else its sector.
    [[nodiscard]] TileSpan WalkSpan(const WorldMap& map, u32 edge) const;
    /// The ways out of `node`, one for each of its edges, by edge ID.
    [[nodiscard]] std::span<const GraphLink> LinksOf(u32 node) const;
    /// The bytes its arrays hold, by their capacity.
    [[nodiscard]] std::size_t Bytes() const;
    /// Whether the two hold the same nodes, edges and links under the same rules; the profile isn't compared.
    [[nodiscard]] bool SameAs(const RouteGraph& other) const;

private:
    /// An empty graph, for Assemble to fill.
    RouteGraph() = default;
    /// Appends sector `sector`'s edges, crossings and block edges, built as `built`, in that order; its nodes and every
    /// sector's first node are already in place.
    void AppendSector(u32 sector, const SectorGraph& built);

    /// The profile it was built for.
    ProfileIndex m_profile = WALKING;
    /// The rules its sectors and gates follow.
    RoutingRules m_rules{};
    /// The map's width in tiles.
    u32 m_columns = 0;
    /// The map's height in tiles.
    u32 m_rows = 0;
    /// Sectors across.
    u32 m_sectorColumns = 0;
    /// Each node's tile.
    std::vector<u32> m_nodeTiles;
    /// Each sector's first node, and after the last sector's, the number of nodes.
    std::vector<u32> m_firstNode;
    /// Each place's node, or NO_NODE.
    std::vector<u32> m_placeNode;
    /// Every edge, by ID.
    std::vector<GraphEdge> m_edges;
    /// Each sector's first crossing's edge ID.
    std::vector<u32> m_firstCrossing;
    /// Each sector's first block edge's ID.
    std::vector<u32> m_firstBlockEdge;
    /// Each node's first link, and after the last node's, the number of links.
    std::vector<u32> m_firstLink;
    /// Every node's links, a node's together.
    std::vector<GraphLink> m_links;
};

/// How a build is carried out; none of it changes what is built.
struct BuildOptions {
    /// The job system the chunks run on; in turn on the calling thread when null.
    Jobs* jobs = nullptr;
    /// How many sectors a chunk builds, every profile's; at least 1.
    u32 sectorsPerChunk = SECTORS_PER_CHUNK;
    /// Whether a sector is built once for the profiles whose costs agree on it, and copied for the later ones.
    bool share = true;
};

/// Every profile's route graph over one map.
class RouteGraphs {
public:
    /// Every profile's graph over `map` by `rules`.
    [[nodiscard]] static RouteGraphs Build(const WorldMap& map, RoutingRules rules = {}, BuildOptions options = {});

    /// Profile `profile`'s graph.
    [[nodiscard]] const RouteGraph& Of(ProfileIndex profile) const;
    /// How many sectors were built rather than copied: sectors × profiles, less those shared.
    [[nodiscard]] u32 SectorsBuilt() const;
    /// The bytes every graph holds, by capacity.
    [[nodiscard]] std::size_t Bytes() const;

private:
    /// No graphs, for Build to fill.
    RouteGraphs() = default;

    /// Each profile's graph, by profile.
    std::vector<RouteGraph> m_graphs;
    /// How many sectors were built rather than copied.
    u32 m_sectorsBuilt = 0;
};

/// Whether `first` and `second` cost the same to enter on every tile a sector's graph reads of its profile: the sector and
/// the ring one tile round it, or for a sector holding a place, its block and the ring round that; so where they agree
/// the two build the same sector.
[[nodiscard]] bool SectorCostsAgree(
    const WorldMap& map,
    RoutingRules rules,
    u32 sector,
    ProfileIndex first,
    ProfileIndex second
);

/// An edge's tiles from its `from` node to its `to` node, both included, into `into` (cleared first); `walk` is the
/// caller's workspace.
void EdgeTiles(const WorldMap& map, const RouteGraph& graph, u32 edge, SectorWalk& walk, std::vector<u32>& into);

} // namespace core
