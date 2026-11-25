# Review: game-travel-npcs (part 2)

### GAME-TRAVEL-NPCS-11: ChooseDestination's thread-safety relies on a comment: Paths::To is non-const and builds lazily

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** thread-safety · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/travel.cpp:248`

SetOut runs in parallel chunks and calls the non-const `Paths::To`, which builds a missing map in place. It is safe only because SendOut warmed exactly the profiles ProfileFor can return. If ProfileFor or the warm loop changes (for example a profile chosen by something other than the bands), the chunks race on `m_maps` without any compile-time or runtime guard.

**Evidence:** paths.hpp:77 'Not thread-safe: Warm before any parallel reads'; travel.cpp:153 and 248 call `paths.To` from chunks.

**Impact:** A latent data race, which TSan would only catch when exercised.

**Recommendation:** Give Paths a const `Built(city, profile)` accessor that asserts the slot exists, and pass a `const core::Paths&` into the chunks.

**Verification:** travel.cpp:248-254 calls the non-const paths.To from chunks, and paths.hpp documents it as not thread-safe and needing a warm-up. The AGENTS.md records 'the paths by walking and by every band's profile built before the fork', so the warm-up is deliberate and consistent with ProfileFor. The missing compile-time guard is a fair latent-risk note, not a present race.

### GAME-TRAVEL-NPCS-12: Pull's integer scale rounds small or distant towns to zero weight on large maps

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** numerics · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/travel.cpp:222`

pull = people × 1,000,000 / cost², rounded down. At the reach of a strong traveller (cost about 10,000), any city under about 100 people pulls 0. A 50-person village at cost 2,000 already gets 12. On large procedural maps with costlier crossings, many legitimate destinations will quantise to 0 or near 0, which flattens the choice.

**Evidence:** travel.cpp:92 `PULL_SCALE = 1'000'000`; travel.cpp:222.

**Impact:** Distant small places are never visited on large maps; the behaviour depends on map scale.

**Recommendation:** Raise PULL_SCALE (the products still fit given MOST_WORLD_PEOPLE and MOST_DRAW), or compute the weight as a single u128 product and divide once.

**Verification:** The arithmetic is right (PULL_SCALE 1e6, pull = people*1e6/steps^2 rounded down, so 100 people at cost 1e4 gives 1 and 50 people at cost 2000 gives 12). It is also true that pull 0 makes the whole weight 0. Whether this bites depends on real cost scales on large procedural maps, which reading cannot show. The headroom claim holds: MOST_WORLD_PEOPLE is 1e5 and MOST_DRAW is 100, so the scale could be raised. I found no recorded decision about it. This is a tuning risk, not a defect.

### GAME-TRAVEL-NPCS-13: Tests miss CellAt bounds, journal merging across interleaved NPCs, Travel::Seed itself, and the generated world's identity

- **Severity:** low · **Verdict:** confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/game/travel_tests.cpp:1`

travel_tests covers Finish for one NPC and for a pair that share a journey, but not a merge in which new journal owners interleave with existing ones (lower ID, between two, above all). Travel::Seed has no direct test (only indirectly through GenerateWorld in save tests). npcs_tests checks CellAt only on cell centres, never off-edge, huge or negative points. No test pins GenerateWorld's output. None of these tests would catch the CellAt UB, a merge-order regression, or an unbumped generator change.

**Evidence:** travel_tests.cpp:111-283 (single-NPC Finish cases); npcs_tests.cpp:69-72 (CellAt only); no 'Seed' case in travel_tests.

**Impact:** Regressions in these paths would surface only through save-exactness or reference tests, if at all.

**Recommendation:** Add: a Finish case with three NPCs (journals for 1 and 5 exist, new entries for 0, 3 and 7); a Seed case checking negative departures, finished journals and that no journey ends by monthStart; CellAt cases for y = 1e12, x < 0 and a point past the last column; a golden-hash test of GenerateWorld on SmallData.

**Verification:** grep finds no Travel::Seed test in travel_tests.cpp. Seed is only exercised through GenerateWorld in save_exactness_tests. npcs_tests.cpp:70-71 checks CellAt only on cell centres. npcs.cpp:132-133 casts a float row to u32 with no upper clamp, so a huge y is undefined behaviour, which is a real latent issue. Negative y is clamped by max(…,0), and x is clamped. travel_tests has single-NPC and pair Finish cases but no interleaved-owner merge. A golden-hash test of GenerateWorld was not found, though I did not search for one exhaustively.

### GAME-TRAVEL-NPCS-14: Npc::cell means a 64-column grid cell in some scenes and a world-map tile index in others

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/npcs.hpp:67`

