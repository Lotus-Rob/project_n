# Shared routes: travel as plans over routes worked out once

Date: 2026-11-24 · Status: **draft** (builder check done; its two questions settled as Decisions 7 and 8) · Builds on: `2026-11-23-phase6-chunk5b-look-travellers-design.md` (its Outcome),
`2026-11-22-roads-design.md` · Replaces two sweep-up backlog items: "NEXT DESIGN after the travellers branch:
per-destination walk tables + journeys keep only their plan + NPCs set out from their city?" and "Journeys share their
routes in the simulation"

## In short

- **Routes are worked out once and shared.** Every route a traveller walks is kept once, in a route store over the map:
  its tiles, its step timings each way and its walk table each way. Nothing builds a route or a walk table while the
  game draws, and nothing there takes a lock.
- **A journey is its plan.** An NPC's journey keeps who travels, where, when, how long it stays and handles to the
  routes it walks; never a copy of a route's tiles or timings.
- **NPCs set out from their city.** An NPC living on a region tile walks to its home city, travels on from there, and
  on the way back walks home to its own tile.
- **Travellers look as they do now.** Where a traveller stands, how far it has walked and which way it faces are what
  they are today for the same legs.

## Intent

The user, 2026-11-23, of the travellers branch: "Most of the journeys are repeated, could we not just precalculate them
and assign a journey to someone and then keep only one copy and calculate/cache it only once?"; then agreed per-destination
walk tables, plan-only journeys and setting out from the city as the next design. On 2026-11-24, asked whether NPCs should
set out from their city: "Yes. Traveller/NPC/potentially automated player travel should all be extremely lightweight
referenced to precalculated routes/destinations." Chose approach 1 below, and asked that more dynamic routes can come
later from efficient pathfinding "spread/calculated across threads or multiple frames".

**Why:** travel is the world map's crowd, and its cost should grow with the places it connects, not with each traveller,
each month or each frame. Today every journey owns copies of its route, every month's step copies thousands of them, and
the drawn travellers' walk tables are rebuilt each month into a cache behind a lock. Worked out once, routes become data
every traveller (and later the player's automated travel) only points at.

## History

- **Journeys today** (`core/journey.hpp`): a `core::Journey` owns its parts, each leg holding `tiles` and `arrive`
  vectors; `core::Visit()` copies a route from `core::RouteDown()` over `core::Paths::To(destination, profile)` (one
  distance map per city and profile, built lazily, not thread-safe: `Warm` before parallel reads) into an out leg, a stay
  and a home leg (the out route reversed, timed by its own entering costs, which differ each way). `game::SetOut`
  (`game/travel.cpp`) routes each NPC from its own tile (`Npc::cell`: 7 in 10 on the city's tile, 3 in 10 on a drawn
  region tile), its reach weighed by `CostFrom(npc.cell)`.
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
- **Considered and dropped here:** one dense table per destination over every tile (the backlog's first idea; approach 2).
- Pre-1.0 saves and hashes may break freely; no migration (project memory).

## Goals

1. **Route store.** A `core` type holding routes keyed by (start tile, destination city, profile): the tiles of
   `RouteDown()` from the start to the city, and for each direction (out: start → city; back: city → start along the same
   tiles reversed) the cumulative step costs and the walk table (today's `LegWalk` content). Its routes never change once
   added, and the store is only added to outside the publish (map load, world build, load, the month step), so the publish
   reads it with no lock. A route is a pure function of its key and the map.
2. **City routes precalculated, region routes while used.** Every city → city route by every profile is added when the
   map's store is made. A region route (an NPC's tile → its home city) is added by the month step for the NPCs setting out
   that month (after its parallel draws, before the journeys start) and by a load for the journeys it restores, and dropped
   at the month step once no journey holds it (Decision 7). So the store grows with the places routes connect and the
   journeys under way, never with every tile people live on.
3. **Plan-only journeys.** An NPC journey is a compact record: members, home, destination, start tile, departure tick,
   stay days, stay seed, profile, and handles to its routes. Its parts (region leg out, city leg out, stay, city leg back,
   region leg back; the region legs absent for an NPC on the city's tile) and their ticks are worked out from the record
   and the store, each part starting where and when the one before ends.
4. **Setting out from the city.** A region NPC's journey walks its tile → home city route, then home → destination, stays,
   walks back destination → home, then home → its tile; no stay at home on the way through. Its reach (the destination's
   weight) is the cost of the whole way out: tile → home plus home → destination (the controller's call; Decision 3).
5. **Same look.** `Where`, the drawn road shift (`DrawnWhere`) and the walk (`WalkedAt`: distance, moving, facing) give for
   a journey's legs what they give today for legs over the same points, facing looking back and ahead across part
   boundaries as now. Distance may differ from today's in its last bits where it is now summed differently (presentation
   only), but the fast path and the from-scratch definition agree to the bit, as today's tests require.
6. **What goes.** `core::LegWalkCache`, `core::LegWalks`, `core::KeptWalk`, the `WalkedAt` overloads taking them,
   `Host::LegWalkTables()`, `Host::KeptWalks()`, the kept walks' sizing and stamp (`Travel::JourneysStamp()` if nothing else
   reads it), the copying `core::Visit()` for NPCs, and `SaveDelta`'s whole-journey fallback for NPC journeys.
7. **Hash and saves.** The world hash covers each journey's record (the plan fields, not route contents, which are a
   function of the hashed map); `WORLD_VERSION` moves to 9. A save keeps the records; a load re-resolves the handles from
   the store, adding any route missing, and refuses a record whose routes can't exist (unreachable, impassable start, a
   profile or city the map lacks).
