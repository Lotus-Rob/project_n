# Review: gd-world-sim

**Scope:** GAME DESIGN of world, travel and NPC simulation as implemented and specified: world map and look, roads/rivers/fog, journeys and travel, NPCs and relationships, world-kept delta saves (src/core/world_map*, journey*, src/game/{travel,npcs,world_state}*, assets/data/world*.yaml, specs chunk5*, relationships-bounded, roads, travellers, shared-routes in .claude/worktrees/shared-routes/docs/superpowers/specs/ if present): does it scale in design terms to a living world, is world.yaml-as-test-map vs procedural generation coherent, is there a plan for the generator, time model (days/months/years), what the player actually does on the map.

**Findings:** 23 standing (0 critical, 0 high, 4 medium, 18 low, 1 info), 0 raised but refuted on verification.

**Parts:** gd-world-sim.md, gd-world-sim-part2.md, gd-world-sim-part3.md

## Assessment

## Scope: game design of the world map, travel and the NPC simulation (gd-world-sim)

**Strengths**
- The engineering underneath is very disciplined. Map derivation uses whole numbers with fixed tie-breaks. Journeys are pure-function records (`core::Where`, `WalkedAt`), so one month step decides travel and positions are computed at publish. Travel is deterministic across worker counts and tested. Saves rebuild journeys from plans and check them byte for byte. Every risky number is bounded (MAX_WALK_COST, MOST_WORLD_PEOPLE, MAX_SIGHT, the journal and relationship caps). Refusals name the YAML line they come from.
- The shape left room for transport: movement profiles, leg shapes, journeys with several members, and power bands.
- Specs are unusually honest. Outcome sections record misses (5c1's save-time aim, unbounded relationship growth) with measurements, and fixes followed (relationships-bounded).
- Presentation is kept apart from simulation: `world_look.yaml`, drawn road shifts and walk tables are never hashed.

**Weaknesses (design)**
1. **"Procedural per seed" (decided 2026-11-25) does not fit the current architecture, and nothing plans the generator.**
   - The map is game data read from `world.yaml` once per process, before any seed exists.
   - A save's base identity hashes that file.
   - Place names are `strings.yaml` keys. `world_look.yaml` names the place key `capital`. Measurement anchors and every shipped-world test pin the test map.
   - The only generator is an offline Python script whose output is then hand-edited. Map rules (where rivers rise, danger sites forking roads, no long straight kind edges) exist only as hand edits and shipped-data tests.
   - The in-flight shared-routes spec builds a synthetic map maker for benchmarks only, "not the game's world generator".
   - The docs still call `world.yaml` "the source from then on".
2. **Delta saves rest on regenerating the world bit for bit.**
   - A load rebuilds the base from seed, generator version and data hash, then applies a delta. Nothing checks that the rebuilt base equals the saver's.
   - `GENERATOR_VERSION` is bumped by hand only; it has stayed 1 through a change to how travel is generated.
   - FP contraction is left unset on purpose.
   - With a float-noise procedural generator, a save moved to another build or OS would load onto a different world without any error.
   - The delta has also stopped paying for the world's people: all 50,000 NPCs change every month, and the delta costs more bytes per NPC than writing them whole.
3. **The simulation drifts from its own tuning.**
   - Power grows by 1 in every month an NPC cultivates, with no limit. Within about the first in-game year (roughly 10-20 minutes of world-map play), nearly every "mortal trader" becomes a "cultivator". The wary danger routes, trader draws and reach limits that travel was tuned against stop mattering.
   - Tests check traffic only in the first two months.
4. **The world is alive to watch but inert to touch.**
   - Visits do nothing: travellers don't socialise, nothing happens at the destination, and nothing comes home with them.
   - Relationships are about 10 random meetings with the ~10,000 people sharing a city tile.
   - The player's walk ignores terrain, roads, rivers and danger (held walking crosses deep water and peaks), so these systems serve only the NPCs.
   - Places can't be entered yet, and selecting a traveller does nothing.
   - The core loop is known-deferred, but the concrete result is that the map currently gives the player no decisions.
5. **The time model is thin for a cultivation game.**
   - Months are counted up from 1 with no years or seasons ("Month 37, day 4").
   - A month takes 90 s of real time, and time stands still off the map.
   - There is no time-skip such as closed-door cultivation, and a multi-year skip would cost hundreds of month steps.
   - The chunk spec's "day length as a setting" would put a player setting into hashed simulation state.

**Scaling (towards very large generated maps)**
- **Being addressed (shared-routes):** whole-map distance maps per city.
- **Not addressed yet:**
  - Every publish evaluates every journey before culling.
  - The whole explored bitset is copied into every snapshot, and whole-map fog targets are rebuilt each time the player enters a tile.
  - Fog and ground textures hold one texel per tile, which caps map width at the GPU's texture limit (4096 is all Vulkan guarantees).
  - Each click runs an unbounded whole-map A* on the logic thread.
  - Dense per-tile layers (road index, place layer, one cost layer per profile).
  - Every NPC is an individual simulated each month. The aggregate tier (Group, Fold, Materialise, Met) exists but is unused on the world map. Shared-routes admits "at 50x the NPCs (millions), not the routes, are the cost".
- **Shared-routes spec flaw:** its place table is bounded by "the furthest reach its people can have", but reach has no bound while power grows.

**Overall health:** good engineering on a placeholder design. Code quality and determinism discipline are high, and I found no crash or undefined-behaviour bug in scope. The main risks are architectural and come from the two decisions with the widest reach: procedural maps, and saves as deltas against a regenerated base. Neither has a design yet that makes them safe together. The game-design brainstorm should run before more is built on the test map. In particular, it should decide:
- the generator and what defines a world's identity;
- the time model and time-skip;
- NPC progression and population tiers;
- what the player does on the map.

**Files read in full:** src/core/world_map.hpp, src/core/world_map.cpp, src/core/journey.hpp, src/core/journey.cpp, src/core/leg_walk_cache.hpp, src/core/leg_walk_cache.cpp, src/core/road_curve.hpp, src/core/road_curve.cpp, src/core/fog.hpp, src/core/fog.cpp, src/core/calendar.hpp, src/core/schedule.hpp, src/core/paths.hpp, src/core/saves.hpp, src/core/delta.cpp, src/game/travel.hpp, src/game/travel.cpp, src/game/npcs.hpp, src/game/npcs.cpp, src/game/world_state.hpp, src/game/world_state.cpp, src/game/scenes/world.hpp, src/game/scenes/world.cpp, src/gfx/fog.cpp, src/app/fog_fade.hpp, assets/data/world.yaml, assets/data/world_look.yaml, art/AGENTS.md, docs/superpowers/specs/2026-11-10-phase6-chunk5-world-map-design.md, docs/superpowers/specs/2026-11-15-phase6-chunk5b1-map-design.md, docs/superpowers/specs/2026-11-15-phase6-chunk5b2-moving-design.md, docs/superpowers/specs/2026-11-16-phase6-chunk5c1-world-kept-delta-saves-design.md, docs/superpowers/specs/2026-11-19-relationships-bounded-design.md, docs/superpowers/specs/2026-11-22-roads-design.md, docs/superpowers/specs/2026-11-22-player-walk-and-camera-design.md, docs/superpowers/specs/2026-11-23-phase6-chunk5b-look-travellers-design.md, .claude/worktrees/shared-routes/docs/superpowers/specs/2026-11-24-shared-routes-design.md, /tmp/claude-1000/-home-lotus-Development-project-n/5e986e63-9457-419a-a1f3-2b39adbcdc44/scratchpad/review-history.md, /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/game-design-gaps.md

## Findings

### GD-WORLD-SIM-01: The procedural-per-seed world has no generator design, and the architecture assumes one data-defined map per process

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** architecture/design · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/main.cpp:112`

`main` reads `world.yaml` once, before any seed exists. `options.data.world` is one `shared_ptr<const WorldMap>` for the whole run, and `app::KeepWorld` keeps it through reloads.

Everything below assumes that single authored map:
- the save's base identity (`GameData::BaseHash()` hashes the map data);
- place names as `strings.yaml` keys (`place_<key>`);
- `world_look.yaml`'s by-key paintings;
- `WORLD_BUSIEST` and `WORLD_PORTAL_OFFSET`;
- every `shipped_world_tests` pin (costs 126/162/173/152/562, crowd bounds, the forking links).

The only generator is `art/world_map.py`, 1,745 lines of offline Python. Its output is hand-edited afterwards: river sources, ponds, moved danger sites, kind-edge jogs. Several map rules therefore exist only as hand edits plus tests on the test map:
- rivers rise on high ground or from a pond;
- danger sites sit on road links so the roads fork;
- no straight kind edge runs 8 tiles or more;
- every band's profile reaches every city.

No spec or plan covers a game-side generator. Shared-routes Goal 14 builds a synthetic map maker for tests only, "not the game's world generator". The procedural decision is recorded (memory `procedural-world-map.md`, 2026-11-25), but nothing follows from it.

**Impact:** Every chunk built now (towns in chunk 6, the enter prompt in 5c2, encounters) sits on a fixed map:
- `WorldScene` and the renderer's `SetWorld` happen once at start;
- the look file is checked against that one map;
- strings are keyed per place.

Moving to seeded maps would touch data, app, gfx, the save identity and most of the world tests at once. The map rules now enforced only by hand would be lost silently.

**Recommendation:** Write a generator spec before chunk 6. It should decide:
- where generation runs (C++ in `game` or `core`, deterministic and integer or fixed-point);
- what a world's identity is (seed + GENERATOR_VERSION + generator parameters, not the map file);
- generated place names (as `core::NameOf` does for NPCs);
- look validation against the generator's kind and sort lists rather than a loaded map;
- per-game `SetWorld` and Paths rebuilds.

Turn the hand rules (river sources, danger forks, reachability per band, edge smoothness) into generator invariants with property tests over about 20 seeds. Keep `world.yaml` explicitly as a test and measurement fixture.

**Verification:** Facts hold: one map is loaded at start and BaseHash covers the map data. The memory note on a procedural map is also recorded, and the game-design-gaps note puts a design brainstorm before chunk 6. I did not find a generator spec, so the gap is real. The 'high' rating is too strong: it is a planning gap, not a defect, and no spec yet binds the design to a fixed map.

### GD-WORLD-SIM-02: Delta saves trust that the base regenerates bit for bit, but nothing checks it

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** determinism/saves · **Status:** contradicts-recorded-decision · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:164`

A save's world identity is `(seed, GENERATOR_VERSION, BaseHash(), ID counter)` (host.cpp:164, checked at :280). A load regenerates the base with the loading binary's code and applies field deltas: only the fields that differed from the saver's base, plus journeys 'gone' named by their place index in the base's journey list (`Travel::SaveDelta`, travel.cpp:516-524).

No hash of the regenerated base is stored or compared. Three recorded facts make that a problem:
- determinism is promised only within one binary;
- `-ffp-contract` is left unset on purpose;
- the map is to become procedural per seed, which almost certainly means float noise (`art/world_map.py` uses fbm noise).

Today the float exposure is small (`StartingPower` computes `low + (high-low)*unit` in f64, which contraction may turn into an FMA). A procedural terrain generator would regenerate a different map in another build or on another OS.

**Impact:** A save carried to a patched build, or synced between Windows and Linux (cloud saves), can load onto a slightly different world without any error:
- unchanged fields take the new base's values;
- the journeys marked gone by index remove the wrong ones;
- the loaded state no longer equals the saved one.

This breaks the user's condition that a load be 'identical, deterministic'. Nobody would notice.

**Recommendation:** Store the regenerated base's `WorldHash` (and the generated map's hash) in the save header, and refuse with a clear message on mismatch. That is cheap and makes the scheme self-verifying.

