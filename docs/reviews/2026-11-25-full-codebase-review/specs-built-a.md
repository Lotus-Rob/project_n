# Review: specs-built-a

**Scope:** docs/superpowers/specs and plans dated 2026-11-10 to 2026-11-17 that are implemented (text, abilities, seals, glow, scenes, chunk5a, 5b1, 5b2, 5c1, plus the tooling ones: faster-check, superpowers-workflow, dev-loop-speedups, ci-and-local-speed, history-first, build-folder-size, code-comment-cleanup, warnings): does each spec's Outcome section match the code on main, were promised things dropped silently, are measurements/rulings recorded, are plans consistent with specs, leftover TODOs.

**Findings:** 18 standing (0 critical, 0 high, 0 medium, 9 low, 9 info), 0 raised but refuted on verification.

**Parts:** specs-built-a.md, specs-built-a-part2.md

## Assessment

## Assessment of specs-built-a (specs and plans 2026-11-10 to 2026-11-17)

I read every Outcome section in scope in full or near-full. That covers faster-check, superpowers-workflow, dev-loop-speedups, ci-and-local-speed, history-first, build-folder-size, warnings, text, abilities (a long middle part only skimmed), seals, glow, scenes, 5a, 5b1, 5b2 and 5c1. I read the code-comment-cleanup plan in part. I did not read the plans task by task. They were only checked for status markers and checkboxes. About 40 Outcome claims were checked against code on main.

**Strengths**
- The Outcome sections are unusually honest and traceable.
  - They record what changed against the design, with a reason and a "cost if wrong" for each ruling.
  - They give dated measurements and name the machine, flags and run counts.
  - They state limits and invalid runs openly: the voided browser-contention run in glow, the 1256x1368 window correction in 4b, and the unconfirmed single-run p99 in 5b1.
- Where I verified claims against code, they held:
  - Tooling: `tidy_units` and `tidy_verdict`, `restore_lock_unless_added`, `PROJECT_N_CL_DROP_SOURCE_DEPENDENCIES`, `CI_CACHED_TOOLCHAINS`, `CACHE_DAYS` and `STALE_DAYS` of 7, `process_cpu_count`, the `shared_pch` and `SHARED_PCH_TOOLCHAINS` rules, `-Wno-effc++`, the removed unity step with its `test_check_runs_no_unity_step` self-test, the nine required checks in `.github/rulesets/main.json`, the `.lsp.json` that runs clangd through `pixi run`, and prune-worktrees.
  - Game code: `LABEL_WORLD_SIZE` 20, `NUMBER_CHUNK` 256, `INSTANCE_BYTES` 56 with its `static_assert`, `FIELD_REACH_TEXELS` 16, the gutter of 32, `MAX_SEALS_DRAWN` 64, `MOST_NAMES` 1,024, `MOST_WORLD_PEOPLE` 100,000, `JOURNAL_LENGTH` 10 and `MET_JOURNAL_LENGTH` 50, `TICKS_PER_DAY` 180, `WidestCount`, `--month-step-at` and `--travel-share`, `SAVE_FILE_VERSION` 2 with zstd, and the scene-list `static_assert`.
  - No design-process reference was found in code comments, so the comment-cleanup goal holds on main.
- Rationale is recorded and later claims are tracked. 5c1's own findings (an unbounded relationship table, the 1 GiB save limit, a month step that grows with the table) were closed by `2026-11-19-relationships-bounded-design.md`, with before and after numbers.

