# Review: specs-built-b

**Scope:** docs/superpowers/specs and plans dated 2026-11-18 to 2026-11-24 (reference-hang, input-latency, long-run-determinism, relationships-bounded, chunk5b3 fog, test-suite-names, text-layout-budget, 5b-look-terrain, player-walk-and-camera, roads, 5b-look-travellers, river-heads): same questions as above: Outcome vs code on main, silent drops, recorded measurements, plan/spec consistency, leftovers.

**Findings:** 19 standing (0 critical, 0 high, 0 medium, 13 low, 6 info), 0 raised but refuted on verification.

**Parts:** specs-built-b.md, specs-built-b-part2.md

## Assessment

## Assessment: specs/plans 2026-11-18 to 2026-11-24 (12 specs, 12 plans)

I read all 12 specs in full. I checked their Outcome claims against code with grep and targeted reads. For the plans I read headers and task lists, and counted checkboxes. I did not read every plan body line by line.

**Strengths**
- The Outcome sections are unusually honest and measurement-backed. They give before and after tables, rulings with "cost if wrong", rejected findings with reasons, and what was not measured, in plain words.
- Every Outcome claim I spot-checked matches main:
  - input-latency: `BUFFERS = 5`, the 34-tick stale margin, and the delay constants.
  - relationships-bounded: `RelationshipLimits`, `Trim`, `IsMet`, and the `lastMet <= months` load check.
  - reference-hang: `REFERENCE_CASE_TIMEOUT = 80`, `reference_share`, and the per-case runtime dir.
  - text-layout: `LAYOUT_BUDGET_NS` and `LAYOUT_SECONDS`.
  - player-walk: `MIN_WALK_SPEED` and `FOLLOW_BOX_SHARE`.
  - roads and river-heads: the 2,400 to 3,300 test, the shimmer bounds in `reference_tests.cpp`, `RiverStarts` and its tests, and `taper` in `world_look.yaml`.
- The sim-side changes are well bounded and deterministic.
  - `Trim` uses a total order (lastMet, opinion, subject), so `nth_element` instability cannot matter.
  - `Relationships::Load` refuses a bad `lastMet` but never refuses a holder over the limit.
  - The long-run determinism cases cover both month steps and per-tick systems on 1 and 4 workers.
- The art generator is plain IEEE arithmetic plus integer hashing. I found no `sin`, `cos` or `exp` libm calls except one in `placeholders.py`, so `world.yaml`'s stale-roads self-test should be stable across platforms.

**Weaknesses and risks**
1. Several recorded performance results end in a measured gap that nobody closed or tracked.
   - The fog's main-thread work median rose 0.4 to 1.1 ms, and the Outcome says "the rest of the rise is unexplained".
   - The sight-fade cost in normal play was never measured, because `measure` pins the debug sight on.
   - The ground shader costs 0.59 ms of GPU time at 2560x1440 on the dev GPU. It was never measured on the minimum-spec GPU.
   - `measure` counts `SubmitFrame`, which waits on the GPU, as main-thread work.
2. Several designs are O(map) on the main thread, or serial per month step. This conflicts with "maps are procedural and may be very large".
   - `FogFade::SetTargets`, `FindMovers` and `Snap` are O(tiles) and allocate on every explored-set change.
   - The fog texture is uploaded whole.
   - `Relationships::Merge` and `Trim` copy the whole table serially every month.
   - The walk-table cache is cleared at each month step, so every month step triggers a re-fill burst.
3. Tuned content constraints are brittle.
   - A hand edit to rivers moved the traveller count enough that the traffic floor test had to be relaxed from 2,500 to 2,400.
   - The 2,000 floor in the busiest view and the pinned costs 126, 162, 173, 152 and 562 are snapshots of one map.
   - Per the project's own rule they are updated with the map. But a "balance target" that fails when a river is moved is really a regression test of the map's content.
4. The spec set has doc hygiene gaps.
   - All 12 plans have 0 ticked checkboxes although all are implemented. The ledger is gitignored, so the plans read as unexecuted.
   - Specs cite evidence in gitignored `build/measure/...` captures and in `.superpowers/` and mockup paths. These are unrecoverable.
   - Approved sections were left inconsistent with their Outcomes. Examples are the roads spec's 2,500 to 3,300 and the river-heads spec's "stop and ask if 2,500 breaks". The Outcomes record the change, but the approved text was not annotated.
5. Behaviour left open or inconsistent, all recorded as decisions or deferrals.
   - Held movement ignores terrain and collision, and walks over deep water and peaks. Click routes refuse those tiles.
   - `FindRoute` has no production caller.
   - The relationship graph is cell-local, with 30% of NPCs holding fewer than 10 relationships forever. This is read from the code and not measured.

