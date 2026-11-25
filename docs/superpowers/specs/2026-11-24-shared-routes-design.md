# Shared routes: travel as plans over place routes worked out once

Date: 2026-11-24 · Status: **draft** (builder check done; reworked to routes between fixed places only after the user
read the first draft) · Builds on: `2026-11-23-phase6-chunk5b-look-travellers-design.md` (its Outcome), `2026-11-22-roads-design.md` ·
Replaces two sweep-up backlog items: "NEXT DESIGN after the travellers branch: per-destination walk tables + journeys
keep only their plan + NPCs set out from their city?" and "Journeys share their routes in the simulation"

## In short

- **Place routes are worked out once and shared.** One route from each city to each other fixed place of the map's data
  (cities and points of interest, today's 10 dangers among them) by each movement profile (90 a profile with today's 6
  cities and 16 places), all made when the map loads: its tiles, its step timings each way and its walk table each way.
  Nothing builds a route or a walk table at run time, and nothing reading one takes a lock.
- **A journey is its plan.** An NPC's journey keeps who travels, where, when, how long it stays and the handle of its
  route; never a copy of the route's tiles or timings.
- **NPCs set out from their city.** Every traveller leaves from and returns to its home city's tile, whether it lives
  there or on a region tile.
- **Travellers look as they do now.** Where a traveller stands, how far it has walked and which way it faces are what
  they are today for the same legs.
