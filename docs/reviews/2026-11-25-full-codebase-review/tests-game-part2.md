# Review: tests-game (part 2)

### TESTS-GAME-11: MeasureLongSave re-implements Travel::SaveDelta's classification and derives 'rest' by subtraction

- **Severity:** low · **Verdict:** unverifiable · **Category:** duplication · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/game/shipped_world_tests.cpp:628`

Lines 628-728 (about 100 lines) copy the kept, gone, planned and whole journey classification from Travel::SaveDelta. They compute `rest = total - domain - order - ...` by subtraction. The skipped measurement then reports these as if they were the save's real parts.

**Evidence:** `const std::size_t rest = save.total - save.domain - save.order - save.journeyGone - ...`

**Impact:** The numbers drift silently when SaveDelta or the save layout changes. The skipped case is never run by the gate, so nobody notices.

**Recommendation:** Have Travel/Host expose per-part sizes or a stats struct and use that. Otherwise remove the byte accounting.

**Verification:** I did not open lines 628-728. The finding concerns a skipped, measurement-only case, so it has no effect on the gate. Reading the code would decide whether it duplicates SaveDelta's classification.

### TESTS-GAME-12: Tautological determinism test for StartingPower

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** test-quality · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/game/npcs_tests.cpp:558`

'starting power is the same for the same identity and seed' calls a pure function twice with identical arguments and compares the results. The 'differing seed gives different power' half passes if any of 5 IDs differ. 'starting power lies in its bands' checks only `>= 0` and `< 500` over 20 IDs, never that the bands are hit or that their shares are right. The one golden test uses `Approx(...).epsilon(1e-3)` on floats copied from output.

**Evidence:** `CHECK(Same(game::StartingPower(SEED, core::StableId{ id }), game::StartingPower(SEED, core::StableId{ id })));`

**Impact:** The tests cannot fail on a real regression. A change that makes all powers 0 passes the 'bands' test.

**Recommendation:** Assert band membership for known IDs, or fix the distribution check to the 80/18/2 share on a larger fixed sample, as the shipped-world test does. Drop the same-input-twice case.

**Verification:** The 'same identity and seed' half is a pure function called twice with identical arguments (npcs_tests.cpp:558), which is near-tautological, though it does catch hidden global state. The 'differing > 0' check is weak. A separate golden test (line 572) pins mortal, cultivator and strong draws for named IDs, which does catch an all-zero regression. I did not verify the 'bands' test, so 'all powers 0 passes' may be overstated.

### TESTS-GAME-13: Weak assertions that only prove a load or a run didn't fail

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** test-quality · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/game/npcs_tests.cpp:159`

'a save after a fold loads' (line 159) checks only that Load returns true, with no state comparison after load. host_tests 'months pass for the stress arena's world NPCs' (line 569) runs 12 months over 50,000 NPCs and asserts only `NpcRelationships().All()` is non-empty and the active count is unchanged. 'a month step gives the same world with one worker and with many' (npcs_tests:407) compares only power, action, company and rest, not cell or home. The 'several chunks' assumption is not asserted against MonthStep::CHUNK.

**Evidence:** `CHECK(host.Load(reader, 0));` is the only assertion.

**Impact:** A fold that corrupts the loaded world, or a worker-dependent change to the cell, would not be caught.

**Recommendation:** Compare hashes or the full NPC records after load. Include cell and home in the worker comparison. Static_assert or REQUIRE that NPCS > 2 * MonthStep::CHUNK.

**Verification:** Both cited assertions match the code. 'a save after a fold loads' (npcs_tests.cpp:159) is a load-succeeds check built via test::ArenaSave. The 12-month stress test (host_tests.cpp:569) asserts only that the relationships are non-empty and the active count is unchanged. I did not check the worker-comparison test's fields or MonthStep::CHUNK. The 'weak' framing is fair, but these are smoke-style cases with other tests nearby.

### TESTS-GAME-14: Missing negative and edge cases for NPC activation, folding and relationship loading

- **Severity:** low · **Verdict:** unverifiable · **Category:** test-gap · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/game/npcs_tests.cpp:124`