**Overall health:** good. Process discipline is high: test-first fixes, measured claims, and reviewed rulings. I found no critical or high defects in the specs or plans. The main risks are unclosed measurement gaps (fog cost, GPU cost on minimum spec), O(map) presentation and serial month-step work at larger map sizes, and brittle shipped-data pins.

**Files read in full:** docs/superpowers/specs/2026-11-18-reference-hang-design.md, docs/superpowers/specs/2026-11-19-input-latency-design.md, docs/superpowers/specs/2026-11-19-long-run-determinism-design.md, docs/superpowers/specs/2026-11-19-relationships-bounded-design.md, docs/superpowers/specs/2026-11-20-phase6-chunk5b3-fog-design.md, docs/superpowers/specs/2026-11-20-test-suite-names-design.md, docs/superpowers/specs/2026-11-20-text-layout-budget-design.md, docs/superpowers/specs/2026-11-21-phase6-chunk5b-look-terrain-design.md, docs/superpowers/specs/2026-11-22-player-walk-and-camera-design.md, docs/superpowers/specs/2026-11-22-roads-design.md, docs/superpowers/specs/2026-11-23-phase6-chunk5b-look-travellers-design.md, docs/superpowers/specs/2026-11-24-river-heads-design.md, docs/superpowers/plans/2026-11-19-input-latency.md (headers, task list), src/app/fog_fade.cpp, src/game/npcs.cpp (Trim, Load), tools/project_n.py (reference runner), tests/unit/game/shipped_world_tests.cpp (traffic tests)

## Findings

### SPECS-BUILT-B-01: Fog's main-thread work rise is recorded as unexplained and never resolved

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-20-phase6-chunk5b3-fog-design.md:295`

The Outcome table shows the main-thread work median 0.4 to 1.1 ms higher than main in every segment and the p99 up to 1.3 ms higher. The text then says "the rest of the rise is unexplained", and attributes only about 0.125 ms to DrawClouds. It also says "normal play's sight-fade cost is unmeasured", because measure forces the debug sight on, which pins every in-sight value to 1.

**Impact:** The design's Goal 11 and anti-goal "Cost: a frame-time change measure can see" is declared met only because paced frames hide the work behind the GPU wait. Unmeasured: fade cost in real play, and any hitch when many tiles ease at once. A later chunk may pile onto an unaccounted baseline.

**Recommendation:** Profile main against the branch in normal play without the debug sight. Attribute the roughly 0.5 ms, or record it as GPU submit time like the terrain Outcome did. Add the normal-sight case to measure, or note that measure's debug sight hides fog easing cost.

**Verification:** Spec lines 295-299 literally say 'the rest of the rise is unexplained' and 'normal play's sight-fade cost is unmeasured' (debug sight pins values to 1). FogFade::Update itself is under 0.001 ms median, worst 0.205 ms, and later per-frame trims followed. The text is honest about it, so this is a recorded gap, not a hidden one. Severity is low, not medium.

### SPECS-BUILT-B-02: Fog easing and targets are O(total tiles) on the main thread, and the texture is uploaded whole

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/fog_fade.cpp:19`

Every change of the explored set or sight tile runs `SetTargets` (assign tiles*2 floats, a loop over all tiles, then `ForEachInSight`), then `FindMovers` (a loop over all 2*tiles). `Snap` copies and converts the whole array. The snapshot copies the explored bitset when its version changes. Rationale in the spec rests on 12,750 tiles and "1.6 KB of flags".

**Impact:** Maps are procedural per seed and may be very large. At around 1M tiles each tile the player enters costs several ms of main-thread allocation and scanning, plus an 8 MB float array, and the fog texture is uploaded in full. That is a hitch on the main thread each step.

**Recommendation:** Make target updates incremental: keep targets across calls and update only tiles newly explored or changed in sight. Track a dirty-tile list for the fog texture and use a sub-rectangle upload. Record the map-size limit if left.

**Verification:** SetTargets does assign tiles*2 floats, loop all tiles and call ForEachInSight. FindMovers scans all entries, and Snap copies the whole array. So it is O(tiles) per change, as stated. The spec sizes the map at 150x85 (12,750 tiles). The 1M-tile scenario rests on the procedural-map memory note, which is a future direction, not the current map. The cost at present size is tiny. Valid scaling note, but speculative for now.