The same `u32 cell` is a MAP_COLUMNS = 64 grid cell (CellCentre and CellAt, 256-unit cells) without a map, and a WorldMap tile index with one. Nothing in the type stops grid helpers being used on tile indices, or the reverse. Socialising pairs NPCs by this value whatever it means, and Group::cell carries the same ambiguity.

**Evidence:** npcs.hpp:45-50 CellCentre/CellAt docs ('for scenes without the world map only'); npcs.hpp:65-67 Npc::cell doc; Activate asserts NO_HOME to avoid misuse.

**Impact:** A latent bug class: any new code that activates or positions homed NPCs with CellCentre puts them in the wrong place. The legacy grid also clutters the API.

**Recommendation:** Separate the two: a strong `TileIndex` type for map NPCs, with the arena's grid as its own scene-local component or a WorldMap of its own, so CellCentre and CellAt go away.

**Verification:** Npc::cell does mean a 64-column grid cell without a map and a tile index with one. The finding says Activate asserts NO_HOME to guard against mixing the two (npcs.cpp:255). That is a design-smell opinion about a guarded ambiguity. It is not a bug.

### GAME-TRAVEL-NPCS-15: m_travellers duplicates Npc::action == Action::Travel, adding a hash set and a lookup per NPC per month

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** simplification · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/travel.hpp:255`

The set of all journey members is kept as an `unordered_set<StableId>`. The code already guarantees that membership equals `Npc::action == Action::Travel`: SetOut sets it, Finish clears it, and Adopt refuses any mismatch. SetOut calls `month.travel->Travelling(npc)` (a hash lookup) for every world NPC every month, although it already holds `person.action`.

**Evidence:** travel.cpp:135 `if (!IsCity(map, person.home) || month.travel->Travelling(npc))`; travel.cpp:637-643 enforces membership == (action == Travel).

**Impact:** Two sources of truth that must be kept in sync by hand, plus wasted hashing and per-node allocations (100k NPCs). If they ever diverge (for example through Start() in tests, which doesn't set action), behaviour splits.

**Recommendation:** Check `person.action == Action::Travel` in SetOut. Either drop m_travellers or keep it only as a debug cross-check asserted against the action.

**Verification:** travel.cpp:135 calls month.travel->Travelling(npc), a hash lookup into m_travellers, when person.action is already at hand. Adopt enforces membership equals action==Travel, per the finding (travel.cpp:637-643, not read in detail). It is a redundant duplicate source of truth with a small cost, not a bug. I did not check whether tests call Start() without setting action, so that part of the impact claim is unverified.

### GAME-TRAVEL-NPCS-16: With assertions off, a journey without parts gets a journal entry built from made-up ticks

- **Severity:** info · **Verdict:** confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/travel.cpp:298`

Start() keeps a parts-less journey ('it ends at tick 0, so the next Finish() writes it and drops it'). EntryOf is documented as 'for journey, which has parts', but Finish calls it on such a journey, producing departureDay 0, daysAway 0 and a WALKING profile, an entry that never happened.

**Evidence:** travel.cpp:27 doc `/// The journal entry for journey, which has parts.`; travel.cpp:298 comment; core EndTick/StartTick return 0 for no parts.

**Impact:** Only reachable after an assertion failure in release; the journal shows a phantom zero-day visit.

**Recommendation:** Drop parts-less journeys in Start() as members-less ones are dropped, or skip the journal entry for them in Finish.

**Verification:** travel.cpp:298 comment says a parts-less journey is kept and written by the next Finish; EntryOf is documented 'which has parts'. Start() asserts non-empty parts, so only reachable with assertions off. Doc/behaviour mismatch is real, impact negligible.

### GAME-TRAVEL-NPCS-17: A loaded NPC standing on another city's tile always 'visits' that city with zero-length legs