Also decide the cross-binary contract explicitly. The decision 'replays only within one binary' was scoped to replays, but delta saves turn base generation into a cross-build contract. Either keep generation integer/fixed-point (or build its translation unit with `-ffp-contract=off` and strict FP), or accept that saves don't cross binaries and say so to players.

**Verification:** host.cpp:164 and 288 store and check seed, GENERATOR_VERSION, baseHash and the ID counter. The regenerated base is not hashed. Design doc 0002 section 7.4 chooses contraction on and says 'Revisit only if replays or saves must ever match across builds', so the cross-binary risk is recorded. Delta saves arguably trigger that revisit condition, and no one has noted it. Today's float exposure is small. The claim that this contradicts a recorded decision is overstated, because the decision names this condition.

### GD-WORLD-SIM-03: Unlimited power growth turns most mortals into cultivators within about one in-game year and undoes the travel tuning

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** game-design/simulation · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/npcs.cpp:99`

`LiveMonth` adds 1.0 power for every Cultivate month, with no limit. In the needs model, Cultivate wins roughly every second or third month (starting at rest=0, company=0: month 1 Cultivate, then it alternates with Rest and Socialise). That is about +4 to +6 power a year.

Starting powers are 80% in [0,5). Several travel rules key on power:
- `VisitorOf` makes power >= 5 a 'cultivator', below that a 'trader';
- the bands move power >= 5 from `on_foot_wary` to `on_foot`;
- reach is 300 + 20 x floor(power).

Expected effect (estimated from code reading, not measured):
- most mortals pass 5 within about 6-12 months (about 9-18 minutes of world-map play at 90 s a month);
- after that, trader draws (port 6, oasis 5) attract almost nobody;
- danger-avoiding routes, and the roads forked for them, are walked by almost no one;
- after about 2 years everyone's reach covers the whole map (sect to isle costs 562, reached at power about 13).

Tests check traffic only over the first two months (`the shipped roads carry 2,400 to 3,300 at once`). The 10,000-month runs check hashes, not behaviour. Separately, game-design-gaps.md says the NPCs "never progress", which is inaccurate: they progress without limit.

**Impact:** The traffic the user approved ('what each city is for', 'power is freedom', mortals reaching the next town) holds only near the start of a game. Within one play session the world drifts to a homogeneous state the tuning never considered.

**Recommendation:** - Until the cultivation design exists, cap or slow power growth: diminishing returns, or growth only for those already cultivators.
- Base visitor kind on an occupation or role field rather than power.
- Add a long-run behaviour test: after 24 and 120 months, the shares of visitor kinds, wary walkers and crowd size stay within bands.
- Correct the game-design-gaps note.
- Raise this at the game-design brainstorm as a concrete example of placeholders interacting.

**Verification:** npcs.cpp:98-100 adds 1.0 power per Cultivate month with no cap, so growth is unbounded. The 5b2 spec line 192 lists 'a power growth model beyond today's month step' as out of scope, and game-design-gaps calls power a placeholder, so this is deliberate for now. The 'about one year' figure is an unmeasured estimate. The game-design-gaps note saying NPCs 'never progress' is slightly inaccurate, since power does grow. This is worth raising at the design brainstorm, but 'high' is too strong.

### GD-WORLD-SIM-04: Whole-map distance maps per (city, profile) grow with places x tiles

- **Severity:** medium · **Verdict:** deliberate · **Category:** performance/scaling · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/core/paths.hpp:89`

