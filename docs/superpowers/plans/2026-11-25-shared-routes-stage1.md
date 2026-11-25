# Layered Routing, Stage 1 (Core Routing) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `core` gains a route graph over sectors and gates, shared segments, a route search from any tile to any tile,
a place table and synthetic large maps, with tests and a benchmark that reports the spec's scale budgets. Nothing in
the game uses them yet.

**Architecture:**
- **Layer 1, `core/route_graph.*`.** The map is cut into sectors; each is built alone into a `SectorGraph` (its nodes,
  the edges between them inside it, its crossings east and south). `RouteGraph` assembles them, in parallel when given
  a job system.
- **Layer 2, `core/segments.*`.** An edge's tiles, costs each way and walk marks each way, made on first use and kept
  in a `SegmentStore` whose slots never move.
- **Layer 3's search, `core/route_search.*`.** A* over the graph, the two ends joined to their sectors' nodes by a
  search inside those sectors. `core/place_table.*`: one graph search per city.
- **Tests' side.** `tests/support/synthetic_map.hpp` makes maps of 1, 10 and 50 times the test map's area;
  `tests/bench/routing.cpp` (`routing_bench`) measures them.

**Tech Stack:** C++23, XMake through `pixi run project_n`, doctest, `core::Jobs`.

**Spec:** `docs/superpowers/specs/2026-11-24-shared-routes-design.md` (Goals 1-5, 7, 14; Decisions 1-3, 10, 11; the
first five and the last of its Tests). Stage 2 (route store, plan-only journeys, saves, hash, removals) gets its own
plan after this stage's measurements.

## Rulings made while planning (for the user to check)

1. **Crossings are orthogonal only.** A gate is a pair of tiles side by side across a border (same row for an east
   border, same column for a south one). No diagonal step crosses a border and sectors touching only at a corner share
   no gate. It keeps a sector's build to its own tiles plus the one-tile ring round it. Goal 5's 5% is the check.
2. **The gate rule, exactly.** Along a border a *run* is a longest stretch of consecutive pairs whose two tiles the
   profile can both enter. A run of `L` pairs is cut into `n = ceil(L / gateSpacing)` pieces, piece `k` covering
   offsets `[k × L / n, (k + 1) × L / n)` (whole-number division); each piece's gate is its pair with the least
   `CostAt(a) + CostAt(b)`, the lowest offset on a tie.
3. **A node is a tile.** Each side of a gate and each place's tile is a node; a tile that is both (or a gate of two
   borders, at a sector's corner) is one node. A place on a tile its profile can't enter is no node: nothing routes
   from or to it by that profile, as today no one sets out from such a tile.
4. **A crossing is an edge of its own**, two tiles long, so a segment never spans two sectors.
5. **Node and edge numbers are positions, not build order.** Sectors row by row; a sector's nodes in tile order; its
   edges by (lower node, higher node), then its crossings by (node, east before south). An edge's ID is its place in
   that order and is the segment's handle.
6. **One sector-local search** (`core::SectorWalk`) builds the edges, gives an edge's tiles again for its segment and
   joins a route's ends, so the three can never disagree.
7. **The place table is dense**: one `u32` per (city, place) and profile. `ReachBound` gives the search's bound; the top
   band has none, so on the shipped rules the strong profile's table covers the map (the spec's 4 bytes a pair).
8. **Segments keep today's walk marks** (`core::LegWalkMark`, 24 bytes a tile each way). A more compact form waits for
   stage 2's reader and this stage's memory numbers.
9. **Quality is unit-tested at 1× only** (debug builds search 12,750 tiles quickly; 640,000 not). The benchmark
   checks the same 5% on 10× and 50× and fails over it.
10. **The benchmark's build row is graph build plus place tables**, every profile, on 4 workers, each part printed.
11. **A missed budget or a quality miss stops the task.** The implementer reports the numbers; the controller brings
    them to the user. No test or budget is loosened.

## Global Constraints

- **Determinism.** Whole numbers, fixed tie-breaks, `core::Random` only (never `std::*_distribution`). A graph, a
  route, a segment and a place table are pure functions of the map, the profile and `RoutingRules`: the same on 1 and
  4 workers, in any build order and whatever was asked before.
- **Costs are `WorldMap::StepCost(from, to, profile)`'s.** A route's cost is the sum of its steps' costs in the
  direction walked. An edge walked back costs its `costBack`, which may exceed the cheapest way back (Decision 10).
- **Constants:** `ROUTING_VERSION = 1`, `SECTOR_SIZE = 16`, `GATE_SPACING = 8`; quality bar: cost within 5% of
  `core::FindRoute`'s, each direction.
- **Nothing existing changes behaviour.** `core::Paths`, `core::Visit`, the walk tables and the game keep working
  unchanged; `game` gains no include of the new headers.