### SPECS-BUILT-B-03: Terrain ground shader cost measured only on the dev GPU; minimum-spec GPU unmeasured

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** performance · **Status:** new · **Reviewer confidence:** low
- **Where:** `docs/superpowers/specs/2026-11-21-phase6-chunk5b-look-terrain-design.md:312`

The Outcome records the ground pass at 0.59 ms GPU and the veil at 0.20 ms. It says "12 octaves of hashed value noise and 8 field samples a pixel over the whole view, every frame". It was measured at 2560x1440 on one NVIDIA GPU and "stands, recorded at that size".

**Impact:** The minimum spec is a discrete GPU, which may be several times slower, at a resolution up to 4K. There is no octave or LOD cut at wide zoom and no cheaper fallback, and the cost moves with the camera, so it cannot be cached.

**Recommendation:** Add a spike with a measured decision rule on a lower-end or integrated-class GPU or at 4K. If over budget, add an octave cut by pixel footprint or a half-resolution wash pass. Record the decision in the spec.

**Verification:** The Outcome measured one dev GPU at 2560x1440 (grep found no minimum-spec or 4K spike in the spec). Whether a slower GPU is over budget can only be decided by measuring on that hardware. The gap is real, but the impact is speculative.

### SPECS-BUILT-B-04: measure counts SubmitFrame as main-thread work, so it blames the CPU for GPU time

- **Severity:** low · **Verdict:** deliberate · **Category:** measurement · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-21-phase6-chunk5b-look-terrain-design.md:308`

The terrain Outcome shows main-thread work up 0.4 to 1.0 ms, entirely in SubmitFrame (1.60 vs 0.79 ms) with WaitForGpu falling by the same amount. The text says `measure` counts it as work and only WaitForGpu as waiting. The item is in the known-deferred list.

**Impact:** Every GPU-heavy change shows as a CPU regression, and the work columns across the specs are not comparable. The branch table had to be re-explained by profiling.

**Recommendation:** Make measure report SubmitFrame plus WaitForGpu as one GPU wait, or as its own column. Say so in tools/AGENTS.md.

**Verification:** Recorded in sweep-up-backlog.md: 'measure counts SubmitFrame as main-thread work (found 2026-11-22 by 5b-look Task 11)', with the same fix of counting it with the GPU wait or in its own column and noting it in tools/AGENTS.md. The finding is accurate and already a known deferral.

### SPECS-BUILT-B-05: Walker labels hide under cloud shade, so measure's labels segment no longer exercises what it did

- **Severity:** low · **Verdict:** deliberate · **Category:** measurement · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-21-phase6-chunk5b-look-terrain-design.md:340`

After the cloud shade, `GatherLabels` in the labels segment fell from 1.01 to 0.11 ms, because walkers' names on unexplored land fade out even under the debug sight. The Outcome says the world run "no longer draws most names". Text-layout-budget's spec compares main with the branch with that segment.

**Impact:** The labels and widest-view segments are the only guards for label and text-layout cost. They now cover about a tenth of the label load. That makes the text-layout-budget's "no frame led by Make" acceptance easier to meet than the earlier baseline, and later label regressions are less visible. Numbers before and after the cloud shade are not comparable.

**Recommendation:** Make the debug sight's walkers and labels show over cloud, or explore the measured area in measure's setup, so label load stays comparable. Note the break in comparability in tools/AGENTS.md.

**Verification:** sweep-up-backlog.md records the same item: since the cloud shade, walkers and names on unexplored land stay hidden under --debug-sight, GatherLabels is 0.11 vs main's 0.99 ms, runs are not comparable with older ones, and the fix is a run flag exploring every tile through Host::Explore. This is a known deferral. The impact claim on the text-layout-budget acceptance is a fair inference.

### SPECS-BUILT-B-06: Relationship cell-locality claim is unmeasured and may mean 30% of NPCs never form relationships