`core::Paths` keeps a u32-per-tile `DistanceMap` for every (city, profile) pair, each built by a full-map Dijkstra and kept for the run. `ChooseDestination` reads each city's map for every departing NPC.

**Impact:** Shared-routes estimates about 380 MB and 45 s at 50x the test map's area, which is unusable for procedural large maps.

**Recommendation:** Being replaced by the shared-routes route graph (stage 1 is in flight). Verify that stage 2 also removes the remaining whole-map reads (DestinationWeight via the place table).

**Verification:** The finding itself says known-deferred, with the shared-routes route graph replacing whole-map DistanceMaps. The ChooseDestination code (travel.cpp:~245) calls paths.To(city, profile).CostFrom(npc.cell) per city, so the whole-map read is real. I did not verify the 380 MB / 45 s estimate, which comes from the shared-routes spec. Medium severity is fair for the scale concern, and it is a tracked deferral.

### GD-WORLD-SIM-05: GENERATOR_VERSION is bumped only by hand; generation changed without a bump

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** saves/process · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/world_state.hpp:30`

The doc says GENERATOR_VERSION is "raised whenever generation changes what it makes (StartingPower() included)". Generation includes `Travel::Seed`, i.e. `SetOut`, `ChooseDestination`, `DestinationWeight`, `ProfileFor`, `RouteDown` and `Visit`.

It is still 1, although `feat(game)!: send travellers by the profile their power picks` changed what `Travel::Seed` makes. That only went unnoticed because `world.yaml` changed in the same branch and so changed `BaseHash`.

No test pins the hash of a freshly generated world to prompt a bump. The only test (save_tests.cpp:1568) checks that a different version number is refused.

**Impact:** A future change to routing tie-breaks, weights or starting power, made without touching `world.yaml`, regenerates a different base under the same identity. Old saves then apply their delta onto it without error (see the base-hash finding).

**Recommendation:** Add a golden test: the `WorldHash()` of the shipped (or a small fixed) world generated from a fixed seed, pinned, with a failure message saying 'generation changed: bump GENERATOR_VERSION'. Better still, adopt the stored base hash, which removes the reliance on discipline.

**Verification:** world_state.hpp:30 still has GENERATOR_VERSION = 1, and the check depends on discipline, so the process gap is real. The base hash covers world.yaml changes, which limits the damage. I did not verify the claim about the feat(game)! commit. A golden pin would help.

### GD-WORLD-SIM-06: The world map gives the player no decisions: terrain, roads, rivers and danger serve only NPCs

- **Severity:** low (reviewer said medium) · **Verdict:** deliberate · **Category:** game-design · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:978`

