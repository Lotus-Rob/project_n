# Review: specs-built-b (part 2)

### SPECS-BUILT-B-11: FindRoute is dead in production code and survives for tests only

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** dead-code · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/paths.cpp:184`

The Outcome says `FindRoute` "has no caller in the game now" and is kept as the test reference. `src/core/paths.cpp:184` defines it and `paths.hpp:66` exports it.

**Impact:** Two route searches with different neighbour rules and costs sit side by side. A future change to one (corner rules, passability) will not be mirrored, and the reference will drift from the shipped search.

**Recommendation:** Move FindRoute into tests/support as a reference implementation, or delete it and port the test samples. Share the neighbour and corner rules in one helper.

**Verification:** FindRoute (paths.cpp:184, paths.hpp:66) is called only from tests/unit/core/paths_tests.cpp and tests/unit/game/npcs_tests.cpp, so it has no production caller. The paths_tests lines 149 and 155 compare FindShortestRoute against FindRoute, which is the deliberate reference role. The header comment says the new search uses 'FindRoute's neighbour and corner rules'. That shared-rules claim is why drift is partly guarded by tests, which weakens the finding. Moving it to tests/support is a reasonable cleanup.

### SPECS-BUILT-B-12: A river's head outside river 0's band (delta, river 10) is a recorded backlog item, and the new test does not cover it

- **Severity:** low · **Verdict:** deliberate · **Category:** correctness · **Status:** known-deferred · **Reviewer confidence:** low
- **Where:** `docs/superpowers/specs/2026-11-24-river-heads-design.md:218`

Qwen's finding that river 10's head lies about 0.2 tile outside river 0's band was rejected for this work and sent to the backlog. The spec's Goal 5 says a branch's head is hidden by its parent's band. The shipped test only classifies the head as a branch.

**Impact:** The one case Goal 5 relies on to hide the cut shows a visible flat cut of up to 0.2 tile. The test passes because it checks classification, not coverage.

**Recommendation:** Add a data test that every branch head lies within the parent band half-width, or move the point onto the parent's line. Say which in the spec.

**Verification:** The spec's rejected list (line 218-219) and sweep-up-backlog.md line 109 both record that river 10's head ends about 0.2 tile outside river 0's band, and that it is kept as it is for now. The backlog item says to draw a branch's head onto its meeting point. The spec's Goal 5 (line 61) still says the parent's band hides the cut, and the Outcome admits that doesn't hold for river 10. That is a small wording inconsistency, not an unrecorded gap.

### SPECS-BUILT-B-13: Walk-table cache is cleared at every month step, so each step triggers a rebuild burst under a lock

- **Severity:** low · **Verdict:** deliberate · **Category:** performance · **Status:** known-deferred · **Reviewer confidence:** low
- **Where:** `docs/superpowers/specs/2026-11-23-phase6-chunk5b-look-travellers-design.md:420`

The Outcome clears the shared tables at each month step and on load, because NPC legs start on each NPC's own tile and tables grew without bound (10,430 after 12 months). It notes "the first publish after the month's clear refills each journey's tables under the lock by design" and a logic-thread worst of about 31 ms in the first half second. A two-generation cache was "measured negligible and skipped".

**Impact:** Hitch on the logic thread at every month step, on top of the step itself. With more travellers on bigger maps, tables to rebuild grow linearly. The "off the frame's critical path" argument holds only while the logic thread is not the pacing one.

**Recommendation:** Carry the planned per-destination walk tables item forward and make it a measured spike with a decision rule, as the repo's unknowns rule requires. Record the month-step refill cost per traveller count.

**Verification:** The Outcome (lines 207-216, 240-245) records the month-step clear, the 31 ms logic-thread worst case, and that a two-generation cache and building off the publishing path were measured and skipped. Line 256 and sweep-up-backlog.md lines 107-108 carry forward per-destination immutable walk tables as the next design. That item already says to measure first and notes that a lazy table behind a lock brought the contention back. The finding's recommendation is therefore already met. The cited line 420 may be off, since the quoted text is around lines 207-245.

### SPECS-BUILT-B-14: Relationships::Trim and Merge copy the whole table serially every month step

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/game/npcs.cpp:173`

`Trim` walks all edges with one thread and copies each holder's edges into `m_merged`. With a lowered limit or a bigger world, every month copies the full table. The final review triaged this as "left as it is" at 6.0 ms a step.

**Impact:** The step is 6 ms at 50,000 NPCs. At 1M NPCs, a likely procedural-map size, the serial copy of up to 28 MB (28-byte edges times 10 each) adds a main-thread hitch each month, and the hitch sits inside the month step's logic stall.

**Recommendation:** Trim only holders touched this month, except when the limit changes. Or fork Trim in MonthStep::CHUNK chunks merged in order. Revisit when relationships get their real design.

**Verification:** Relationships::Trim is serial, walks all edges and writes into m_merged (npcs.cpp 173-180). The cost is O(edges) per month. The spec's 6 ms per step at 50,000 NPCs was triaged as left as it is. The 1M-NPC projection is speculative, and relationships are slated for a real design later. Low impact today.

