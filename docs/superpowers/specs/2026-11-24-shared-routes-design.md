# Layered routing: journeys as plans over a route graph built once per world

Date: 2026-11-24, reworked 2026-11-25 · Status: **draft** · Builds on:
`2026-11-23-phase6-chunk5b-look-travellers-design.md` (its Outcome), `2026-11-22-roads-design.md` · Replaces two sweep-up
backlog items: "NEXT DESIGN after the travellers branch: per-destination walk tables + journeys keep only their plan +
NPCs set out from their city?" and "Journeys share their routes in the simulation"

## In short

- **One routing system for every trip, on a map of any size.** The map is cut into fixed square sectors. Where two
  sectors touch and a traveller can cross, there is a gate. A small graph joins the gates and the map's places; the
  tiles between two neighbouring gates are a *segment*, worked out once and shared by every traveller crossing it.
- **A route is a list of segments**, found by searching the small graph rather than the tiles, from any tile to any
  tile. Routes between places are kept once and shared; every traveller on one only points at it.
- **A journey is its plan**: who travels, where, when, how long it stays, and handles to its routes. No journey owns a
  copy of a route.
- **Built per world, once, in parallel.** The graph is a pure function of the map, built sector by sector across the
  worker threads when a world is made. Its size and build time grow with the map's area, never with pairs of places.
- **Nothing is built, searched or locked while the game draws.**
- **NPCs set out from their city**, today's only trips: city to city. The same system later serves points of interest,
  arbitrary spots (wandering, gathering, hunting, missions) and trips that start anywhere.

## Intent

The user, 2026-11-23, of the travellers branch: "Most of the journeys are repeated, could we not just precalculate them
and assign a journey to someone and then keep only one copy and calculate/cache it only once?" On 2026-11-24:
"Traveller/NPC/potentially automated player travel should all be extremely lightweight referenced to precalculated
routes/destinations"; of a draft that routed each region NPC from its own tile: "We shouldn't be seeing 15000 routes";
"not just city-city, but prebuilt, non-generated, non-random points of interest". Asked what travellers would go to
arbitrary spots for: "Wandering, gathering, hunting, missions, sect missions, exploring, socialising, cultivating, all
manner of 'lifelike' simulation actions". Asked about maps 2 to 50 times larger, and whether routing data could be
"pregenerated in some form of 'creating world' screen and then loaded/streamed from disk". On 2026-11-25: "Assume that
the map is procedurally generated per seed, the current defined map is purely a test map", and: "We need a generic
design, efficient, fast, stable, for arbitrary 'very large' maps, which can be procedurally generated, multi-layer
journey and pathfinding."

**Why:** travel is the world's crowd and the backbone of every lifelike activity to come. Its cost must grow with the
size of the map, not with each traveller, each pair of places, each month or each frame, and one mechanism must serve a
trader between cities and a hunter heading for a spot in the woods alike.

## History