**Weaknesses and risks**
1. **Dropped promise.** dev-loop-speedups ends with "Still to measure (Goal 6): the glow chunk's time per task against chunk 4's, in the glow chunk's Outcome". The glow Outcome has no per-task time. Goal 6 was the point of the "sized reviews" change, so it was never closed.
2. **5a's budgets were broken without a statement.** 5b2 broke several of 5a's budgets, and 5a requires a breaking chunk to say so. The 5b2 Outcome does not.
3. **The perf budgets are prose only.** They sit in a spec table, `measure` never compares against them, and the world-run baseline keeps shifting (see the 5b2 measurement finding).
4. **Stale numbers in Outcomes.** They are frozen as of their date: `share_per_mille` 60 versus 58 now, "24 members in 10 files", chunk 4's "1280x720" window versus 4b's 1256x1368, and history-first's CLAUDE.md byte count. The Outcomes do not say they were superseded.
5. **Status markers.**
   - None of the 17 plans carry a done marker: every checkbox is unchecked, and 12 of the 17 have no Status line at all. A reader cannot tell which plans were executed.
   - The abandoned original 5-chunk world-map spec has no note saying it was split into 5a, 5b1, 5b2 and 5c1.
6. **Docs against the repo's own rule.** `src/AGENTS.md` has single lines of 5 to 15 KB, against the "~100 lines, one line per rule" rule. The 15 KB line is the sort of paragraph that makes the file unmaintainable.
7. **Open items left on the floor:**
   - which checkout's clangd a worktree gets;
   - the 5b2 month-step name prewarm;
   - the 5c1 save-time aim, missed with no lever taken;
   - three chunk 4 commits that do not build on MSVC and were left as they are.

**Overall health:** good. Process and rationale discipline is strong, and drift is mostly one-directional: facts that were true on the Outcome's date and changed later without a pointer. The substantive gaps are the unfinished goal in finding 1 and the unratified budget breach in finding 2.

**Files read in full:** (not reported)

## Findings

### SPECS-BUILT-A-01: dev-loop-speedups Goal 6 (glow chunk's time per task) was never recorded

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** promised-thing-dropped · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-12-dev-loop-speedups-design.md:275`

The Outcome ends with 'Still to measure (Goal 6): the glow chunk's time per task against chunk 4's, in the glow chunk's Outcome' (line 275). Goal 6 is also set at line 65. The glow Outcome (2026-11-12-phase6-glow-design.md, from line 296) records frame-time A/B, memory and load, but no per-task or wall time per task. Later specs (5a, 5b1, 5b2, 5c1) do not record it either. A grep for 'per task', 'Goal 6' or 'task time' finds nothing that closes it.

**Impact:** The goal that justified 'review: full/light', one gate per level and parallel waves has no before and after number. Whether the change helped cannot be shown, and the dev-loop spec stays open forever.

**Recommendation:** Either measure it from the plan ledgers or git timestamps of the glow and a later chunk and add one line to the dev-loop Outcome, or record that Goal 6 was dropped and why.

**Verification:** dev-loop spec line 275 still says 'Still to measure (Goal 6)'. Grep finds no 'time per task' in the glow spec. The other 'Goal 6' hits in 5b1, 5b2 and 5c1 are unrelated goals with the same number. Nothing closes it. It is a dropped measurement, not a code problem, so low.

### SPECS-BUILT-A-02: 5b2 breaks several of 5a's measure budgets but its Outcome never says so

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** spec-outcome-mismatch · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-15-phase6-chunk5b2-moving-design.md:340`

5a's budget table (2026-11-14-phase6-chunk5a-measure-camera-design.md, 'The first budgets') sets world work median <= 2.5 ms, work p99 <= 5.5 ms and hitches 'none'. It says 'a later chunk that breaks one says so in its Outcome, with why', and that a branch breaks a budget only when main in the same sitting keeps it. The 5b2 table has branch-after-fix work medians of 3.81 (widest view), 3.08 (labels) and 3.72 (month step). Work p99 is 7.43 in the widest view, 6.76 in zoom and 6.63 in labels. Hitches are 2 and 3, and the month-step frames are 54 to 60 ms. Main in the same sitting kept all of them (1.41, 4.40, 0). The Outcome says only that 'the margin shrank' and 'p99 moved far more than the 0.5 ms'. It never says a budget is broken, never ratifies new ones, and the world shimmer budget is the only one it explicitly restates (guard 0.06).