Several mechanisms on the map have no effect on the player:
- **Click routes:** `FindShortestRoute` walks the shortest path whatever the ground, timed by distance (player-walk spec), so roads, terrain costs, fords and danger cost the player nothing.
- **Held walking:** has no collision and walks over deep water, peaks and rivers.
- **Danger sites:** do nothing.
- **Places:** can't be entered (5c2 is not built).
- **Selecting a traveller:** shows only an outline (the panel waits for chunk 9).
- **Fog:** exploring reveals only terrain and place names.

So the systems built in 5b1/5b2/roads (movement profiles, danger reach, bridges, fords, power bands) are invisible to play. The world also stops whenever the player is off the map.

**Impact:** The 'hub' scene has no loop yet. Anything built now (towns, encounters) will be designed without knowing whether map travel should cost time, risk or resources. The current design risks a screensaver-like map that ToI-style play (exploring for opportunities, travel time as pressure, danger as a choice) would need reworked.

**Recommendation:** At the game-design brainstorm, decide whether terrain and roads cost the player time (they would make travel time and lifespan pressure meaningful), and whether danger reach triggers encounters. If held walking stays free, at least keep it off impassable tiles so the map's geography means something. Pick a minimal 'map verb' set for 5c2 and chunk 6: enter, rest, meet a traveller, encounter.