- **Budgets** (development machine, release, 4 workers), from the spec's Goal 14:

  | | 1× | 10× | 50× |
  |---|---|---|---|
  | Graph build and place tables | 20 ms | 0.2 s | 1 s |
  | Graph and place tables in memory | 1 MB | 10 MB | 50 MB |
  | One place-to-place search (p99) | 0.1 ms | 0.5 ms | 2 ms |
  | Segments and routes of the trip model | 2 MB | 20 MB | 100 MB |

- **Tests** follow `tests/AGENTS.md`: about 20 named samples, a loop's worst value asserted once, cases in an anonymous
  namespace, no standard includes in test files (add missing ones to `tests/unit/pch.hpp`).
- **Comments** follow `src/AGENTS.md`: `///` on every declaration, plain comments only for a why, no citation of this
  plan, the spec or its goal and decision numbers.
- **Commits:** `feat(core)` for `src/core`, `test(tests)` for test-only changes, `refactor(core)` for Task 1,
  `docs(agents)` for AGENTS.md-only edits. No hashes in docs.
- **Gates per task:** rounds run `pixi run project_n test --suite core_tests --test <file stem>`; a task ends with
  `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD`.

## Review Focus

1. **A map smaller than a sector, or not a multiple of 16.** The 7 × 3 small map is one sector with no gates; the
   150 × 85 map's last column of sectors is 6 tiles wide. Tasks 3 and 4 test both.
2. **A place on a tile the profile can't enter.** No node, no route, `UNREACHABLE` in the place table, no assert.
   Tasks 4, 6 and 7 test it.
3. **A place on a gate's tile, and a gate tile at a sector's corner serving two borders.** One node, listed once.
   Task 3 tests it.
4. **A sector cut in two by impassable ground.** Its nodes aren't all joined inside it; a route between its halves
   leaves the sector and comes back. Tasks 3 and 6 test it.
5. **Costs that differ each way.** A route from A to B and one from B to A are each within 5% of the cheapest that
   way and cost what their own steps cost. Tasks 5 and 6 test both directions.

## Waves