There is no test of Activate on an already-active or unknown ID, Deactivate on a world NPC, Materialise on a group that does not exist or has 0 members, Fold with an NPC that is not in the group, Relationships::Load on truncated or hostile input, or Trim with `kept = 0`. Only the happy paths and the clock-limit refusal are tested.

**Evidence:** Test list: only activate/deactivate round-trips, one Materialise/Fold, Merge/Trim and Load of sound or late tables.

**Impact:** Defensive paths in src/game/npcs.cpp are untested, and the host's save loader trusts them.

**Recommendation:** Add small cases with literal inputs, and add a fuzz-ish truncated-prefix test for Relationships::Load.

**Verification:** I did not audit the full npcs_tests list or src/game/npcs.cpp for untested defensive paths. A missing-negative-case claim needs a grep for each API. It is plausible but unchecked.

### TESTS-GAME-15: Test name does not match its content and a duplicate case follows it

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** misleading-test · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/game/combat_tests.cpp:1646`

'the data's combat rules set the aura's period and the lance's burst' (line 1646) only tests auraPeriod and the aura damage min/max. Its second half, a lance burst, is nowhere. The following test, 'set the lance's burst and its poison's period' (line 1662), ends with `CHECK(ticked >= 1)`, which is almost vacuous. It never checks poison ticks happen only every PERIOD or that the damage is as expected.

**Evidence:** `CHECK(ticked >= 1);`

**Impact:** The name over-claims coverage. The weak final check passes if poison ticks on every tick.

**Recommendation:** Rename the first test. In the second, assert the first poison tick is exactly at CAST + PERIOD and that no earlier tick has one.

**Verification:** The second test (combat_tests.cpp:1662) ends with 'CHECK(ticked >= 1)', counting Hit events at tick == CAST+PERIOD for the use. It does assert a tick at the period but does not assert that none came earlier, so the weak-assertion part holds. The claim that the lance burst is 'nowhere' is wrong: the second test sets flashTicks and checks flashes at CAST+FLASH-1 and CAST+FLASH. Only the first test's name over-claims, and the second test is misnamed less than the finding says.

### TESTS-GAME-16: Shipped data values hard-coded across tests with only partial linkage

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** maintainability · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/game/combat_tests.cpp:17`

Fixed copies of assets/data and manifest values:
- QI_START, QI_MAX, BOLT_GAIN, LANCE_COST, SWORDS_COST, DASH_*, BURN_*, SWORD_DAMAGE and SWORDS = 1,000
- host_tests AURA_DAMAGE_MIN/MAX, CULTIVATOR_FOOT_Y = 122 and the TOTALS array 1,008/1,000/996/996
- scenes_tests `entities.front() > 1'000'000`
- `RULES{}` standing in for the shipped combat rules, with the comment 'which a data test checks'.

The host test 'shipped creatures and abilities tables are the built-in data' ties only the hash.

**Evidence:** `constexpr core::CombatRules RULES{};` and `constexpr i64 FOX_HEALTH = 1'200;` (also in support/host.hpp:29).

**Impact:** A rebalance fails dozens of tests whose purpose is unrelated, and a change to defaults silently re-bases the tests.

**Recommendation:** Derive these values from `game::BuiltInData()` where the number isn't the point, and keep literals only where the arithmetic is what is being checked.

**Verification:** Checked combat_tests.cpp:17-25. RULES{} and FOX_HEALTH/CULTIVATOR_HEALTH are literal copies, and the comment admits a data test is the link. The coupling is real. But for arithmetic-oriented combat tests, literals are what the check needs. The recommendation to derive from BuiltInData is itself partly in tension with that. Low maintainability nit.

### TESTS-GAME-17: Volley tests run 1,000-sword volleys with per-tick publish

