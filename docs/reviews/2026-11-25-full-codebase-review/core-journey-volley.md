# Review: core-journey-volley

**Scope:** src/core/journey.{hpp,cpp}, volley.{hpp,cpp}. Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 17 standing (0 critical, 0 high, 0 medium, 11 low, 6 info), 0 raised but refuted on verification.

**Parts:** core-journey-volley.md, core-journey-volley-part2.md

## Assessment

## core-journey-volley: assessment

**Scope:** src/core/journey.{hpp,cpp}, src/core/volley.{hpp,cpp}, tests/unit/core/journey_tests.cpp and tests/unit/core/volley_tests.cpp. To judge callers and claims I also read parts of game/travel.cpp (load validation), game/combat.cpp (cast, load, Arrive), gfx/volley_draw.cpp, core/road_curve.cpp (JourneyShift), core/leg_walk_cache.hpp, core/random.hpp, core/tick_clock.hpp, core/world_map.cpp (cost validation, StepOver), core/paths.hpp, data/world_map.cpp (stay_days) and host.cpp (kept-walk lifetimes).

### Strengths
- Both modules are pure functions of plain records. The simulation and the renderer share them, and the code is written with determinism in mind: f64 internally, `core::math` for trig, keyed `core::Random`, no state hidden between calls.
- The edge cases are carefully handled. Each float-to-int conversion is checked in floating point first (`MakePacedLeg`, `CommitTicksFor`). Divisions are guarded by how the branches are ordered (`flown < flightTicks`, `elapsed < outTicks`, `ShareGone`). The `KeptWalk` incremental sum reproduces the fresh sum bit for bit because it adds in the same order from 0.0. `WalkTargets` terminates for a stated reason (each move is to a target that outlives the last).
- Loading is well defended one layer up. `travel.cpp`'s `LegSound`, `StaySound` and `plannable`, and `combat.cpp`'s volley load, check everything these functions only `PN_ASSERT`. Zero step costs are refused when the map is built, so arrive ticks always rise strictly.
- The volley tests are unusually thorough: smoothness while sampling between ticks, retarget swings, the edges of the commit window, bitwise sameness, statistics on spread. The journey tests cover the kept walk against the fresh one in both orders and rebuild-equals-visit across 40 samples.

### Weaknesses
- **Wave split rounding (confirmed numerically).** `count * (radii(w) / total)` truncates below exact integers. Equal rings can get uneven sizes (90 swords over 10 equal rings comes out 9…8,10…), and with no ring a wave can be empty. This contradicts the header's formula and its promise of an even split. The shipped 1000/10 shape happens to escape it, and the tests only try counts 1000 and 1003.
- **Cost not bounded for loaded or large inputs.** `StayWalked`/`StayAhead` walk every day up to the tick on every call. A loaded stay may legally last about 23.8M days (StaySound allows up to u32::max ticks), while the data caps stays at 255 days. `WalkTargets` scans every target at each link, so it is O(T) per link and O(T²) per chain. A crafted save can hang the arrival tick or the draw.
- **Repeated work every frame.** The renderer asks for the same sword's plan (wave search, 4 to 5 random draws, cos and sin) about 12 times per frame. Arrival and target chains are also worked out several times per sword. The API offers no way to compute a sword's plan once and reuse it.
- **Layering claims are not true.** journey.hpp says legs are reached only through `Where()`/`EndTick()`. In fact road_curve.cpp copies `Where()`'s part search and step interpolation, and host.cpp, travel.cpp and leg_walk_cache.cpp read `tiles`/`arrive` directly. `JourneyPart` holds both a leg and a stay behind a bool, so stay parts carry an empty Leg that breaks the leg's "never empty" invariant, and saves and hashes carry the unused fields.
- **Doc drift.** `VolleyShape::Valid` says `Volley::Valid` checks `flightTicks`; it does not. `Journey::parts` is documented "never empty" while every function handles empty. The `LegWalks::Of` lifetime contract is weaker than what the `KeptWalk` overload needs.

### Overall health
Good. The code is correct in normal play: I found no crash, UB or desync on the paths the shipped data exercises. The real defects are one rounding bug in an edge of the data space, cost that crafted saves can blow up (saves are a fuzzed untrusted input), and repeated per-frame work in the renderer's volley path. The rest is about honest architecture docs and dead fields in the journey record.