8. **Room for dynamic routes later** (not built): the store's interface is "ensure a route for this key, get a handle",
   so a later route builder can work routes out across worker threads or over several frames and add them between
   publishes. Whatever it does, the simulation must not depend on when a route finished: either the tick that needs a
   route works it out (split across workers), or a request on tick N is guaranteed ready by tick N+k, the tick waiting if
   it isn't. Points of interest and the player's automated travel get their routes the same way.
9. **Measured, not assumed:** travel's memory (the journeys plus the route store, against main's journeys plus its walk
   cache), the month step's time, and `measure --runs world` publish p99 per segment against main, before and after, with
   about as many travellers under way as main (Decision 8). Publish p99 no worse than main's by more than the travellers
   branch's ~10% bar; the month step and travel's memory lower than main's.

## Non-goals

- The player's click route (`Host::PlayerRoute()`): it starts from the exact foot by the shortest way paced by distance
  (`MakePacedLeg`), one unshared leg, and stays as it is. Automated player travel along stored routes is a later feature.
- Dynamic or background route building (Goal 8 only keeps the door open).
- Points of interest, new destinations, NPCs moving home: none exist yet.
- Changing routing itself: `RouteDown` over `Paths`' distance maps, its tie-breaks and the roads stay.

## Anti-goals

- Any lock, lazy build or allocation on the publish's read path.
- Simulation state depending on when or on which thread a route was built: handles are never saved or hashed; a route's
  contents depend only on its key and the map.
- Travellers that look different for the same legs (pose, facing, road shift), or a reference picture changed by
  anything but the region NPCs' new way through their city.

## Approaches

1. **Shared routes in a store, precalculated at build (chosen by the user).** Routes per (start, city, profile) with both
   directions' timings and walk tables; journeys are plans with handles. Same look as today, nothing built while
   drawing, the fewest moving parts. Grows with pairs of places (6 cities: 30 city pairs a profile, plus the region
   routes); at hundreds of places the city pairs would be capped by reach or added on first use outside the publish.