- **Severity:** low · **Verdict:** confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-19-relationships-bounded-design.md:165`

The Outcome shows 3,680 NPCs hold none and 11,311 hold 1 to 9, unchanged from 10 h to 1000 h. The explanation (partners drawn only from the same cell) is stated as "a reading of the code that the numbers fit, not a result". Goal 4's "about 3 to 4 hours" fill time is neither confirmed nor refuted.

**Impact:** A third of the world never socialises. Relationship graphs are tile-local, which affects any later reader (travel following relationships, the UI view). For larger procedural maps the single-NPC-cell share could be larger. The spec's unconfirmed 0.07 edges per NPC per month underlies the 3 to 4 hour claim.

**Recommendation:** Log cell-size counts once in the measurement case and confirm or refute. Record the result in the spec, or in the game-design review inputs.

**Verification:** Spec lines 165-170 say 'a reading of the code that the numbers fit, not a result' and 'I did not log cell sizes'. The 3,680 / 11,311 / 35,009 groups are recorded. The claim is accurate and the limitation is openly stated. The 'never socialise' impact is plausible but unconfirmed, as the spec itself says.

### SPECS-BUILT-B-07: Traffic-count bounds moved when rivers moved: the demo-load tests are coupled to hand content

- **Severity:** low · **Verdict:** confirmed · **Category:** test-design · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `tests/unit/game/shipped_world_tests.cpp:319`

The test "the shipped roads carry 2,400 to 3,300 at once" pins five counts. River-heads moved river 9's source and the start count fell from 2,519 to 2,495, so the floor went from 2,500 to 2,400 (the user ruled it a demo load). The roads spec's Goal 8 and the world.yaml comment treated these as targets, with "share 59 gives 2,549".

**Impact:** Any further content edit near a populated region may trip the floor again, and the share, the bounds and the pins have to be retuned by hand each time. The roads spec text (2,500 to 3,300) and the river-heads spec's "stop and ask" rule (2,500) now disagree with the test and with the world.yaml comment.

**Recommendation:** Annotate the roads and river-heads specs with the relaxed floor. Consider asserting a wider load band (say 2,000 to 4,000) for the demo-load role and keep the tight pins only for the retuned costs.

**Verification:** The test is titled 2,400 to 3,300. The river-heads spec (line 191) records the floor drop from 2,500 to 2,400 as a user ruling, but its own line 137 'stop and ask' rule still says 2,500 to 3,300. The roads spec lines 117 and 214 still say 2,500. The world.yaml line 120 already says 2,400. So the stale spec text is real, though the drop itself is deliberate and recorded in the river-heads Outcome. Fix is a doc annotation only. The wider-band suggestion is an opinion.

### SPECS-BUILT-B-08: Reference timeout of 80 s rests on one 7.4 s sample from a 32-core container

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** test-design · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:895`

`REFERENCE_CASE_TIMEOUT = 80` is "ten times" the slowest case in a single container run. The Outcome admits a smaller CI runner gives one thread per case too, and calls the margin "a judgement, not a measurement". The TSan run showed cases up to 15 s on 32 cores with 4 threads.

**Impact:** A slow or loaded CI runner could time out a healthy case. The gate then reports a hang that is not one, and `reference_case_name` adds a second subprocess call per failure.

**Recommendation:** Record the slowest case on the actual CI runner after a few runs. Consider scaling the bound by the case's share, or setting an environment override.

**Verification:** REFERENCE_CASE_TIMEOUT = 80 at tools/project_n.py:895 and is used at 928 and 932. The margin is self-described as a judgement. A timeout failure reports a case index and is clearly worded. I did not check the measurement claim or the extra subprocess call.

### SPECS-BUILT-B-09: Reference runner's pass/fail leaves picture drift only as a printed note

- **Severity:** low · **Verdict:** deliberate · **Category:** test-design · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `tools/project_n.py:995`

After a successful run, pictures that differ from tests/reference/ within tolerance only print "within tolerance but changed". The Outcomes lean on the claim "lavapipe draws every case the same to the byte", for example 'no picture moved' in text-layout and fog.

**Impact:** Silent byte drift is not a failure, so "no picture moved" claims in Outcomes depend on a human reading the log. A Mesa bump could shift pictures unnoticed.

**Recommendation:** Fail or warn loudly on byte drift in CI. This is the known "reference drift guard missing" item.

**Verification:** sweep-up-backlog.md line 40 records 'Reference drift guard' as deferred. project_n.py:977 only prints 'within tolerance but changed'. Known deferral.

### SPECS-BUILT-B-10: Held movement has no collision while click routes refuse impassable tiles, so the two walks can disagree

- **Severity:** low · **Verdict:** deliberate · **Category:** design · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-22-player-walk-and-camera-design.md:23`

The spec records that held movement walks over deep water and peaks and was left alone by the user. A click route is a shortest walk only over tiles WALKING can enter. The player can explore fog and cross rivers by WASD that a click cannot.

**Impact:** Roads, fords, bridges and rivers are irrelevant to a keyboard player, and the world map's travel design (danger sites, wary profile) never applies to the player. The unreachable-by-click but reachable-by-key state is easy to forget in the game-design review.

**Recommendation:** Carry this item explicitly into the game-design review inputs. Add a test that notes the divergence on purpose.

**Verification:** The finding itself says the spec records that held movement ignores terrain and the user left it alone. The only open point is carrying it into the game-design review. I did not search game-design-gaps.md for it.
