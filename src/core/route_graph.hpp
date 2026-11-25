#pragma once

/// @file
/// @ingroup core
/// Routing over sectors: the world map cut into square sectors, the gates where two sectors touch and a profile can cross
/// from one to the other, and each sector's graph, whose nodes are its gates' and places' tiles and whose edges are the
/// cheapest ways between them inside the sector. Whole numbers with fixed tie-breaks, so every graph is the same on
/// every machine, whatever was built before it.

#include "core/paths.hpp"
#include "core/types.hpp"
#include "core/world_map.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace core {

/// The version of the routing rules: changes whenever a graph built from the same map would differ.
inline constexpr u32 ROUTING_VERSION = 1;
/// A sector's side in tiles.
inline constexpr u32 SECTOR_SIZE = 16;
/// The longest stretch of a border one gate serves, in tiles.
inline constexpr u32 GATE_SPACING = 8;

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
/// The sector holding `tile`.
[[nodiscard]] u32 SectorOf(const WorldMap& map, RoutingRules rules, u32 tile);

/// A search inside one sector from or to one of its tiles, and the workspace it runs in: kept by its caller and
/// walked again and again, it allocates nothing once it has held a sector of the largest size. One thread at a time.
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

/// One sector's graph by one profile.
struct SectorGraph {
    /// The node tiles, ascending: each side of a gate on any of the sector's borders and each place the profile can
    /// enter, a tile that is several of these once.
    std::vector<u32> nodes;
    /// The ways inside the sector between pairs of nodes, by (from, to); none for a pair no way inside joins.
    std::vector<SectorEdge> edges;
    /// The steps over the east and south borders, by (node, east before south).
    std::vector<SectorCrossing> crossings;

    /// Whether all are equal.
    [[nodiscard]] friend bool operator==(const SectorGraph&, const SectorGraph&) = default;
};

/// Sector `sector`'s graph by `profile`, from its own tiles and the ring of tiles round it. `walk` is the caller's
/// workspace: what it held before changes nothing.
[[nodiscard]] SectorGraph BuildSector(
    const WorldMap& map,
    ProfileIndex profile,
    RoutingRules rules,
    u32 sector,
    SectorWalk& walk
);

} // namespace core