- **Journeys today** (`core/journey.hpp`): a `core::Journey` owns its parts, each leg holding `tiles` and `arrive`
  vectors; `core::Visit()` copies a route from `core::RouteDown()` over `core::Paths::To(destination, profile)` (one
  whole-map distance map per city and profile, built lazily, not thread-safe) into an out leg, a stay and a home leg
  (the out route reversed, timed by its own entering costs, which differ each way). `game::SetOut` routes each NPC from
  its own tile (`Npc::cell`: 7 in 10 on the city's tile, 3 in 10 on a region tile); `DestinationWeight` cuts a
  destination off at the NPC's reach (`reach_base` + `reach_per_power` × power), which for strong cultivators spans the
  map.
- **Saves already keep plans**: `Travel::SaveDelta` writes `core::VisitPlan` when `core::Rebuild()` gives the journey
  back exactly, else the journey whole. The hash hashes every journey whole.
- **Walk tables** (travellers Outcome): one `core::LegWalk` per distinct leg in `core::LegWalkCache`, cleared each
  month step and load (2,300-2,700 tables a month, about 5 MB), plus a 40-byte `core::KeptWalk` per journey.
  Presentation only.
- **Tried and rejected:** tables built lazily behind a reader lock while publishing: publish p99 +30-90%, about 82% of
  it libc++'s `shared_mutex` serialising the publishing threads. Hence: what the publish reads is immutable and
  lock-free.
- **The bar the travellers branch held:** publish p99 within about 10% of main (follow 2.08, pan 1.88, zoom 3.00, widest
  2.24, month step 2.82, labels 2.19 ms).
- **Why not what exists:** whole-map distance maps per destination, a route per pair of places, and one tree per city
  all grow with places × tiles or places²: fine on the 150 × 85 test map (12,750 tiles, 6 cities, 10 dangers, 2
  profiles), unusable at 50 times the area (about 640,000 tiles, 300 cities), where the per-pair drafts of this design
  came to hundreds of megabytes and tens of seconds. See Approaches.
- The map will be generated per seed; `world.yaml` is a test map. Pre-1.0 saves and hashes may break freely.

## The layers

| Layer | What it is | Size grows with | Built |
|---|---|---|---|
| 0. Tiles | `core::WorldMap`: what entering each tile costs by each profile | area | with the map |
| 1. Route graph | sectors, gates, places as nodes; edges with their cost each way | area | once per world, per sector in parallel |
| 2. Segments | the tiles of one edge, its step timings and walk table each way | edges travelled | when a route first uses the edge, never while drawing |
| 3. Routes | a list of segments with running totals, for one start, end and profile | trips in use | when first needed, never while drawing; place-to-place ones shared |
| 4. Journeys | a plan: parts in order (legs over routes, stays), each a few numbers | travellers under way | when a journey starts |

A reader of any layer during the publish takes no lock and finds everything already there.

## Goals

**The route graph (layers 1 and 2)**

1. **Sectors and gates.** The map is cut into square sectors of a fixed size (Decision 1). Along each border between two
   sectors, each unbroken run of tile pairs a profile can step across gives gates: one at the run's cheapest crossing
   (so a road crossing a border is a gate), more along a long run (Decision 2). Each place (city, danger, any later
   kind) is a node in its sector. Within a sector an edge joins each pair of nodes that the profile can walk between
   without leaving it, with the cheapest cost each way; an edge joins the two sides of each gate.
2. **A pure function of the map.** The graph depends only on the map, the profile and `ROUTING_VERSION` (Goal 12): the
   same on every machine and worker count, with `core/paths.hpp`'s fixed tie-breaks. Each sector is built from its own
   tiles alone, so sectors build in parallel and one sector can be rebuilt alone.
3. **Segments on demand, then fixed.** An edge's tiles, cumulative step costs each way and walk table each way (today's
   `LegWalk` content, compact) are worked out the first time a route uses the edge and never change after. Memory goes
   to the ground actually travelled.