2. **One table per destination and profile over every tile** (remaining walk distance and costs down the
   `RouteDown` tree). Any start tile reads it, but finding a traveller's tile at a tick means walking its route or an
   index (jump pointers) per destination; memory grows with tiles × destinations × profiles (about 140 MB at 200 places);
   facing where routes merge depends on the branch walked in, so a per-tile table can't hold it.
3. **Both:** shared route tiles plus per-destination distance tables. More moving parts, no gain at today's scale.

## Decisions (the controller's calls, for the user to check)

1. **One key shape for every route:** (start tile, destination city, profile). A city → city route is the key whose start
   is the home city's tile; a region route is the key whose destination is the NPC's home city. The way back is always the
   way out reversed (as today), never `RouteDown` into the start.
2. **Both directions in one route record:** the tiles once, cumulative costs and walk marks per direction (the back
   direction's walk table made from the reversed points, as today's home leg's is), so neither direction derives the other
   by subtraction.
3. **Reach weighs the whole way out** (tile → home + home → destination), since that is what the NPC walks; an NPC with no
   route to its home city by its profile doesn't travel that month, as one with no reachable destination doesn't today.
4. **The record holds handles, resolved when the journey starts** (the month step after its routes are ensured, or a
   load), so the publish never looks a route up by key.
5. **The player's route keeps its own leg.** Where and the walk share leg-reading code between an owned leg and a stored
   route's direction (a view over tiles, costs or arrive ticks, pace, start tick and origin), so there is one `Where` and
   one walk definition, not two.
6. **Stays unchanged:** day points drawn from the stay's seed round the destination's tile, summed as today.
7. **Region routes are kept only while used** (from the builder check: about 15,000 region NPCs on mostly distinct tiles
   would make an up-front store larger than main's journeys and cache together, and slow every world build and load). A
   dropped route's slot may be reused for another key at a later month step; handles stay valid for as long as a journey
   holds them, and the publish never sees the store change.
8. **The roads stay about as busy.** Going through the city lengthens region NPCs' journeys and Decision 3 shortens their
   reach, so the number under way moves: `world.yaml`'s `share_per_mille` is retuned so the shipped world keeps about as
   many travellers under way as main (counted over a year of month steps), and the reference pictures are refreshed after.
   The whole way's cost goes into both terms of `DestinationWeight` (pull and reach), as the one cost it takes today.

## Left to the builder

- The store's container and handle types (an index into a vector of routes, a map from key to handle for adding and
  loading), and whether walk marks live in the route or beside it.
- How the month step gathers missing keys from its chunks (per-chunk lists merged in chunk order, then built across
  workers or in turn) so the result is the same on any worker count.
- Where the store lives (beside `core::Paths` in the host, made with the map) and how `Where`/`DrawnWhere`/`WalkedAt`
  reach it (an argument or a member of the map-side context the publish already has).
- Whether the compact record keeps `members` as a vector (group journeys exist) or something smaller.
- The ride-along cleanup: `core/journey.hpp` includes `core/calendar.hpp` only for `journey.cpp`.

## Tests (what each must show)

- A region route dropped and added again comes back the same; the month step drops only routes no journey holds.
- A route's contents are the same whichever order the store was filled in, and equal `RouteDown`'s tiles with today's
  `Visit` legs' arrive ticks for the same start, city and profile.
- A region NPC's journey goes through its home city's tile on both ways and ends on its own tile; a city NPC's has no
  region legs.
- `Where`, `DrawnWhere` and `WalkedAt` over a plan-only journey equal today's over the same legs (a handful of
  representative journeys, ticks across every part boundary), the fast walk to the bit with the from-scratch one.
- The month step gives the same journeys (and world hash) on 1 and 4 workers, including one that adds a missing route.
- Save and load round-trip the records; a load refuses a record naming an unreachable route, a missing city or profile.
- Nothing on the publish's path adds to the store (an assert in debug that the store's size doesn't change during a
  publish).

## Outcome

(Written when implemented.)
