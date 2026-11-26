# src/core/: value types, facades and routing

## Rules
- A route graph, a route, a segment and a place table are pure functions of the map, the profile and `RoutingRules`: the same on any worker count, chunk size or build order; a change to a routing rule or tie-break raises `ROUTING_VERSION` → `core/route_graph.hpp`.
- A search's caller keeps its workspace (`RouteScratch`, `SectorWalk`), one per thread; a warm search allocates nothing → `core/route_search.hpp`.
- The routing files and what each holds are in `core/module.dox` ("Routing"); the game doesn't use them yet.