| Wave | Tasks | Needs |
|---|---|---|
| A | 1 shared helpers, 2 synthetic maps, 3 sector build | none; no shared file |
| B | 4 route graph | 3 (same files) |
| C | 5 segments, 6 route search | 5: 1 and 4; 6: 1, 2 and 4 |
| D | 7 place table | 4 and 6 (its cross-check reads a route's cost) |
| E | 8 benchmark, measurements, docs | all; the measurement runs alone |

`tests/xmake.lua`, `tests/AGENTS.md` and `src/AGENTS.md` are edited by Task 8 only; `core/paths.*` and
`core/journey.*` by Task 1 only. New `tests/unit/core/*_tests.cpp` files register themselves.

---

### Task 1: shared helpers out of `paths.cpp` and `journey.cpp`

`implementer: sonnet` · `review: light`

**Files:**
- Modify: `src/core/paths.hpp`, `src/core/paths.cpp`, `src/core/journey.hpp`, `src/core/journey.cpp`
- Test: `tests/unit/core/paths_tests.cpp`, `tests/unit/core/journey_tests.cpp`

**Interfaces:**
- Produces, in `core/paths.hpp` (today's anonymous `CheapestEntry` and `Estimate`, bodies unchanged):
  - `[[nodiscard]] u32 CheapestEntry(const MovementProfile& profile);`
  - `[[nodiscard]] u64 StepEstimate(u32 cheapest, i64 dColumn, i64 dRow);`
- Produces, in `core/journey.hpp`:
  - `[[nodiscard]] std::vector<LegWalkMark> MakeWalkMarks(const WorldMap& map, Vec2 origin, std::span<const u32> tiles);`
    the marks `MakeLegWalk` makes for a leg with that origin and those tiles, to the bit. `MakeLegWalk` calls it.

- [ ] **Step 1: Write the failing tests.**
  - `paths_tests.cpp`, `TEST_CASE("the step estimate never exceeds a route's cost")`: on the file's existing road map,
    for 5 named tile pairs, `StepEstimate(CheapestEntry(profile), dColumn, dRow) <=` the cost of `FindRoute`'s route;
    and `CheapestEntry` of a profile whose every cost is `IMPASSABLE_COST` is 0.
  - `journey_tests.cpp`, `TEST_CASE("walk marks made from tiles are a leg's walk table")`: for an existing test leg,
    `MakeWalkMarks(map, leg.origin, leg.tiles)` equals `MakeLegWalk(map, leg).marks` field by field (`walked` and
    `length` compared with `==`).
- [ ] **Step 2: Run them.** `pixi run project_n test --suite core_tests --test paths` and `--test journey`.
  Expected: build fails, the names are undeclared.
- [ ] **Step 3: Move and factor.** Declare the three in the headers with `///` docs, define them outside the anonymous
  namespaces; `Search` and `MakeLegWalk` call them.
- [ ] **Step 4: The ride-along cleanup.** `core/journey.hpp` includes `core/calendar.hpp` only for `journey.cpp`: move
  the include to `journey.cpp`, and add it to any file that relied on it through the header (the build names them).
- [ ] **Step 5: Gates and commit.** `pixi run project_n test`, `lint --range`. Commit:
  `refactor(core): share the step estimate and the walk marks' maker`.

---

### Task 2: synthetic maps

`implementer: sonnet` · `review: full`

**Files:**
- Create: `tests/support/synthetic_map.hpp`
- Test: `tests/unit/core/synthetic_map_tests.cpp`

**Interfaces:**
- Produces, in namespace `test`:
  - `struct SyntheticSize { u32 columns; u32 rows; u32 cities; u32 dangers; };`
  - `[[nodiscard]] constexpr SyntheticSize SyntheticSizeOf(u32 scale);` scale 1: 150 × 85, 6 cities, 10 dangers;
    10: 474 × 269, 60, 100; 50: 1061 × 601, 300, 500. Only these three scales exist; another is a compile error
    in a constant expression and an assert otherwise.
  - `[[nodiscard]] inline core::WorldMap SyntheticMap(u32 scale, u64 seed);`

**The recipe** (every draw `core::Random(seed, 0, <tile or place index>, <a purpose constant per use>, <draw>)`):
- Kinds, the shipped map's names and costs: `deep_water` impassable, `shallows` 30, `sand` 14, `marsh` 25, `meadow`
  10, `forest` 16, `hills` 20, `rock` 40, `peaks` impassable.
- Terrain: whole-number value noise, two octaves (lattice cells of 24 and 6 tiles, weights 3 and 1, bilinear in fixed
  point). The sum picks a kind by fixed thresholds giving roughly 12% deep water, 5% shallows, 8% sand, 35% meadow,
  20% forest, 10% hills, 5% rock, 5% peaks. No floating point.
- Places: each drawn on a passable tile no other place holds, redrawn with the next draw index until it is; cities
  first. City 0 is the start. A city's `people` is 100, a danger's 0; keys `city_<n>`, `danger_<n>`; a danger's sort
  `beast`; `dangerReach` 3.
- Roads: city `i > 0` to the nearest city of lower index, and every city to its nearest city, by straight-line
  distance (lower index on a tie); each road one polyline of two points. A road caps impassable ground, so every city
  is joined to city 0.
- Profiles and travel, as shipped: `on_foot` and `on_foot_wary` (`danger: 40`), both 16 ticks per cost; reach base
  300, per power 20; bands `{0, on_foot_wary}`, `{5, on_foot}`. `tileSize` 256.
- `WorldMap::Build` must accept it; a refusal aborts with its message (a test support fault, as `ShippedWorld()`).

- [ ] **Step 1: Write the failing tests.**
  - `TEST_CASE("a synthetic map has its scale's size and places")`: scales 1 and 10, seed 1: `Columns()`, `Rows()` and
    the counts of cities and dangers equal `SyntheticSizeOf(scale)`'s; `SyntheticSizeOf(50)` is `{1061, 601, 300, 500}`.
  - `TEST_CASE("a synthetic map is the same for the same seed")`: `SyntheticMap(1, 7).Hash()` twice equal; differs
    from seed 8's.
  - `TEST_CASE("every synthetic city reaches the start city")`: scale 1, seeds 1, 2, 3: `FindRoute` from each city's
    tile to city 0's by `WALKING` is not empty.
  - `TEST_CASE("a synthetic map mixes open and closed ground")`: scale 1, seed 1: impassable tiles between 10% and 30%
    of all, road tiles at least 100.
- [ ] **Step 2: Run.** `pixi run project_n test --suite core_tests --test synthetic_map`. Expected: no such header.
- [ ] **Step 3: Write `SyntheticMap` and `SyntheticSizeOf`** by the recipe, header-only, `///` on each.
- [ ] **Step 4: Run.** Expected: 4 cases pass. If a threshold misses the 10-30% band, tune the thresholds, not the test.
- [ ] **Step 5: Gates and commit.** `test(tests): make synthetic maps of 1, 10 and 50 times the test map`.

---

### Task 3: one sector's graph

`implementer: opus` · `review: full`

**Files:**
- Create: `src/core/route_graph.hpp`, `src/core/route_graph.cpp`
- Test: `tests/unit/core/route_graph_tests.cpp`

**Interfaces:**
- Produces, in `core/route_graph.hpp`:

```cpp
inline constexpr u32 ROUTING_VERSION = 1;
inline constexpr u32 SECTOR_SIZE = 16;
inline constexpr u32 GATE_SPACING = 8;

struct RoutingRules {
    u32 sectorSize = SECTOR_SIZE;
    u32 gateSpacing = GATE_SPACING;
    friend constexpr bool operator==(RoutingRules, RoutingRules) = default;
};

/// Sectors across and down, the last of each short when the map isn't a multiple.
[[nodiscard]] u32 SectorColumns(const WorldMap& map, RoutingRules rules);
[[nodiscard]] u32 SectorRows(const WorldMap& map, RoutingRules rules);
/// Sector `sector`'s tiles (sectors numbered row by row).
[[nodiscard]] TileSpan SectorSpan(const WorldMap& map, RoutingRules rules, u32 sector);
[[nodiscard]] u32 SectorOf(const WorldMap& map, RoutingRules rules, u32 tile);

/// Costs and ways inside one sector from or to one of its tiles, one entry per tile of `span`, row by row.
struct SectorWalk {
    TileSpan span;
    std::vector<u32> cost;   // UNREACHABLE where no way stays inside the sector
    std::vector<u32> step;   // From(): the tile before on the way; To(): the tile after; UNREACHABLE at the end tile
    [[nodiscard]] static SectorWalk From(const WorldMap& map, ProfileIndex profile, TileSpan span, u32 tile);
    [[nodiscard]] static SectorWalk To(const WorldMap& map, ProfileIndex profile, TileSpan span, u32 tile);
    [[nodiscard]] u32 Cost(u32 tile) const;
    /// The tiles of the way between the walk's tile and `tile`, in walking order, both included; empty if unreachable.
    [[nodiscard]] std::vector<u32> Way(u32 tile) const;
};

struct SectorEdge { u16 from; u16 to; u32 cost; u32 costBack; };       // local node indices, from < to
struct SectorCrossing { u16 node; bool south; u32 cost; u32 costBack; }; // to the east or south neighbour
struct SectorGraph {
    std::vector<u32> nodes;                // tiles, ascending
    std::vector<SectorEdge> edges;         // by (from, to)
    std::vector<SectorCrossing> crossings; // by (node, east before south)
};  // each with a defaulted operator==

[[nodiscard]] SectorGraph BuildSector(const WorldMap& map, ProfileIndex profile, RoutingRules rules, u32 sector);
```

**Rules the tests don't fully pin:**
- `SectorWalk`: Dijkstra over the span's tiles only, steps priced by `StepCost(from, to, profile)` (`To()` relaxes a
  neighbour `n` by `StepCost(n, current)`, as `DistanceMap::Build` does); the queue pops by (cost, tile index); a tile's
  `step` is set only by a strictly lower cost; neighbours in `ForEachNeighbour`'s order.
- An edge `from < to`: its way is `SectorWalk::From(nodes[from]).Way(nodes[to])`, `cost` that way's cost, `costBack`
  the sum of `StepCost` over the same tiles walked back. No edge when `to` is unreachable inside the sector.
- A crossing from node tile `a` to the tile `b` beside it in the east or south sector: `cost = CostAt(b, profile)`,
  `costBack = CostAt(a, profile)`. A sector lists the gates of all four borders as nodes, but only its east and south
  crossings.
- Gates by Ruling 2, nodes by Ruling 3.

- [ ] **Step 1: Write the failing tests** on small hand-built maps (a local helper building a `WorldMapSource` of
  meadow 10, peaks impassable, optional roads at cost 4, one start city; `RoutingRules{ .sectorSize = 4,
  .gateSpacing = 2 }` unless said):
  - `"a map smaller than a sector is one sector with only its places"`: 7 × 3, default rules: 1 sector, nodes = the
    places' tiles, no crossings.
  - `"sectors at the map's edge are short"`: 10 × 6, sector size 4: 3 × 2 sectors; sector 2's span is columns 8-9,
    rows 0-3; `SectorOf` of tile (9, 5) is 5.
  - `"an open border gets a gate for every stretch of the gate spacing"`: 8 × 4 all meadow: sector 0's east border has
    2 gates, at rows 0 and 2 (the lowest offset of each piece).
  - `"a road crossing a border is a gate"`: the same with a road along row 3: the gates are rows 0 and 3.
  - `"impassable ground splits a border into runs"`: column 4 impassable at row 1: runs of 1 and 2 pairs, gates at
    rows 0 and 2.
  - `"a place on a gate's tile and a corner tile serving two borders are one node each"`: `nodes` strictly ascending,
    the shared tile once.
  - `"an edge is the cheapest way inside the sector from its lower node, and costs its own tiles back"`: a sector with
    a road entered from one side only: `cost` equals `FindRoute`'s cost on a map holding just that sector, and
    `costBack` equals the sum of `StepCost` along `Way()` reversed, which differs from `cost`.
  - `"nodes a sector's ground keeps apart share no edge"`: a wall of peaks down the middle of a sector: no edge joins
    a node left of it to one right of it.
  - `"a crossing costs the tile it enters each way"`: meadow to road: `cost == 4`, `costBack == 10`.
  - `"a sector builds the same alone as it does again"`: `BuildSector` twice, `==`.
  - `"a place on ground its profile can't enter is no node"`.
- [ ] **Step 2: Run.** `pixi run project_n test --suite core_tests --test route_graph`. Expected: no such header.
- [ ] **Step 3: Implement** the declarations above.
- [ ] **Step 4: Run.** Expected: all pass.
- [ ] **Step 5: Gates and commit.** `feat(core): build one sector's gates, nodes and edges`.

---

### Task 4: the route graph

`implementer: opus` · `review: full`

**Files:**
- Modify: `src/core/route_graph.hpp`, `src/core/route_graph.cpp`
- Test: `tests/unit/core/route_graph_tests.cpp` (new cases)

**Interfaces:**
- Consumes: Task 3's, all of it.
- Produces:

```cpp
inline constexpr u32 NO_NODE = std::numeric_limits<u32>::max();

struct GraphEdge { u32 from; u32 to; u32 cost; u32 costBack; u32 sector; }; // global nodes, from < to
struct GraphLink { u32 node; u32 edge; u32 cost; bool reversed; };          // one way out of a node

class RouteGraph {
public:
    /// Every sector built, on `jobs` when given, in turn when null.
    [[nodiscard]] static RouteGraph Build(const WorldMap& map, ProfileIndex profile, RoutingRules rules = {},
                                          Jobs* jobs = nullptr);
    /// The graph of sectors built one by one; `sectors[i]` is BuildSector(..., i).
    [[nodiscard]] static RouteGraph Assemble(const WorldMap& map, ProfileIndex profile, RoutingRules rules,
                                             std::vector<SectorGraph> sectors);
    [[nodiscard]] ProfileIndex Profile() const;
    [[nodiscard]] RoutingRules Rules() const;
    [[nodiscard]] u32 Nodes() const;
    [[nodiscard]] u32 NodeTile(u32 node) const;
    [[nodiscard]] u32 NodeAt(u32 tile) const;                       // NO_NODE for a tile that is none
    [[nodiscard]] u32 PlaceNode(u16 place) const;                   // NO_NODE (Ruling 3)
    [[nodiscard]] std::span<const u32> SectorNodes(u32 sector) const; // its nodes' tiles; its first node: FirstNode
    [[nodiscard]] u32 FirstNode(u32 sector) const;
    [[nodiscard]] std::span<const GraphEdge> Edges() const;
    [[nodiscard]] std::span<const GraphLink> LinksOf(u32 node) const; // by (node, edge)
    [[nodiscard]] std::size_t Bytes() const;                        // the capacity of everything it holds
    friend bool operator==(const RouteGraph&, const RouteGraph&);
};

/// An edge's tiles from its `from` node to its `to` node, both included.
[[nodiscard]] std::vector<u32> EdgeTiles(const WorldMap& map, const RouteGraph& graph, u32 edge);
```

- Numbering by Ruling 5: a sector's nodes are `FirstNode(sector) + local`; edge IDs run sector by sector, a sector's
  edges then its crossings. A crossing's `to` is the node at the tile beside it; its `sector` is the sector that
  lists it.
- `EdgeTiles`: a crossing gives its two tiles; another edge `SectorWalk::From(span of edge.sector,
  NodeTile(from)).Way(NodeTile(to))`.
- `Build` forks one chunk per run of sectors (the builder picks the run's length), each writing only its own slots,
  then calls `Assemble`.

- [ ] **Step 1: Write the failing tests.**
  - `"the graph is the same built on 1 and 4 workers and without a job system"`: a 40 × 24 hand map with roads and
    peaks, sector size 8: `Build` with `nullptr`, `Jobs{1}` and `Jobs{4}`, all `==`.
  - `"the graph assembled from sectors built in any order is the same"`: `BuildSector` for every sector in reverse
    order into its slot, `Assemble`, `==` `Build`.
  - `"rebuilding one sector alone gives the same sector"`: replace one slot by a fresh `BuildSector`, `Assemble`, `==`.
  - `"each crossing joins the nodes either side of its border"`: for every crossing edge, `NodeTile(from)` and
    `NodeTile(to)` are side by side in different sectors, and the east or south sector's node list holds `to`.
  - `"a node's links name each of its edges once, each way's cost"`: for 5 named nodes, a link's `cost` is the edge's
    `cost` when it leaves `from`, `costBack` when `reversed`.
  - `"an edge's tiles start and end on its nodes and cost what the edge says each way"`: every edge of the hand map,
    the worst mismatch asserted once (forward sum `== cost`, backward sum `== costBack`).
  - `"a place's node is its tile's, and a place its profile can't enter has none"`.
  - `"a profile that can enter nothing has an empty graph"`: `Nodes() == 0`, no assert.
- [ ] **Step 2: Run.** `pixi run project_n test --suite core_tests --test route_graph`. Expected: undeclared names.
- [ ] **Step 3: Implement.** Adjacency as one array of links with a first-link index per node; `NodeAt` by a binary
  search of the tile's sector's nodes.
- [ ] **Step 4: Run.** Expected: all pass, Task 3's too.
- [ ] **Step 5: Gates and commit.** `feat(core): assemble the sectors into a route graph, in parallel`.

---

### Task 5: segments

`implementer: sonnet` · `review: full`

**Files:**
- Create: `src/core/segments.hpp`, `src/core/segments.cpp`
- Test: `tests/unit/core/segments_tests.cpp`

**Interfaces:**
- Consumes: `RouteGraph`, `EdgeTiles` (Task 4); `MakeWalkMarks`, `LegWalkMark` (Task 1).
- Produces:

```cpp
struct Segment {
    std::vector<u32> tiles;            // from the edge's `from` node to its `to` node
    std::vector<u32> cost;             // to reach tiles[i] walking forward; cost[0] == 0
    std::vector<u32> costBack;         // to reach tiles[i] walking back from the last; costBack.back() == 0
    std::vector<LegWalkMark> walk;     // MakeWalkMarks(map, centre of tiles.front(), tiles)
    std::vector<LegWalkMark> walkBack; // the same over the tiles reversed, indexed along that walk
    [[nodiscard]] std::size_t Bytes() const;
};
[[nodiscard]] Segment MakeSegment(const WorldMap& map, const RouteGraph& graph, u32 edge);

/// The segments made so far, one slot per edge, sized once: a made segment never moves and a reader takes no lock.
/// Add() is called only where nothing reads.
class SegmentStore {
public:
    SegmentStore(const WorldMap& map, const RouteGraph& graph); // both outlive it
    void Add(std::span<const u32> edges);                       // makes those not yet made
    [[nodiscard]] const Segment* Find(u32 edge) const;          // null until made
    [[nodiscard]] u32 Made() const;
    [[nodiscard]] std::size_t Bytes() const;                    // the slots and every made segment
};
```

- [ ] **Step 1: Write the failing tests** (the 40 × 24 hand map of Task 4, copied into a local helper):
  - `"a segment's costs are its edge's each way"`: for an edge whose costs differ each way: `cost.back() == edge.cost`,
    `costBack.front() == edge.costBack`, and each entry is the running sum of `StepCost` that way.
  - `"a segment's walk marks are a leg's over its tiles, each way"`: `walk` equals `MakeLegWalk` of a leg over `tiles`
    from the first tile's centre, field by field; `walkBack` likewise over the reversed tiles.
  - `"a crossing's segment is its two tiles"`.
  - `"the store makes a segment once and never moves it"`: `Find` null before `Add`; after two `Add`s naming the same
    edge and others, the first pointer is unchanged and `Made()` counts each edge once.
  - `"the store's bytes grow only with the segments made"`: `Bytes()` after adding 2 edges is less than after adding 6.
- [ ] **Step 2: Run.** `pixi run project_n test --suite core_tests --test segments`. Expected: no such header.
- [ ] **Step 3: Implement.** Slots: `std::vector<std::unique_ptr<const Segment>>` of `graph.Edges().size()`.
- [ ] **Step 4: Run.** Expected: all pass.
- [ ] **Step 5: Gates and commit.** `feat(core): keep each travelled edge's tiles, costs and walk marks once`.

---

### Task 6: the route search

`implementer: opus` · `review: full`

**Files:**
- Create: `src/core/route_search.hpp`, `src/core/route_search.cpp`
- Test: `tests/unit/core/route_search_tests.cpp`, `tests/unit/game/shipped_routes_tests.cpp`

**Interfaces:**
- Consumes: `RouteGraph`, `GraphLink`, `SectorWalk`, `EdgeTiles` (Tasks 3, 4); `CheapestEntry`, `StepEstimate` (Task 1);
  `test::SyntheticMap` (Task 2); `test::ShippedWorld()`.
- Produces:

```cpp
struct RouteStep { u32 edge; bool reversed; };
struct GraphRoute {
    std::vector<u32> head;        // the tiles from the start to its first node, both included; empty from a node
    std::vector<RouteStep> steps; // the graph's edges in walking order
    std::vector<u32> tail;        // the tiles from the last node to the end; empty onto a node
    u32 cost = 0;                 // head, steps (each way's own cost) and tail
    [[nodiscard]] std::size_t Bytes() const;
};
/// No route when `target` can't be entered or reached. Thread-safe: it reads the map and the graph only.
[[nodiscard]] std::optional<GraphRoute> FindGraphRoute(const WorldMap& map, const RouteGraph& graph, u32 from, u32 target);
/// The route's tiles from start to end, each joint between its parts once.
[[nodiscard]] std::vector<u32> RouteTiles(const WorldMap& map, const RouteGraph& graph, const GraphRoute& route);
```

**The search:**
- `from == target`: `head = {from}`, nothing else, cost 0.
- Start: `from` a node seeds it at cost 0; otherwise every node of its sector at
  `SectorWalk::From(from).Cost(node tile)`. End: `target` a node ends there; otherwise each node of its sector may end
  at `SectorWalk::To(target).Cost(node tile)` more.
- A* over `LinksOf`, the estimate `StepEstimate(CheapestEntry(profile), ...)` from a node's tile to `target`; the queue
  pops by (cost so far + estimate, node ID); a node's way is replaced only by a strictly lower cost.
- Both ends in one sector: the way inside it (`SectorWalk::From(from).Way(target)`) is a route of `head` alone; it
  wins unless the graph's is strictly cheaper.

- [ ] **Step 1: Write the failing tests.** `route_search_tests.cpp` (hand maps as in Task 4; `Cost(tiles)` a local
  helper summing `StepCost`):
  - `"a route's steps are walkable and cost what the route says"`: 6 named pairs on the 40 × 24 map: every step of
    `RouteTiles` has `StepCost != IMPASSABLE_COST`, the tiles start on `from` and end on `target`, their sum is
    `route.cost`.
  - `"a route between two places uses only the graph's edges"`: `head` and `tail` empty.
  - `"a route from open ground has a stretch of its own at that end only"`.
  - `"a route is the same whatever was asked before it"`: route X, then 5 others, then X again: `steps`, `head`,
    `tail`, `cost` equal; and equal on a graph built on 4 workers.
  - `"a start and an end on one tile, in one sector and on a gate work"`: three named samples with exact costs.
  - `"a route between the halves of a split sector leaves it and comes back"`: the wall map of Task 3 with a gap
    outside the sector: a route exists and has at least one step.
  - `"an end that can't be entered or reached gives no route"`: a peak tile; an island ringed by peaks; a place on a
    tile its profile can't enter.
  - `"a route from a tile that can't be entered leaves it"`: `from` on a peak beside meadow: a route, costing its
    steps.
  - `"the way there and the way back each cost their own steps"`: a pair whose costs differ: both routes hold the
    first case's checks, and their costs differ.
  - `"routes on a synthetic map cost within 5% of the cheapest, each way"`: `SyntheticMap(1, 1)`, both profiles, 10
    pairs drawn by `core::Random` among passable tiles and places (20 routes a profile): the worst
    `route.cost × 100 <= cheapest × 105` asserted once, and every pair `FindRoute` reaches has a route.
- [ ] **Step 2: `shipped_routes_tests.cpp`**, `TEST_CASE("routes between the shipped map's cities keep to its roads")`:
  for each ordered pair of the 6 cities by `WALKING`: cost within 5% of `FindRoute`'s, and the tiles without
  `TileFlag::Road` number at most the cheapest route's plus 5% of its length, rounded up; the worst of each asserted
  once.
- [ ] **Step 3: Run.** `pixi run project_n test --suite core_tests --test route_search` and
  `--suite game_tests --test shipped_routes`. Expected: no such header.
- [ ] **Step 4: Implement.**
- [ ] **Step 5: Run.** Expected: all pass. A miss of the 5% or the road bound is Ruling 11: stop and report the worst
  pairs with both costs; change neither the bound nor the sample.
- [ ] **Step 6: Gates and commit.** `feat(core): find routes from any tile to any tile over the graph`.

---

### Task 7: the place table

`implementer: sonnet` · `review: full`

**Files:**
- Create: `src/core/place_table.hpp`, `src/core/place_table.cpp`
- Test: `tests/unit/core/place_table_tests.cpp`

**Interfaces:**
- Consumes: `RouteGraph` (Task 4), `FindGraphRoute` (Task 6), `test::SyntheticMap` (Task 2).
- Produces:

```cpp
/// The furthest cost anyone moving by `profile` will go: the reach at the next band's power for each band naming it,
/// UNREACHABLE (no bound) for the last band, and for WALKING with no bands; 0 for a profile no band names.
[[nodiscard]] u32 ReachBound(const TravelRules& rules, ProfileIndex profile);

class PlaceTable {
public:
    /// One search of the graph per city, stopped past `bound`; on `jobs` when given, a city a chunk.
    [[nodiscard]] static PlaceTable Build(const WorldMap& map, const RouteGraph& graph, u32 bound = UNREACHABLE,
                                          Jobs* jobs = nullptr);
    /// What the way from `city` to `place` costs: 0 for itself; UNREACHABLE past the bound, with no way, for a place
    /// with no node, or when `city` isn't a city.
    [[nodiscard]] u32 Cost(u16 city, u16 place) const;
    [[nodiscard]] std::size_t Bytes() const;
    friend bool operator==(const PlaceTable&, const PlaceTable&);
};
```

- The reach at a power is `reachBase + whole(power) × reachPerPower`, saturating at `UNREACHABLE - 1`, as
  `game/travel.cpp` counts it (copy its rounding; `game` isn't linked here).
- The search is Dijkstra over `LinksOf` from `PlaceNode(city)` by each link's `cost`; a cost above `bound` is not kept.

- [ ] **Step 1: Write the failing tests.**
  - `"the place table's cost is the cost of the route found for the same pair"`: `SyntheticMap(1, 1)`, both profiles,
    every city to 3 named places each (18 pairs a profile): `Cost == FindGraphRoute(...)->cost`, or both report none.
  - `"a city costs nothing to itself"`; `"a danger has no row"` (`Cost(danger, city) == UNREACHABLE`).
  - `"a place past the bound is unreachable in the table"`: a bound between two known costs keeps the nearer only.
  - `"a place its profile can't enter is unreachable in the table"`.
  - `"the table is the same on 1 and 4 workers"`.
  - `"the reach bound follows the bands"`: the shipped rules (base 300, per power 20, bands 0 and 5): the first band's
    profile 400, the last's `UNREACHABLE`; no bands: `WALKING` `UNREACHABLE`, profile 1 gives 0.
- [ ] **Step 2: Run.** `pixi run project_n test --suite core_tests --test place_table`. Expected: no such header.
- [ ] **Step 3: Implement.** Dense rows: cities in place order by places, `u32` each.
- [ ] **Step 4: Run.** Expected: all pass.
- [ ] **Step 5: Gates and commit.** `feat(core): table each city's cost to every place in reach`.

---

### Task 8: the benchmark, the measurements and the docs

`implementer: sonnet` · `review: full`

**Files:**
- Create: `tests/bench/routing.cpp`
- Modify: `tests/xmake.lua` (target `routing_bench`: binary, deps `core`, `add_includedirs(os.scriptdir())`, the game's
  precompiled header as `headless_bench` takes it), `tests/AGENTS.md`, `src/AGENTS.md`

**Interfaces:**
- Consumes: everything above.
- Produces: `pixi run project_n test --mode release --target routing_bench [-- --scales 1,10,50 --sector 16
  --gate-spacing 8 --workers 4 --seed 1]`: one table per scale, exit 1 over a budget or over 5%.

**What it measures, per scale** (`SyntheticMap(scale, seed)`, every profile):
1. Build: `RouteGraph::Build` for every profile then `PlaceTable::Build` with `ReachBound`, on `Jobs{workers}`; the
   best of 5 runs; both parts printed, their sum against the budget. Also the build with no job system, printed only.
2. Memory: the graphs' and tables' `Bytes()`.
3. The trip model: every city's routes to its 20 nearest places by the table's cost, each profile. Each search timed
   alone; p99 against the budget, mean printed.
4. The model's memory: a `SegmentStore` per profile given every route's edges, its `Bytes()` plus the routes'
   `Bytes()`; segments made against edges in the graph printed.
5. Quality: 20 pairs drawn by `core::Random` among places and passable tiles, each way, each profile:
   `FindGraphRoute` against `FindRoute`; the worst ratio printed and held to 5%.
6. Counts, printed: sectors, nodes, edges, gates a sector on average.

- [ ] **Step 1: Write `routing.cpp` and its target.** Log through `PN_LOG_INFO` as `headless.cpp` does; one line per
  row as `<row>: <measured> (budget <budget>) [OVER]`.
- [ ] **Step 2: Build and run it.** `pixi run project_n test --mode release --target routing_bench`. Expected: three
  tables. Run alone, nothing else building.
- [ ] **Step 3: Measure the rules.** Run again with `--sector 8` and `--sector 32`, and at sector 16 with
  `--gate-spacing 4` and `--gate-spacing 16`. Report all five tables in full to the controller. Any `OVER` at the
  default rules is Ruling 11: report, change nothing.
- [ ] **Step 4: Docs.**
  - `tests/AGENTS.md`: Scope and Rules name `routing_bench` beside `headless_bench` (what it measures, that it fails
    over the scale budgets and the 5%, run on request, never a gate) and `tests/support/synthetic_map.hpp`; Commands
    gain the line above.
  - `src/AGENTS.md`: one rule on core's routing: the files, that a graph, a route and a table are pure functions of
    the map, the profile and `RoutingRules`, that changing a rule or a tie-break raises `ROUTING_VERSION`, and that
    the game doesn't use them yet.
  - `pixi run project_n doctor --docs`. Expected: pass.
- [ ] **Step 5: Gates and commit.** `pixi run project_n test`, `lint --range`. Commits:
  `test(tests): benchmark the route graph on large synthetic maps`, then
  `docs(agents): describe core's routing and its benchmark`.

**After Task 8, the controller:** records the five tables in the ledger; brings the user Decision 1 and 2's
confirmation (16 and 8, or the better pair) with the numbers; on a changed pair, one more task sets the constants and
re-runs the suites; writes stage 1's part of the spec's Outcome; then `check`, the final review and the pull request.