**Impact:** The 'budget' rule becomes decorative. Later chunks (5c, towns, combat scenes) have no agreed world baseline: 5a's numbers are void, and 5b2's are measured under a different load (see the travel-share finding).

**Recommendation:** Add a short ruling to the 5b2 Outcome (or a follow-up note in 5a) stating which budgets are exceeded, why that is accepted (the travellers' cost, the month step hiding behind its transition), and the new budgets later chunks are held to.

**Verification:** 5a line 380 does say a later chunk that breaks a budget must say so in its Outcome, and the 2.5 ms median row is there. 5b2 grep shows no ruling on budgets, only 'the margin shrank' at line 340. The table sits after the 5b2 measurement with a different load (travel share 200), so most of the gap is the load rather than a regression. The missing explicit statement is real. Medium is too high.

### SPECS-BUILT-A-03: 5b2's measure run uses --travel-share 200, so it is not the shipped game's cost and not like-for-like with main

- **Severity:** low · **Verdict:** confirmed · **Category:** measurement-validity · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-15-phase6-chunk5b2-moving-design.md:300`

The branch runs `--travel-share 200 --month-step-at 15`. Shipped `share_per_mille` is 58 or 60, which gives only 650 to 884 travellers in the busiest view. Main has no such flag and ran with about 3,120 stand-in walkers. The Outcome is candid about 'not like for like', but that makes the 'main vs branch' summary table read as a regression of 0.7 to 1.4 ms up to 1.5 to 3.8 ms of work median. That figure is mostly the artificial 3x crowd that Goal 18 required, not what a player sees at the shipped share.

**Impact:** The new baseline for later chunks is a stress configuration, with no number for the shipped share. Budgets (see the previous finding) cannot be judged against either.

**Recommendation:** Record one measure run at the shipped share (and one at share 200) as the two baselines, and say which one budgets refer to.

**Verification:** 5b2 lines 293 to 298 and 430 to 433 state the run is 'not like for like' and uses --travel-share 200. The shipped share gives only 650 to 884 travellers in the busiest view against 2,304 to 2,813 at share 200. The spec is open about this and says Goal 18 required it. The only gap is that no run at the shipped share is recorded.

### SPECS-BUILT-A-04: World shimmer budget replaced by a looser guard; acceptance check 0.02 still outstanding

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** known-gap · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-15-phase6-chunk5b1-map-design.md:418`

5a set shimmer <= 0.031 (measured 0.0309). 5b1 replaced it with a 0.06 guard (0.0397, then 0.0436 after harbour reshaping). 5b2 reaches 0.0482. The backlog item '5b-look' carries the real acceptance 'world shimmer <= 0.02'. The drift 0.0309, 0.0397, 0.0436, 0.0482 is each time explained as new content, and the guard is about 1.25 to 1.4 times the measure each time.

**Impact:** Each chunk raises the 'ceiling' by the amount it adds. The 0.02 acceptance may never be reached if 5b-look does not land before more content is added.

**Recommendation:** Keep 5b-look scheduled before chunk 6 (towns); until then, pin the guard at a fixed value, not a ratio of the current measure.

**Verification:** I did not re-verify the shimmer numbers in the Outcomes. The finding itself carries status known-deferred, with the 5b-look backlog item holding the 0.02 acceptance. That makes it a recorded deferral. The 'pin the guard' suggestion is an opinion.

### SPECS-BUILT-A-05: Cost-model hazard: Travel::Finish scans every live journey and merges the whole journal table each call

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/game/travel.cpp:308`

`Finish` iterates all `m_journeys`, and when any ended it stable-sorts the written entries and moves every existing `m_journals` element into `m_merged` (O(total journals)). The 5b2 Outcome notes it is 'quadratic when journals are built one journey at a time' and so could not run the 50,000-NPC save-size test. The sizes are extrapolated from 200 and 400 NPCs. It is only called per month, so it is fine now, but it is O(world) per call by design.

**Impact:** Bigger procedural worlds, or any future path that finishes journeys per day or per tick (the spec says days drive the calendar), would hit the O(N) merge with 100,000 journals.

**Recommendation:** Keep it month-only with an assert, or keep journals in a node or hash structure keyed by NPC. The 5c1 delta-save measurement later covered 50,000 NPCs, so the linear extrapolation could be replaced by its numbers.

**Verification:** Travel::Finish (src/game/travel.cpp:308) scans m_journeys and merges journals as described. It is only called from the month step (line 457), so the claim of per-call O(world) cost holds but is a speculative future hazard. The recommendation to reuse the 5c1 measurements is reasonable. Low confidence.

### SPECS-BUILT-A-06: 5c1 aim 'saving no slower than today's ~16 ms' was missed and not revisited

- **Severity:** low · **Verdict:** unverifiable · **Category:** promised-thing-dropped · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-16-phase6-chunk5c1-world-kept-delta-saves-design.md:316`

The aims table: serialise 25.5 ms plus 4.0 ms write after three months, against the aim of 16 ms. It is marked 'missed'. The levers (narrower places and masks, run-length IDs, a cheaper by-ID lookup) are listed as 'none taken'. The 'relationships-bounded' follow-up fixed the unbounded growth, not this. At 250 h the bounded save is 49.7 ms serialise plus 30.2 ms write, with 50,000 NPCs changing every month, so the delta is a whole save for the NPC domain (2.2 MB, about 44 B per NPC).

**Impact:** Saves on a frozen tick take 30 to 80 ms. That is acceptable per 0002 s7.6, but the 'delta' design costs more than it saves for the world domain: every NPC changes every month, so the delta machinery adds a compare pass and format complexity for no size gain.

**Recommendation:** Decide explicitly whether the world domain keeps the delta, or is written whole with the base only used for regeneration. Record the decision in a design doc or the sweep-up backlog.

**Verification:** I did not open the 5c1 spec's aims table or measurements, so the figures are unchecked. The spec marks the aim 'missed' per the finding. Whether the world domain should keep the delta is a design opinion, and the finding itself lists it as known-deferred.

### SPECS-BUILT-A-07: Open question about which checkout's clangd and compile database the LSP uses in a worktree was never closed

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** open-question · **Status:** new · **Reviewer confidence:** low
- **Where:** `docs/superpowers/specs/2026-11-10-superpowers-workflow-design.md:187`

The superpowers-workflow Outcome says 'Still to be seen: which checkout's clangd and compile database the LSP uses inside a worktree'. The faster-check Outcome (also 2026-11-10) says it 'loads at the next session start' and 'Untested on Windows'. `.claude/marketplace/plugins/clangd/.lsp.json` runs `pixi run --manifest-path ${CLAUDE_PROJECT_DIR}/pixi.toml clangd --background-index` with no compile-commands setting. AGENTS.md tells reviewers to use LSP `findReferences`, which depends on `build/compile_commands.json` being fresh in that checkout. No Outcome records the answer.

**Impact:** Reviewers in a worktree may query the main checkout's stale database, or none, and silently get incomplete reference lists. AGENTS.md requires that tool over grep.

**Recommendation:** Test once in a worktree and add the result to tools/AGENTS.md or the spec, or set `--compile-commands-dir`.

**Verification:** The 'Still to be seen' text was not found by grep in the workflow spec (it may be worded differently). .lsp.json confirms no compile-commands-dir flag. clangd runs with the pixi manifest under CLAUDE_PROJECT_DIR, which is the worktree root when a session runs in one, so it likely finds that checkout's build/compile_commands.json. The risk is a stale or missing database in a fresh worktree, which is plausible but untested.

### SPECS-BUILT-A-08: All 17 plans lack a completion marker; checkboxes are all unchecked and 12 have no Status line

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** docs-hygiene · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/plans/2026-11-15-phase6-chunk5b2.md:1`

Every in-scope plan has `- [ ]` steps and no `- [x]`, including the implemented 5b2 (95 unchecked). Only 5 of the 17 (5b1, 5b2 and code-comment-cleanup with 'Status: approved ...', plus two more) have a Status line at all, and none says implemented or superseded. The code-comment-cleanup plan says 'Spec: none'. The decisions and rulings live in 5b2's spec Outcome ('Rulings made in the comment cleanup' F1..U2).

**Impact:** A fresh session or the history-recall agent cannot tell an executed plan from an open one, and might re-run or trust stale steps. The cleanup rationale is only findable inside an unrelated spec.

**Recommendation:** Add 'Status: executed (branch / Outcome pointer)' to each plan's header, or move executed plans into an archive folder. Give code-comment-cleanup a short design note.

**Verification:** A grep for '^Status' in the plans found 0 matches in all of them, so the Status lines may use another format, for example bold. The finding's count of 5 with a Status line is therefore unverified. Unchecked boxes and no completion marker are plausible, since they are consistent with the repo's practice of recording outcomes in the specs. The harm is minor.

### SPECS-BUILT-A-09: src/AGENTS.md has single lines of 5 to 15 KB, breaking the repo's 'one line per rule' standard

- **Severity:** low · **Verdict:** confirmed · **Category:** agents-md-violation · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/AGENTS.md:4`

Root AGENTS.md says an AGENTS.md holds what any task needs 'one line per rule' at about 100 lines. src/AGENTS.md is 49 lines, but line 4 is about 10 KB and others are 7.3 KB, 15.4 KB (line 80), 5.4 KB and 7.3 KB. tests/AGENTS.md has a 5.8 KB line and .github/AGENTS.md has a 3.3 KB line. The Outcomes keep adding to these as 'a gotcha' (per-chunk additions are visible in the text).

**Impact:** The files are loaded into every session's context. A 15 KB paragraph is expensive on every turn, with rules buried inside, and a reviewer checking a diff against the AGENTS.md of each touched directory cannot find a rule.

**Recommendation:** Split the long paragraphs into one bullet per rule, or move the per-subsystem data-flow prose to docs/design and leave pointers.

**Verification:** src/AGENTS.md has single lines of 7.3 KB (line 4), 7.3 KB (line 24), 15.4 KB (line 27) and 5.4 KB (line 31). tests/AGENTS.md line 20 is 5.8 KB and .github/AGENTS.md line 13 is 3.3 KB. The line numbers and sizes differ somewhat from the finding (the 10 KB and 'line 80' claims are wrong; the file has about 49 lines), but the violation of the 'one line per rule' standard holds.

### SPECS-BUILT-A-10: 5a's budgets exist only as prose in a spec Outcome and are not machine-checked

- **Severity:** info (reviewer said low) · **Verdict:** deliberate · **Category:** design-weakness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-14-phase6-chunk5a-measure-camera-design.md:389`

The 'first budgets' table (work median, p99, logic p99, input latency <= 50 ms, shimmer <= 0.031) lives only in the spec. `tools/project_n.py` defines `HITCH_MS` and prints rows, but has no budget constants and no pass or fail line. `measure` is deliberately not a gate, but nothing even annotates a row as over budget. The spec relies on later chunks reading the table by hand.

**Impact:** A regression or budget breach is noticed only if a reviewer remembers the table. 5b2 already exceeded several silently (see above).

**Recommendation:** Put the budgets in one place that `measure` can print next to its numbers (a table in tools/project_n.py or a YAML), still never a gate, and point the spec at it.

**Verification:** 5a presents the budgets as a prose table, and measure is documented as never a gate. Whether to machine-annotate them is a design preference, not a defect.