4. **Any tile to any tile.** A route search joins the start and the end to their sectors' nodes by a search inside
   those sectors, then searches the graph (A* with the map's distance estimate). A route between two places uses only
   shared segments; one from or to an arbitrary tile adds a short stretch of its own at that end. Unreachable gives no
   route.
5. **Near-cheapest, stable routes.** A route's cost is within 5% of the true cheapest by tiles on a spread of sampled
   pairs (Tests), and never differs between runs, worker counts or the order routes were asked for.

**Routes and what the simulation asks (layers 3 and the place table)**

6. **Route store.** Routes are kept by (start, end, profile), each a list of (segment, direction) with running cost
   and walked-distance totals, so where a walker is at a tick is two binary searches (the route's segments, then the
   segment's tiles) and how far it has walked is one more lookup. A way back is the way out reversed (as today), read
   from the same route. Routes and segments are added only at fixed points between publishes (world build, load, the
   month step after its parallel draws) and never moved, so handles and the publish's reads stay valid with no lock.
   Routes no journey holds may be dropped at a month step once the store passes a budget.
7. **Place table.** For each city and profile, the cost to every place within the furthest reach its people can have,
   from one bounded search of the graph per city, made at world build in parallel. Choosing a destination reads it and
   searches nothing.
8. **Nothing routes while drawing, and the simulation never waits on a clock.** A journey's routes are resolved in the
   tick or month step that starts it, in its parallel chunks (a search is a pure function, so chunks may search at
   once) with new routes added at the merge. Handles are never saved or hashed.

**Journeys (layer 4)**

9. **Plan-only journeys.** A journey is a compact record: members, the plan (for a visit: home, destination, departure
   tick, stay days, stay seed, profile) and its route handles. Its parts (legs over routes, stays) and their ticks
   are worked out from the record and the store, each part starting where and when the one before ends. The record is
   a chain of parts, so later plans (out to a spot, an activity there, on to the next, home) add kinds of plan, never
   new routing.
10. **NPCs set out from their city.** Every visit leaves from and returns to the home city's tile, whether the NPC lives
    there or on a region tile; reach and pull use the place table's cost. Travellers spread along the roads by their
    random departure ticks and `Travel::Seed`'s month before the start, as today. Travellers still visit only cities.
11. **Same walk.** For a given route, `Where`, the drawn road shift (`DrawnWhere`) and the walk (`WalkedAt`: distance,
    moving, facing, looking across segment and part boundaries as legs do across parts today) are defined as today
    over the route's tiles and timings; every fast path agrees to the bit with the from-scratch definition. Which
    tiles a route takes may differ from today's (Goal 5), so reference pictures with travellers are refreshed and
    checked by eye.
12. **Hash and saves.** The world hash covers each journey's record (the plan, not handles or route contents). A save
    keeps the records; a load resolves their routes again and refuses a record with none. Because a plan only gives
    the same route under the same routing rules, `ROUTING_VERSION` (sector size, gate rule, search and tie-breaks)
    joins the save's base identity beside `GENERATOR_VERSION`; `WORLD_VERSION` moves to 9.
13. **What goes.** `core::Paths`' whole-map distance maps and `RouteDown` for travellers, `core::LegWalkCache`,
    `core::LegWalks`, `core::KeptWalk` and the `WalkedAt` overloads taking them, `Host::LegWalkTables()`,
    `Host::KeptWalks()`, the kept walks' sizing and stamp, the copying `core::Visit()` / `core::Rebuild()`, and
    `SaveDelta`'s whole-journey fallback for NPC journeys.

**Scale**

14. **Proven on large generated maps.** A deterministic synthetic map maker for tests and benchmarks (terrain by noise,
    roads, places scattered by seed; not the game's world generator) at 1, 10 and 50 times the test map's area.
    Budgets, on the development machine, release build, 4 workers:

    | | 1× | 10× | 50× |
    |---|---|---|---|
    | Graph build at world creation | 20 ms | 0.2 s | 1 s |
    | Graph and place tables in memory | 1 MB | 10 MB | 50 MB |
    | One place-to-place route search (p99) | 0.1 ms | 0.5 ms | 2 ms |
    | Segments and routes after a year of the shipped travel rules | 2 MB | 20 MB | 100 MB |

    A budget missed by measurement is brought to the user with the numbers, not silently relaxed.
15. **Measured against main on the test map:** travel's memory (journeys, routes, segments, graph against main's
    journeys, distance maps and walk cache), the month step's time, the travellers under way, and `measure --runs
    world` publish p99 per segment: no worse than main's by more than about 10%, with the month step and travel's
    memory lower.

## Non-goals (each has its place kept, none is built)

- **Activities and trips to arbitrary spots or points of interest**: the game-design brainstorm before chunk 6 gives
  them rules; Goal 4's search and Goal 9's chain of parts are their routing.
- **Routing spread over several ticks**: when one tick would need more searches than fit (thousands of trips from
  arbitrary tiles), requests made on tick N are answered by tick N + k, a fixed k, the tick waiting if the work isn't
  done; never "when it finishes".
- **A coarser graph above the sectors** (sectors grouped into regions, searched first): added when the 50× search
  budget needs it; the graph's node and edge types don't assume one level.
- **A disk cache and a "creating world" screen**: the graph is a self-contained, versioned value with no pointers into
  anything else, built per sector, so it can be written and read sector by sector under the map's hash; both come
  with the procedural map generator, when there is a build time to hide.
- **A map that changes during play** (a bridge falls): the changed sectors are rebuilt, and routes over a changed
  segment re-planned; not built.
- **Simulating far regions in less detail**: at 50× the NPCs (millions), not the routes, are the cost.
- **The player's click route** (`Host::PlayerRoute()`): from the exact foot by the shortest way paced by distance, one
  leg of its own, unchanged. It shares the code that reads a leg (Decision 6).

## Anti-goals

- Any lock, search, lazy build or allocation on the publish's read path.
- Any table that grows with places × tiles or places × places, built up front.
- Simulation state that depends on handles, on the order routes were made, on thread timing or on the worker count.
- A route that changes for the same map, ends, profile and `ROUTING_VERSION`.
- Travellers funnelled along sector borders or through gate-to-gate zigzags a player can see in open country (Goal 5's
  bar and Decision 2 exist to prevent it; checked by eye on the reference pictures).

## Approaches

1. **A route graph over sectors and gates, segments shared, routes as segment lists (chosen).** The standard answer for
   large grids (hierarchical pathfinding, HPA*). Build time and memory grow with the map's area; every sector builds
   alone, in parallel, and can be cached or rebuilt alone; one search serves place-to-place, city-to-spot and
   anywhere-to-anywhere; shared segments give the user's "one copy" for every traveller on the same stretch of road,
   even on different routes. Costs: routes are near-cheapest, not exactly cheapest (Goal 5), so they differ a little
   from today's; more machinery than a table of routes.
2. **A stored route per pair of places** (the 2026-11-24 draft, which the user picked over the two below before the
   map's size and generation came up). Simplest, and exactly today's routes; but routes grow with places², each long
   route's tiles are stored again on every route that shares its road, and trips to arbitrary spots or from anywhere
   need a second system. About 240,000 routes at 50×.
3. **A whole-map table per destination, or a tree per city** (the backlog's first idea and a later variant). Any tile
   reads it, but memory and build grow with places × tiles (about 380 MB and 45 s at 50×), limiting them by reach fails
   for strong cultivators whose reach spans the map, and trips that don't start in a city still need another system.
4. **Search the tiles at run time for every trip.** No tables, but about 2,900 trips a month on the test map at
   0.2-1 ms each (estimated) is a visible hitch already, every route is stored per journey, and it grows with both the
   crowd and the map.

## Decisions (the controller's calls, for the user to check)

1. **Sectors are 16 × 16 tiles**, to be confirmed by measuring 8, 16 and 32 on the synthetic maps (build time, search
   time, route quality); the size is part of `ROUTING_VERSION`.
2. **Gates: the cheapest crossing of each run, and one more every 8 tiles of a longer run**, so open country gets
   enough gates to stay within Goal 5's 5% and roads always have their own. Tuned with Decision 1 by the same
   measurements.
3. **One graph per profile**, built for every profile in the data; identical sectors may share storage (builder's
   call).
4. **Segments are made when first used, routes kept while used.** Up-front segments for every edge would spend most of
   their memory on ground nobody walks. The store is added to only on the logic thread between publishes, with a debug
   assert that it doesn't change during one.
5. **The way back is the way out reversed** (as today), with its own timings, so a visit needs one route.
6. **One definition of a walk.** A stored route's direction and the player's owned leg are read through one view (tiles,
   cumulative costs or arrive ticks, pace, start tick, origin), so `Where`, `DrawnWhere` and `WalkedAt` exist once.
7. **Stays unchanged**: day points from the stay's seed round the destination's tile.
8. **The roads' crowd is counted, not retuned up front.** Leaving from the city and near-cheapest routes shift journey
   lengths; the travellers under way over a year are counted on main and on the branch, and `share_per_mille` is
   retuned only if the count moves by more than about 10%.
9. **Two stages, two pull requests.** Stage 1: the route graph, segments, search, place table and synthetic maps in
   `core`, with tests and benchmarks; nothing in the game uses them yet. Stage 2: the route store, plan-only journeys,
   the month step, saves and hash on top, and the removals of Goal 13. Each gets its own plan; stage 2's is written
   after stage 1's measurements.

## Left to the builder

- Containers and handle types (stable addresses for segments and routes; a lookup by key for interning and loads).
- How start and end tiles join their sectors' nodes, and the case of both in one sector.
- The compact form of a segment's walk table, and whether a gate's crossing step belongs to a segment or the route.
- How the place table's bounded search takes the furthest reach (the travel rules' top band), and its form.
- The store's budget and which unheld routes go first.
- The synthetic map maker's recipe, and where benchmarks live (`tests/bench/`).
- The ride-along cleanup: `core/journey.hpp` includes `core/calendar.hpp` only for `journey.cpp`.

## Tests (what each must show)

- The graph is the same built on 1 and 4 workers and sector by sector in any order; rebuilding one sector alone gives
  the same sector.
- A route is the same whatever was asked before it, and its steps are all walkable by its profile, each costing what
  the map says.
- Route quality: on about 20 sampled pairs per synthetic map, cost within 5% of the true cheapest by tiles
  (`core::FindRoute`); between the test map's cities, routes keep to the roads.
- Unreachable ends give no route; a start and end in one sector, on one tile, and on a gate work.
- The place table's cost equals the cost of the route found for the same pair.
- A visit's journey starts and ends on its home city's tile; `Where`, `DrawnWhere` and `WalkedAt` over it agree with
  the from-scratch definition to the bit, at ticks across every segment and part boundary.
- The month step gives the same journeys and world hash on 1 and 4 workers; save and load round-trip the records; a
  load refuses a record with no route and a save of another `ROUTING_VERSION`.
- Benchmarks report Goal 14's table on the 1×, 10× and 50× synthetic maps.

## Outcome

(Written when implemented.)