### SPECS-BUILT-B-15: All 12 plans in the range have no ticked checkboxes though every one is implemented

- **Severity:** info · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/plans/2026-11-24-river-heads.md:1`

Counts of `- [ ]` against `- [x]` are 24, 33, 15, 13, 54, 8, 15, 52, 11, 48, 43 and 18 unticked with 0 ticked across the plans. This is expected under the ledger convention, but the plans carry no status line.

**Impact:** Anyone using the plans as history sees what looks like unexecuted work. The ledger that held the execution state is deleted after the branch finishes.

**Recommendation:** Add a one-line "Executed; see the spec's Outcome" header to each plan. Or finish the finishing step by marking every step complete.

**Verification:** river-heads plan has 0 ticked boxes (grep -c of '- [x]' = 0). Ledger convention explains it; a status line is optional polish.

### SPECS-BUILT-B-16: Specs cite gitignored evidence that cannot be rechecked

- **Severity:** info · **Verdict:** unverifiable · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-19-input-latency-design.md:31`

Evidence and Outcome point to `build/measure/20261117-003212/world.tracy`, `build/measure/20261120-145157/`, and `.superpowers/brainstorm/.../mockup-scripts/lod.py` (fog spec). All of these are gitignored and get cleaned (`clean --stale`).

**Impact:** The numbers can't be re-derived once the build trees are cleaned, and the mockup layout a spec says the cloud code started from is gone.

**Recommendation:** Copy the key numbers or the mockup script into docs/design, or state that the capture is not kept. Keep it as a single-line footnote per spec.

**Verification:** Gitignored build/measure and .superpowers paths cannot be rechecked by reading. Impact is real but small; Outcomes carry the numbers inline.

### SPECS-BUILT-B-17: Input-latency spec's Design 8 lives inside the approved spec, but the plan orders Task 7 before Task 6

- **Severity:** info · **Verdict:** unverifiable · **Category:** docs · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/plans/2026-11-19-input-latency.md:180`

The plan lists Task 5, then Task 7 (the handover queue, added later), then Task 6 (design doc, measurement and Outcome). The header status of the spec says Design 8 was approved, while its item 3 carries an inline "Amended" note.

**Impact:** Minor reading confusion. Inline amendments of an approved item are easy to miss when later reading which rule the code follows.

**Recommendation:** Renumber the plan tasks to execution order. Keep amendments in a dated Amended section.

**Verification:** Task ordering and amendment placement are style matters. I did not open the plan, so I cannot confirm the 7-before-6 order. Impact is nil.

### SPECS-BUILT-B-18: Travellers in a journey walk member 0's distance, so groups move in lockstep

- **Severity:** info · **Verdict:** unverifiable · **Category:** design · **Status:** new · **Reviewer confidence:** low
- **Where:** `docs/superpowers/specs/2026-11-23-phase6-chunk5b-look-travellers-design.md:81`

Goal 2 says members other than 0 walk member 0's distance and keep fixed lattice offsets. Distance and facing follow the tile path, not the drawn curve (Decision 7), so a slide is possible on a road.

**Impact:** Every figure in a group shows the identical frame at once. Feet can slide a few percent on road curves. The spec says it will be judged by eye, and the Outcome leaves "feet sliding and steps flickering at zoom 0.25" to the user's eye in motion.

**Recommendation:** Confirm by eye and record the result. Consider a per-member phase offset in the frame pick, since it is presentation only.

**Verification:** This is a visual judgement the spec leaves to the user's eye. Reading cannot decide it. Lockstep frames are a stated consequence of Goal 2.

### SPECS-BUILT-B-19: Timeline-harness constants and "fails on old code" controls are described but their regression value is not guarded in CI

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** test-design · **Status:** new · **Reviewer confidence:** low
- **Where:** `docs/superpowers/specs/2026-11-19-input-latency-design.md:255`

The Outcome records the old order failing at -1.545 ms at 60 Hz and 5 of 8 phases at 165 Hz. These controls live in the harness itself. The constants (window 600, p99, margin 1 ms, floor 2 ms, rise 10%, fall 2%) are "the spec's, untuned" and exercised on one 165 Hz machine, with no minimum-spec CPU recorded.

**Impact:** D could settle below what slower ticks need on the minimum-spec CPU. The 8-phase set at 60 Hz and 165 Hz, with steady lateness 1 to 2 ms, does not stress a 5 to 10 ms publish lateness.

**Recommendation:** Add a harness case with 5 to 8 ms steady lateness and bursty lateness, and check that D follows it with no late adoption. Record the first slow machine's D range when available.

**Verification:** The spec states that the minimum-spec CPU isn't recorded and that the constants are untuned (line 38-39, Outcome around line 231). The harness controls do exist in the tests, and `measure` is deliberately a report, not a gate (line 54). The suggestion to add a harness case with 5 to 8 ms steady and bursty lateness is a reasonable extra test, but it is not a defect. D is adaptive and tracks the p99 plus margin, so the claim that it could settle too low rests on the harness not covering larger lateness. I did not check which lateness values the harness actually exercises. It is an optional test gap, not a bug.