**Verification:** The game-design-gaps memory records the brainstorm before chunk 6, with the core loop and map verbs undecided. Places cannot be entered because 5c2 is unbuilt. The facts are plausible, and the point is already tracked.

### GD-WORLD-SIM-07: The time model has no years or seasons, no time-skip, and day length marked 'a setting for later'

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** game-design/time · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/calendar.hpp:14`

The calendar is TICKS_PER_DAY 180 (3 s) and DAYS_PER_MONTH 30. There is no year unit, and the date line reads "Month {month}, day {day}" with month counting up without end (strings.yaml:154-156). A month is 90 s of real time on the world map, and time stands still in every other scene (the chunk 5 spec's call, 'until gameplay decides').

A ToI-like game measures lifespans, breakthroughs and sect events in years to centuries. At this rate 100 years is about 30 hours of map time. The genre's usual fix is a time-skip (closed-door cultivation passing months or years at once), but there isn't one; only the debug M key exists. Each month step costs about 6-8.5 ms of logic plus a ~53 ms main-thread hitch after it (known), so a 10-year skip is 120 steps, roughly 1-7 s with nothing designed to cover it.

The chunk spec also says day length is 'a setting for later'. But TICKS_PER_DAY is part of saved journeys (stays count days x TICKS_PER_DAY, journal days), so a player setting would change hashed simulation state.

**Impact:** Lifespan, ageing and realm timing (known gaps) cannot be designed well on this clock. A time-skip feature would expose month-step cost as a multi-second hang. A day-length setting would break replays and saves.

**Recommendation:** - Add years (and perhaps seasons) to `core/calendar.hpp` and the date line now; it is cheap.
- Decide in the game-design brainstorm whether time is real-time or compressed, and design a multi-month skip path: a batched month step that skips per-month presentation work and runs behind a progress screen.
- Keep day length as hashed game data (world.yaml or a calendar table), never a setting.
- Revisit the 'time stands still off the map' call alongside towns.

**Verification:** The calendar has no years, and the chunk 5 spec defers day length and time off the map 'until gameplay decides', so this is deferred rather than missed. Lifespan and ageing sit in game-design-gaps. The point that TICKS_PER_DAY is hashed state is sound. Adding years now is a cheap suggestion. I did not check the timing figures.

### GD-WORLD-SIM-08: The NPC tier model (active, world, aggregate) is unused on the world map; every person is a fully simulated individual

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** architecture/scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/npcs.hpp:5`