- **Only fixed places.** Points of interest are prebuilt in the data, never generated or random, so their routes are
  made with the map. Any other route (the player's automated travel from wherever it stands) comes later from balanced
  pathfinding spread across workers or frames: a later design.

## Intent

The user, 2026-11-23, of the travellers branch: "Most of the journeys are repeated, could we not just precalculate them
and assign a journey to someone and then keep only one copy and calculate/cache it only once?"; then agreed per-destination
walk tables, plan-only journeys and setting out from the city as the next design. On 2026-11-24, asked whether NPCs should
set out from their city: "Yes. Traveller/NPC/potentially automated player travel should all be extremely lightweight
referenced to precalculated routes/destinations." Chose approach 1 below, and asked that more dynamic routes can come
later from efficient pathfinding "spread/calculated across threads or multiple frames". On reading the first draft,
which added a route from each region NPC's tile to its city: "I thought the plan was to have city-city routes, and then
just pre-simulate random progression along those routes. Any other runtime dynamic routes can just use balanced, spread
pathfinding later on. We shouldn't be seeing 15000 routes." Then: "Well not just city-city, but prebuilt, non-generated, non-random points of
interest."

**Why:** travel is the world map's crowd, and its cost should grow with the places it connects, not with each traveller,
each month or each frame. Today every journey owns copies of its route, every month step copies thousands of them, and
the drawn travellers' walk tables are rebuilt each month into a cache behind a lock. Worked out once, routes become data
every traveller only points at.

## History

- **Journeys today** (`core/journey.hpp`): a `core::Journey` owns its parts, each leg holding `tiles` and `arrive`
  vectors; `core::Visit()` copies a route from `core::RouteDown()` over `core::Paths::To(destination, profile)` (one
  distance map per city and profile, built lazily, not thread-safe: `Warm` before parallel reads) into an out leg, a stay
  and a home leg (the out route reversed, timed by its own entering costs, which differ each way). `game::SetOut`
  (`game/travel.cpp`) routes each NPC from its own tile (`Npc::cell`: 7 in 10 on the city's tile, 3 in 10 on a drawn
  region tile), its reach weighed by `CostFrom(npc.cell)`, and skips an NPC on a tile its profile can't enter.
- **Saves already keep plans**: `Travel::SaveDelta` writes `core::VisitPlan` when `core::Rebuild()` gives the journey
  back exactly, else the journey whole. The hash (`Travel::Save` into a Hasher) hashes every journey whole.
- **Walk tables** (travellers Outcome): one `core::LegWalk` per distinct leg point sequence in `core::LegWalkCache`,
  cleared each month step and load (2,300-2,700 tables a month, about 5 MB at worst; 10,430 after 12 months without the
  clear, because NPC legs start on their own tiles), plus a 40-byte `core::KeptWalk` per journey so a drawn walk takes no
  lock after its first fill. Presentation only: never saved or hashed.
- **Tried and rejected:** lazily built tables shared behind a reader lock: publish p99 +30-90% (zoom 2.72 → 5.0 ms,
  widest 2.24 → 3.8, month step 2.64 → 4.5), about 82% of it libc++'s `shared_mutex` serialising the three publishing
  threads. Hence the rule: tables are immutable while publishing and read with no lock.
- **The bar the travellers branch held:** publish p99 within about 10% of main (follow 2.08, pan 1.88, zoom 3.00, widest
  2.24, month step 2.82, labels 2.19 ms after its fixes).
- **Considered and dropped here:** one dense table per destination over every tile (the backlog's first idea; approach
  2); routes from each region NPC's tile to its city (the first draft; the user: city routes only).
- Pre-1.0 saves and hashes may break freely; no migration (project memory).

## Goals

1. **Route store.** A `core` type holding one route per (home city, destination place, profile), the destination any
   other place of the map (a city or a point of interest: today a danger): the tiles of `RouteDown()` from the home
   city's tile to the destination's, and for each direction (out: home → destination; back:
   destination → home along the same tiles reversed) the cumulative step costs and the walk table (today's `LegWalk`
   content). `core::Paths`' distance maps, today to cities only, are kept to every place. Built whole when the map's
   store is made (a pair with no way by a profile has none), then never changed, so
   the publish and the month step's workers read it with no lock. A route is a pure function of its key and the map.
2. **Nothing routes at run time.** No month step, load or publish builds a route or a walk table.
3. **Plan-only journeys.** An NPC journey is a compact record: members, home, destination, departure tick, stay days,
   stay seed, profile and its route's handle. Its parts (the way out, the stay, the way back) and their ticks are worked
   out from the record and the store, each starting where and when the one before ends. Travellers spread along the roads
   by their random departure ticks within the month (as today), and a new world's roads are busy from the first frame by
   `Travel::Seed`'s month before the start (as today).
4. **Setting out from the city.** Every NPC's journey leaves from and returns to its home city's tile; its own tile only
   says where it lives. Its reach (the destination's weight) is the home → destination cost, the same for all of a city's
   people by one profile (Decision 3). Travellers still visit only cities: sending them to points of interest is a later
   gameplay change that needs no new routing.
5. **Same look.** `Where`, the drawn road shift (`DrawnWhere`) and the walk (`WalkedAt`: distance, moving, facing) give for
   a journey's legs what they give today for legs over the same points, facing looking back and ahead across part
   boundaries as now. Distance may differ from today's in its last bits where it is now summed differently (presentation
   only), but every way of working out a walk agrees to the bit with the from-scratch definition, as today's tests require.
6. **What goes.** `core::LegWalkCache`, `core::LegWalks`, `core::KeptWalk`, the `WalkedAt` overloads taking them,
   `Host::LegWalkTables()`, `Host::KeptWalks()`, the kept walks' sizing and stamp (`Travel::JourneysStamp()` if nothing else
   reads it), the copying `core::Visit()` and `core::Rebuild()` for NPCs, and `SaveDelta`'s whole-journey fallback for NPC
   journeys.
7. **Hash and saves.** The world hash covers each journey's record (the plan fields, not route contents, which are a
   function of the hashed map); `WORLD_VERSION` moves to 9. A save keeps the records; a load resolves each route handle
   from the store and refuses a record whose route doesn't exist (a city or profile the map lacks, a pair with no way).
8. **Room for dynamic routes later** (not built): routes from anywhere but a city to a place come from a later balanced pathfinder
   spread across worker threads or over several frames, added between publishes into a store of the same shape. The
   simulation must never depend on when such a route finished: either the tick that needs it works it out (split across
   workers), or a request on tick N is guaranteed ready by tick N+k, the tick waiting if it isn't.
9. **Measured, not assumed:** travel's memory (journeys plus the route store, against main's journeys plus its walk
   cache), the month step's time, the travellers under way, and `measure --runs world` publish p99 per segment against
   main, before and after. Publish p99 no worse than main's by more than the travellers branch's ~10% bar; the month step
   and travel's memory lower than main's.

## Non-goals

- The player's click route (`Host::PlayerRoute()`): it starts from the exact foot by the shortest way paced by distance
  (`MakePacedLeg`), one unshared leg, and stays as it is. Automated player travel along stored routes is a later feature.