### Risks
- Maps and crowds are expected to grow (procedural maps of up to 2^24 tiles, the stress arena). The O(T) scans per link in `WalkTargets` and the repeated per-sword recomputation will be among the first hot spots once volleys tag hundreds of targets.
- The shared-routes branch, which changes journey storage, will touch the leg-reading code that has been copied into road_curve and host. The incomplete "only through Where()" boundary makes that change riskier than the header suggests.
- Stays and arrival chains bounded only by u32 limits give the save fuzzer timeouts to find.

**Files read in full:** src/core/journey.hpp, src/core/journey.cpp, src/core/volley.hpp, src/core/volley.cpp, tests/unit/core/journey_tests.cpp, tests/unit/core/volley_tests.cpp

## Findings

### CORE-JOURNEY-VOLLEY-01: Wave boundaries computed as count*(radii/total) truncate below exact integers: uneven or empty waves

- **Severity:** low · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/volley.cpp:158`

WaveStart returns static_cast<u32>(count * (radii(wave) / total)). The division rounds first, so whenever the exact value count*radii/total is an integer, the f64 product can land just below it and truncation drops a sword. Header WaveOf() (volley.hpp:271-276) documents ⌊count × (r0+…+r(w−1)) / (r0+…)⌋ and says that with no ring radius 'every wave takes an even share'.

**Evidence:** Reproduced the formula in Python: no ring (radius 0, step 0), count=22, waves=22 gives sizes [...,1,0,2,1,...] (wave 14 empty, wave 15 has 2); count=23/waves=23, 69/23 and 51 such cases with waves<60 likewise. Ten equal rings (ringRadius 100, step 0) with 90 swords: wave 7 starts at 62 instead of 63, so the rings hold 9,…,8,10,… Shipped-like 160/38 with count 331: boundary 5 is 117 instead of 118.

**Impact:** A tuned shape (abilities.yaml) with equal rings or no ring gets visibly uneven rings or an empty ring, against the documented contract. The shipped 1000/10/160/38 shape is unaffected. A wave of 0 swords is handled by the binary search, so there is no crash.

**Recommendation:** Multiply before dividing: floor((count * radii(w)) / total). That is exact for integer-valued radii, or use integer or fixed-point arithmetic for the radii sums. Add test samples with count == waves and with equal rings (step 0, e.g. 90/10), checking max-min <= 1.

**Verification:** Reproduced the formula in Python. count=22 and waves=22 with no ring gives boundary 15 as 14, so one wave is empty and the next has 2. Multiplying before dividing gives exact values. The shipped shape is unaffected. The doc comment at volley.cpp:144-148 claims each boundary is 'correctly rounded' and the header promises an even share, so the code contradicts both. The tests would not catch it unless they sample count==waves.

### CORE-JOURNEY-VOLLEY-02: StayWalked/StayAhead walk every day of a stay on every call; a loaded stay may last ~23.8M days

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/journey.cpp:166`

StayWalked loops from day 0 to the day holding the tick on every call, drawing two core::Random numbers and a sqrt per day. KeptWalk caches only finished parts, so the stay under way is re-walked every frame for every traveller drawn walking. StayAhead does the same from firstDay. travel.cpp's StaySound accepts any stay up to u32::max ticks (≈23.8M days at TICKS_PER_DAY=180), while the data caps stay_days at 255 (data/world_map.cpp ReadStayDays).

**Evidence:** journey.cpp:166 `for (i64 day = 0; stay.startTick + (day * DAY_TICKS) < stay.endTick; ++day)`; travel.cpp StaySound: `return span <= std::numeric_limits<u32>::max();`

**Impact:** A crafted or corrupted save with one long stay (a whole journey saved verbatim) makes every publish of that traveller loop millions of times, freezing the world map. In normal play the cost is about 10 draws per traveller per frame, which is acceptable but grows linearly with stay length.