npcs.hpp describes three tiers: active (ticked), world (month steps) and aggregate `Group`s for unmet people, with `Materialise`, `Fold` and `Met`. In `src/`:
- nothing adds `Met`, and `SpawnGroup`, `Materialise` and `Fold` have no callers (tests only);
- `Activate` is used only by the arena, and asserts `NO_HOME`, so a homed world NPC can never be activated, which towns will need;
- `GenerateWorld` spawns all 50,000 as individuals, and `MonthStep` scores each one every month;
- `MOST_WORLD_PEOPLE` caps a world at 100,000.

Shared-routes' non-goals admit "at 50x the NPCs (millions), not the routes, are the cost".

**Impact:** A 'living world' of procedural maps 10-50x larger cannot hold its population: the month step, the save delta (all NPCs change every month) and the base copy kept for deltas all grow linearly in individuals. The tier machinery that was meant to solve this has rotted unused. Its docs promise behaviour the game world doesn't have, and the met-only paths (50-entry journals and relationships) can't be reached in play.

**Recommendation:** Make population levels of detail part of the game-design or generator brainstorm:
- aggregate people per region or settlement, only materialised near the player or when they act (travel, sects);
- individuals for named or met NPCs.

Until then, either delete the unused tier code or mark it clearly as scaffolding. Fix `Activate` for homed NPCs before chunk 6.