- Routes from anywhere but a city, or to anything but a place; dynamic or background route building (Goal 8 only keeps
  the door open).
- Travellers visiting points of interest, generated or random points of interest, NPCs moving home.
- Changing routing itself: `RouteDown` over `Paths`' distance maps, its tie-breaks and the roads stay.

## Anti-goals

- Any lock, lazy build or allocation on the publish's read path.
- Simulation state depending on route handles: handles are never saved or hashed; a route's contents depend only on its
  key and the map.
- Travellers that look different for the same legs (pose, facing, road shift), or a reference picture changed by
  anything but the region NPCs now leaving from their city.

## Approaches

1. **Shared place routes in a store, all made at map build (chosen by the user).** One route per city, other place and
   profile with both directions' timings and walk tables; journeys are plans with a handle. Same look as today, nothing
   built at run time, the fewest moving parts. Grows with cities × places (6 × 15 = 90 a profile); at hundreds of
   places, pairs past every profile's reach can be left out.
2. **One table per destination and profile over every tile** (remaining walk distance and costs down the `RouteDown`
   tree). Any start tile reads it, but finding a traveller's tile at a tick means walking its route or an index (jump
   pointers) per destination; memory grows with tiles × destinations × profiles (about 140 MB at 200 places); facing
   where routes merge depends on the branch walked in, so a per-tile table can't hold it.
3. **Both:** shared route tiles plus per-destination distance tables. More moving parts, no gain at today's scale.

## Decisions (the controller's calls, for the user to check)

1. **Routes start only in cities**, where travellers live, and end at any place; one per (home, destination,
   profile), both ways in it: the way back is the way out reversed (as today),
   never `RouteDown` into the home city, so A → B and B → A are two routes that may differ.
2. **Both directions' timings and walk tables in the route** (the back direction's walk table made from the reversed
   points, as today's home leg's is), so neither direction derives the other by subtraction.
3. **Reach is the home city's cost** to the destination, which is what the NPC walks; it goes into both terms of
   `DestinationWeight` (pull and reach), as the one cost it takes today. The impassable-tile check moves to the home
   city's tile.
4. **The record holds its route's handle, resolved when the journey starts** (the month step, or a load), so the publish
   never looks a route up by key.
5. **The player's route keeps its own leg.** Where and the walk share leg-reading code between an owned leg and a stored
   route's direction (a view over tiles, costs or arrive ticks, pace, start tick and origin), so there is one `Where` and
   one walk definition, not two.
6. **Stays unchanged:** day points drawn from the stay's seed round the destination's tile, summed as today.
7. **The roads' crowd is counted, not retuned up front.** Region NPCs' journeys now start and end in the city, so their
   lengths and destinations shift. The number under way on the shipped world is counted over a year of month steps on
   main and on the branch; only if it moves by more than about 10% is `world.yaml`'s `share_per_mille` retuned to match
   main (then the reference pictures refreshed). The p99 comparison is made at about main's count either way.

## Left to the builder

- The store's container and handle type (an index into a vector of routes; a lookup by key for the month step and
  loads), and whether walk marks live in the route or beside it.
- Where the store lives (beside `core::Paths` in the host, made with the map) and how `Where`/`DrawnWhere`/`WalkedAt`
  reach it (an argument, or the map-side context the publish already has).
- Whether the compact record keeps `members` as a vector (group journeys exist) or something smaller.
- The ride-along cleanup: `core/journey.hpp` includes `core/calendar.hpp` only for `journey.cpp`.

## Tests (what each must show)

- The store holds exactly the reachable (city, other place) pairs by each profile, points of interest included, and a
  route to a city equals `RouteDown`'s tiles with today's `Visit` legs' arrive ticks for the same cities and profile.
- A region NPC's journey starts and ends on its home city's tile.
- `Where`, `DrawnWhere` and `WalkedAt` over a plan-only journey equal today's over the same legs (a handful of
  representative journeys, ticks across every part boundary), the fast walk to the bit with the from-scratch one.
- The month step gives the same journeys (and world hash) on 1 and 4 workers.
- Save and load round-trip the records; a load refuses a record naming a missing city, profile or route.

## Outcome

(Written when implemented.)