- **Severity:** low · **Verdict:** unverifiable · **Category:** performance · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/unit/game/combat_tests.cpp:903`

'each tick's hits are the swords arriving then…' (line 903) publishes a snapshot every tick across a whole 1,000-sword volley, and several cases play full volleys. This breaches the AGENTS.md 'worst value asserted once, not a sweep over every fine time step' rule in spirit. The assertions are folded into counters, but the work remains.

**Evidence:** `for (u64 tick = CAST + 1; tick <= last; ++tick) { ... Play(host, tick, tick) ... before = Published(host, tick); }`

**Impact:** Slow debug runs for a file that is already near the longest.

**Recommendation:** Use a smaller volley count (the shape is data), or sample the death-tick window plus its two neighbours.

**Verification:** Whether the volley tests are slow depends on measured runtime, which I cannot get read-only. The per-tick publish is plausible given the cited loop, but without timings the cost claim is a guess. Timing the suite would decide it.

### TESTS-GAME-18: Slow stress-arena cases use up to 100,000 instances and 12 months of 50,000 NPCs

- **Severity:** low · **Verdict:** unverifiable · **Category:** performance · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/unit/game/host_tests.cpp:456`

'the stress arena publishes 100,000 instances' (456), 'the stress arena's 10,000 bullets' (462), '…same bytes with one worker and with many' (524, with 7 workers) and the stress month case (569) each build a stress host. save_stress_tests builds two more, in a file split out for exactly this reason.

**Evidence:** Four separate `game::Host host(test::TestManifest(), game::STRESS_SCENE)` constructions in one file.

**Impact:** Cost in debug and sanitizer builds for coverage that a smaller fixture could give.

**Recommendation:** Share one stress host across the publish assertions, or make a scaled stress scene for tests.

**Verification:** Multiple stress-host constructions are plausible. Whether the cost matters needs a timing, and the stress arena's size may be the point of the tests (100,000 instances). Sharing a host between tests could introduce order dependence. A per-test timing would decide it.

### TESTS-GAME-19: Determinism tests only compare two runs in the same binary, with no change detector

- **Severity:** low · **Verdict:** deliberate · **Category:** test-gap · **Status:** contradicts-recorded-decision · **Reviewer confidence:** low
- **Where:** `tests/unit/game/save_exactness_tests.cpp:49`

Every determinism, save and replay test compares runs within one process. Nothing pins the simulation's output (a golden StateHash for seed S after N ticks and months, per preset). The only goldens are the three StartingPower values. An unintended change to the sim (RNG draw order, a changed constant) passes all of these tests as long as it is self-consistent.

**Evidence:** No literal hash comparison anywhere in tests/unit/game.

**Impact:** A behaviour change that should be a conscious version bump (GENERATOR_VERSION and the base hash) can land unnoticed.

**Recommendation:** Optionally add one golden hash per toolchain family. This is consistent with the decision that replays are deterministic only within one binary, if kept per-preset.

**Verification:** Determinism is documented as same-binary only (docs/design/0002-engine-architecture.md §7.4 and rule 5, docs/design/0002.2-replays.md:16: 'Same binary only; across builds nothing is promised'). A golden hash would pin cross-build behaviour, which is explicitly not promised. The reviewer's per-preset variant is optional. The 'contradicts-recorded-decision' status is accurate for the cross-build form.

### TESTS-GAME-20: OffCurve divides by zero on a degenerate segment and returns f32 max on an empty curve

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** test-quality · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/support/road_curves.hpp:18`

A zero-length segment gives `lengthSquared == 0`, so `share` is NaN. `std::clamp(NaN,...)` is undefined behaviour or returns NaN, and `std::min` then ignores it. An empty or one-point curve returns `numeric_limits<f32>::max()`. A test asserting `OffCurve(...) > 0.25 * TILE` passes vacuously on an empty curve.

**Evidence:** `const f32 share = std::clamp(((apart.x * along.x) + (apart.y * along.y)) / lengthSquared, 0.0F, 1.0F);`

**Impact:** A broken RoadCurves result can make `REQUIRE(off > …)` setup checks pass for the wrong reason (host_tests 2342, 2503).

**Recommendation:** Skip zero-length segments, and REQUIRE a minimum of two points in the helper or at the call sites.

**Verification:** road_curves.hpp:18-27 matches the description. A zero-length segment divides by zero, which gives NaN or inf. An empty curve returns f32 max, so a '> threshold' REQUIRE would pass vacuously. The helper is only a test utility, though. Real RoadCurves have distinct points, and the call sites likely assert on the curve elsewhere. The actual risk is small.