**Verification:** Met is read at npcs.cpp:170 and used in the pruning code. The finding says nothing adds Met in src/, and I found no emplace of it outside the transfer code at 257 and 275. The tier code appears to be scaffolding. The scaling concern follows from shared-routes' own non-goal text. The finding still needs a decision, not a fix, and I did not re-verify Activate's NO_HOME assert.

### GD-WORLD-SIM-09: Visits change nothing, and relationships are random edges from the tile partition

- **Severity:** low (reviewer said medium) · **Verdict:** deliberate · **Category:** game-design/simulation · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/game/npcs.cpp:80`

Travel is the world's main visible activity, but it has no consequences:
- a traveller's month is `Action::Travel`, which sits out `LiveMonth` and is removed from `m_byCell` (npcs.cpp:343), so visitors never meet anyone at their destination;
- nothing is bought, learned or brought home;
- `Finish` just resets the month's action to Rest.

Socialise draws a partner uniformly from everyone on the same tile. About 10,500 people share the capital's tile, so relationships are 'the last 10 random strangers'. Region NPCs average about 3 per tile, and those alone never meet anyone: the relationships-bounded Outcome found 3,680 with no edges, permanently.

The edge is held by the partner only: the NPC that seeks company gains nothing.

**Impact:** The simulation the player is meant to watch has no cause and effect: travel neither feeds nor is fed by the social layer. Anything that later reads relationships (the backlogged 'travel follows relationships', chunk 9's panel) will find noise.

**Recommendation:** - Choose partners within a settlement or region with some locality weighting, not per tile.
- Let visitors socialise at the destination, which gives cross-city ties.
- Make a visit leave a mark: a relationship or journal entry with someone met, or trade.
- Fold these into the relationships design the user already expects (log plus roles).

**Verification:** The relationships work is on the backlog, and the relationships-bounded Outcome reports the edgeless NPCs. game-design-gaps says relationships have 'nothing behind them yet'. The per-tile partner draw is visible at npcs.cpp:80-93. Travel has no consequences because 5c2 and chunk 9 are not built. The facts hold, but the point is already tracked.

### GD-WORLD-SIM-10: Fog work scales with the whole map: the bitset is copied every publish and all targets rebuilt on each tile step

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** performance/scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1412`

`Host::PublishFog` copies the whole explored bitset into every snapshot: `snapshot.fog.explored.assign(...)`, every publish, even when `m_fogVersion` hasn't changed.

On the main thread, `FogFade::SetTargets` (app/fog_fade.cpp:20-35) re-assigns a 2 x tiles float target array and loops over every tile whenever the source changes. The source includes `playerTile`, so this happens every time the cultivator enters a new tile. `Snap` and the upload are also whole-map.

The fog texture (and 5b-look's ground kind texture) holds one texel per tile. `WorldMap` allows up to 2^24 tiles in any shape, but Vulkan guarantees only a 4096-texel `maxImageDimension2D`.

**Impact:** On the 12,750-tile test map this is negligible. On a generated 2048 x 2048 map:
- 512 KB is copied per publish (about 30 MB/s);
- about 32 MB of floats are rewritten per tile stepped, which would hitch a walk;
- textures of 16 MB or more are uploaded.

Maps wider than 4096 tiles fail on minimum-spec GPUs.

**Recommendation:** - Copy the bitset only when its version changed (the snapshot's buffer can keep the last copy).
- Recompute targets only for the old and new sight diamonds plus newly explored tiles.
- Upload dirty rectangles.
- Plan tiled (paged) fog and ground textures, or cap map width, in the generator spec.

**Verification:** Checked host.cpp PublishFog (line ~1412): explored.assign runs on every publish regardless of m_fogVersion. FogFade::SetTargets reassigns 2*tiles floats and loops all tiles on each call. Both are whole-map work. The 4096 texture limit claim is plausible but I did not check the WorldMap cap. Only matters once maps are generated at large size, so low to medium now.