**Recommendation:** Cap loaded stays in StaySound at what the rules can produce (u8 max days, or the rules' stayMaxDays). Better, make StayWalked O(1) for the distance: keep a per-stay cumulative table, or carry day sums in KeptWalk the way finished parts are carried, and search for the facing only within the current day.

**Verification:** The loop at journey.cpp:166 is as described. StaySound in src/game/travel.cpp:59-67 only bounds the span to u32 max, so a crafted save could hold a very long stay. The call-count claim is unverified. Normal play is capped by the 255-day data limit, so the cost is a bounded ~10 draws per call. This is a load-hardening gap, not a normal-play problem. Medium is too high.

### CORE-JOURNEY-VOLLEY-03: WalkTargets rescans every target at each link: O(T) per move, O(T²) per chain, called several times per sword per frame

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/volley.cpp:246`

For each move WalkTargets runs count_if over all targets, then a second linear scan to find the skip-th living target. A chain can be up to `count` links long. The renderer calls it twice per sword per frame (SwordAim, plus SwordTarget once arrived), and the logic once per arriving sword. Tag() in combat.cpp is uncapped: every enemy within the radius is a target.

**Evidence:** volley.cpp:246 `std::ranges::count_if(targets, ...)` and the following `for (; next < count; ++next)` loop inside `for (u32 move = 0; move < count; ++move)`. The loaded target count is bounded only by the save's size (combat.cpp:822).

**Impact:** In a dense fight (stress arena, hundreds of tagged foxes dying over the flight), each dead-target sword costs O(links × T) per call: millions of operations per frame for 2,000 swords. A crafted save with T targets whose diedTicks rise one after another, plus a long flightTicks, makes each SwordTarget O(T²), so the load or the arrival tick hangs (a fuzzer timeout).

**Recommendation:** Precompute once per volley per snapshot (or per tick on the logic side) the death ticks sorted with their indices, so 'living at tick d' is a count from a binary search and the skip-th living target is found through a rank structure, or at least cache the living list per distinct death tick. Bound the tagged-target count at cast and on load.

**Verification:** Code at volley.cpp:246-262 is as cited: a count_if plus a scan per link, with up to count links. The cost is O(T) per link and the chain only continues while targets are dead and past the commit window. Typical chains are short. A crafted save is the only realistic worst case. Real, but low priority.

### CORE-JOURNEY-VOLLEY-04: Each sword's plan (wave search, random draws, cos/sin) is recomputed ~12 times per frame by the renderer

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/volley.cpp:376`

SwordAt recomputes Plan() (PlaceInWave binary search with WaveStart calls, 4 Random draws, math::Cos and Sin) on every call. gfx/volley_draw.cpp DrawSword calls LaunchTick, ArrivalTick, SwordAim (ArrivalTick again plus WalkTargets plus AimOffset's 2 draws), SwordTarget (ArrivalTick again), DepartureTick, SwordAt for the head, and SwordAt TRAIL_SEGMENTS (8) more times for the trail. Each call places the sword in its wave again and redraws the departure delay.

**Evidence:** volley_draw.cpp:55 `core::SwordAt(volley, sword, headTick - ..., aim)` in a loop of TRAIL_POINTS; volley_draw.cpp:92-126 calls LaunchTick/ArrivalTick/SwordAim/SwordTarget/DepartureTick separately, each re-running PlaceInWave.

**Impact:** With MAX_VOLLEY_COUNT (2,000) swords, about 24k plan computations and about 8k arrival-chain walks per frame, all of them for values that are constant for the sword's life. This is wasted work in a hot path, against the 'avoid wasted work' rule.

**Recommendation:** Expose a per-sword path object, e.g. `SwordPath PathOf(const Volley&, u32 sword)` holding launch, departure, arrival, outwards, radius, bend and aim offset, with SwordAt/SwordAim overloads that take it. It stays a pure function, but is computed once per sword per frame or once per volley. The logic's Prepare() could share it.

**Verification:** Plan() recomputes PlaceInWave and the draws on every call, and the renderer calls it repeatedly per sword. This is true redundancy and matches the avoid-wasted-work rule. I did not measure the cost, so low severity is right.

### CORE-JOURNEY-VOLLEY-05: journey.hpp claims legs are reached only through Where()/EndTick(); road_curve.cpp duplicates Where's step logic and host/travel read tiles/arrive directly

- **Severity:** low · **Verdict:** confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/journey.hpp:7`

The file comment says 'A leg is reached only through Where() and EndTick(), so a new leg shape is one more case there and nothing that uses legs changes.' In fact core/road_curve.cpp JourneyShift copies Where()'s part search (find_if EndTick > tick) and its step interpolation (upper_bound on arrive, fraction). host.cpp:1389-1394 and 1527-1533 walk part.leg.tiles/arrive. travel.cpp LegSound, leg_walk_cache.cpp and journey.cpp's own MakeLegWalk/BegunStretches/LegWalked/PlanOf all assume TileRoute.

**Evidence:** road_curve.cpp:338 `std::ranges::find_if(journey.parts, [tick](const JourneyPart& part) { return EndTick(part) > tick; })` and :364-367, a verbatim copy of journey.cpp:376-379, with the comment 'As Where() finds the step'.

**Impact:** A second LegShape (or the in-flight shared-routes storage change) needs coordinated edits in at least five files, and the copied step search can drift from Where(), so the road shift and the position would disagree at boundaries. The doc misleads whoever adds the next shape.

**Recommendation:** Export the shared pieces from journey.hpp, e.g. `HoldingPart(journey, tick)` and `struct LegStep { size_t from, next; f64 fraction; }; LegStep StepAt(const Leg&, i64)`, and use them in Where(), JourneyShift and the host. Then correct the file comment to say what really depends on TileRoute.

**Verification:** road_curve.cpp:336-338 duplicates Where()'s find_if over parts, and its own comment says 'as Where() picks it'. The header comment that legs are reached only through Where() and EndTick() is therefore overstated. It is a maintenance and doc issue, with no live bug.

### CORE-JOURNEY-VOLLEY-06: JourneyPart holds both a Leg and a Stay behind a bool: stay parts break Leg's 'tiles never empty', and saves and hashes carry the unused fields

- **Severity:** low · **Verdict:** confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/journey.hpp:72`

JourneyPart is {bool stay; Leg leg; Stay rest;} and Fields() archives all three. A stay part carries a default Leg with empty tiles/arrive, which contradicts Leg::tiles 'never empty' (line 41). A leg part carries a Stay. Every reader must check `stay` first; nothing enforces it.

**Evidence:** journey.hpp:83 `archive(self.stay, self.leg, self.rest);` and Visit's `{ .stay = true, .leg = {}, .rest = stay }`.

**Impact:** Every whole journey saved or hashed carries about 30 to 40 bytes of unused fields per part. A loaded stay part's leg (and a leg part's rest) is never validated but is kept and hashed. Code that forgets to check `stay` reads an empty leg; for example MakeLegWalk/EndTick on a stay part's leg returns a meaningless startTick.

**Recommendation:** Use std::variant<Leg, Stay> (archive the index plus the active alternative), or write only the active member in Fields(). Keep the leg invariant true for every Leg that exists.

**Verification:** JourneyPart carries bool stay, Leg and Stay together, and Fields() archives all three. The unused-member waste and the empty-Leg-versus-'never empty' contradiction both follow from the struct layout. It is a design smell with no concrete bug. Low.

### CORE-JOURNEY-VOLLEY-07: VolleyShape::Valid says Volley::Valid checks flightTicks; it checks only commitTicks; startTick is unbounded on load

- **Severity:** low · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/volley.hpp:166`

VolleyShape::Valid's doc: 'The times the cast resolves, flightTicks and commitTicks, are checked on the cast (Volley::Valid()).' Volley::Valid checks shape.Valid(), commitTicks >= MIN_COMMIT_TICKS and finite points only. Neither checks startTick, so a loaded volley with startTick near u64 max makes Launch/Departure/ArrivalTick wrap (unsigned) to an arrival before its start. The comment also has a stray line break ('The\n/// times').

**Evidence:** volley.hpp:165-166 vs :223-225; volley.cpp:188-195 add startTick + wave*gap + stagger + out + hold + jitter + flight unchecked.

**Impact:** There is no UB (unsigned wrap), but a corrupted save can load a volley whose swords 'arrive' at tiny ticks: immediate or never-ending hits, and a flight that never ends. The doc misleads anyone relying on Valid() for load safety.

**Recommendation:** Either make Volley::Valid also require startTick + the latest possible arrival (sum of the time fields' maxima) to fit in u64, or fix the doc to say flightTicks is any u32. Fix the comment's line break.

**Verification:** volley.hpp:166 says Volley::Valid checks flightTicks, but Valid at :223-224 checks only shape.Valid(), commitTicks and finiteness. startTick is not bounded. The doc is wrong. The u64 wrap needs a corrupted save and has no UB, so low.

### CORE-JOURNEY-VOLLEY-08: LegWalks::Of promises a table only until the next call, but the KeptWalk overload stores the returned pointers indefinitely

- **Severity:** low · **Verdict:** confirmed · **Category:** api-design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/journey.hpp:220`

LegWalks::Of's contract is 'valid at least until the next call' (FreshLegWalks keeps only the last). WalkedAt(map, journey, tick, KeptWalk&, LegWalks&) stores &walks.Of(leg) in kept.tables and reads it on later calls. Only that overload's prose requires a longer lifetime; the type allows passing an implementation that honours the base contract and dangles.

**Evidence:** journey.cpp:450-453 `table = &walks.Of(leg);` kept across calls; FreshLegWalks::Of overwrites m_made on every call (journey.cpp:273-276).

**Impact:** A future caller passing a fresh-tables LegWalks to the kept overload reads freed or overwritten tables: wrong walks or UB. Today only LegWalkCache (node-stable unique_ptrs) is passed, and the host clears the cache and drops the kept walks together.

**Recommendation:** Split the interface: a `StableLegWalks` (or a parameter of type LegWalkCache&) for the kept overload, so the type carries the lifetime requirement.

**Verification:** journey.hpp:220 documents Of() as valid only until the next call. FreshLegWalks::Of overwrites m_made on every call (journey.cpp:273-276). The KeptWalk overload stores &walks.Of(leg) in kept.tables (journey.cpp:450-453), and that is only safe with a stable implementation. The type does not enforce this. Only LegWalkCache is passed today, so it is latent.

### CORE-JOURNEY-VOLLEY-09: MakeTileLeg guards its u32 arrive ticks with PN_ASSERT only

- **Severity:** low · **Verdict:** confirmed · **Category:** robustness · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/core/journey.cpp:302`

MakeTileLeg sums u64 ticks and only asserts they fit u32 before narrowing. Release builds narrow silently.

**Evidence:** journey.cpp:302 `PN_ASSERT(ticks <= std::numeric_limits<u32>::max(), ...)`; :303 `static_cast<u32>(ticks)`.

**Impact:** A very long route at a slow pace (large procedural maps) would wrap the arrive ticks in release: non-increasing arrive ticks, so the walker jumps.

**Recommendation:** Return std::optional<Leg>, as MakePacedLeg does, or bound the route length times MAX_WALK_COST × ticksPerCost when the map is built.

**Verification:** journey.cpp:302 is only a PN_ASSERT, followed by static_cast<u32> at :303, so release builds narrow silently. MakePacedLeg already returns optional, so the suggested fix has a precedent. Overflow needs an extremely long route at a slow pace. The route-sharing item in sweep-up-backlog.md (line 108) covers journey storage but does not mention this overflow, so I would not call it a recorded deferral.

### CORE-JOURNEY-VOLLEY-10: Each journey copies its route's tiles twice, and every LegWalk copies them a third time

- **Severity:** low · **Verdict:** deliberate · **Category:** scaling · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/core/journey.cpp:501`

Visit copies `out` into the outward leg and again, reversed, into `back`. MakeLegWalk copies leg.tiles into LegWalk::tiles to support Fits(). Each leg also stores a u32 arrive tick per tile.

**Evidence:** journey.cpp:501 `const std::vector<u32> back{ out.rbegin(), out.rend() };`; :403 `.tiles = leg.tiles`.

**Impact:** Memory and copy time grow with journeys × route length, a concern on large procedural maps.

**Recommendation:** Fold into the shared-routes work: reference shared route storage, and let LegWalk key on a route id instead of a copied tile vector.

**Verification:** The copies of the route tiles are real (journey.cpp:403 and :501). sweep-up-backlog.md:108 records this as an agreed future step ('Journeys share their routes in the simulation'), which will make legs refer to one shared route. The finding's own recommendation says the same.