- **Severity:** info · **Verdict:** confirmed · **Category:** validation · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/travel.cpp:221`

NpcsPlaced accepts any tile on the map for a homed NPC. If its tile is another city's tile, the cost there is 0, which counts as 1, so the pull is people × 1,000,000 and the city is nearly always chosen. RouteDown gives `{from}`, and Visit makes one-tile legs, so the NPC travels nowhere for the whole stay and its journal records the visit.

**Evidence:** host.cpp:460 NpcsPlaced checks the tile range and home kind only; travel.cpp:221 `std::max<u64>(cost, 1)`; paths.hpp RouteDown '{from} when from is the goal'.

**Impact:** Only crafted or hand-edited saves (generation places NPCs on their own region). Odd behaviour, no crash.

**Recommendation:** Either refuse homed NPCs on another city's tile at load, or give zero weight to a destination whose cost is 0.

**Verification:** host.cpp:460 NpcsPlaced checks only the tile range and home kind; travel.cpp:221 clamps cost to 1 so a cost-0 city gets the max pull. Only reachable via crafted saves; generation places NPCs on their own region. Odd but harmless.

### GAME-TRAVEL-NPCS-18: Members of a journey that ended mid-month stay Action::Travel until the next month step

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** design · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `src/game/travel.hpp:122`

Finish runs only at the month step, so an NPC home on day 3 stays a 'traveller' for up to 27 more days. It is excluded from socialising and from being a partner, and its journal entry is written late. This follows from the calendar-step design, but it means the effective travel time per NPC is much longer than the journeys themselves.

**Evidence:** host.cpp:1571-1572: Finish is called only in AdvanceCalendar; MonthStep skips Action::Travel.

**Impact:** Skews the social simulation: for the shipped stays of 2-5 days, the share of NPCs unavailable each month is roughly the whole travel share.

**Recommendation:** Either accept and document it, or let MonthStep treat an NPC whose journey EndTick ≤ monthStart as home (Finish already runs first, so this mainly matters if Finish's tick changes). Consider ending journeys at day boundaries if the world sim gets finer.

**Verification:** host.cpp:1572 runs Finish only in AdvanceCalendar, and npcs.cpp:343/361 exclude Action::Travel from socialising, so members stay travellers until the month step. This is inherent to the month-step design (src/AGENTS.md: 'Each month step ... ends the journeys over by the clock's tick, then ... runs the month's actions'), and the NPC's month action is Travel for that whole month by design, so the 'contradicts-recorded-decision' status is wrong and it is deliberate. The claim that unavailability is about the whole travel share is just the month granularity.

### GAME-TRAVEL-NPCS-19: DestinationWeight uses std::array::at, which can throw

- **Severity:** info · **Verdict:** confirmed · **Category:** rules · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/travel.cpp:231`

`city.draws.at(std::to_underlying(VisitorOf(rules, power)))` uses the throwing accessor in simulation code, while src/AGENTS.md says 'Our code throws nothing'. The index is always 0 or 1, so it never throws in practice.

**Evidence:** travel.cpp:231.

**Impact:** None at runtime; inconsistent with the stated error policy.

**Recommendation:** Use operator[] with a static_assert on VISITOR_KINDS, or PN_ASSERT the index.

**Verification:** travel.cpp:231 uses `.at(...)`, which can throw, while src/AGENTS.md says 'Our code throws nothing'. The index comes from VisitorOf, a 2-value enum, so it never throws in practice. This is only a style inconsistency.

### GAME-TRAVEL-NPCS-20: GenerateWorld's freshness assert checks only the world domain and the clock, not relationships, travel or the active domain

- **Severity:** info · **Verdict:** confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/world_state.cpp:53`

The precondition 'must be fresh' is asserted through the world-domain view, months and worldTicks. A WorldState with leftover journeys, relationships, journals or active entities passes, and Travel::Seed then merges new journeys into the old ones.

**Evidence:** world_state.cpp:52-55.

**Impact:** Misuse in future callers (for example regenerating into a reused WorldState) is not caught.

**Recommendation:** Extend the assert to `world.travel.Journeys().empty() && world.relationships.All().empty()` and an empty ACTIVE domain.

**Verification:** The assert at world_state.cpp:52-55 checks only the WORLD domain's StableId entities, months and worldTicks. It does not check travel, relationships or the active domain. The scenario needs a future caller to reuse a WorldState, and the current callers pass fresh ones, so the gap is hypothetical hardening.
