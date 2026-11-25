# Review: specs-built-a (part 2)

### SPECS-BUILT-A-11: Outcome figures drifted after the spec was written: travel share 60 now 58

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** stale-doc · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-15-phase6-chunk5b2-moving-design.md:445`

The Outcome records the tuned values 'share_per_mille: 60' and 'travel: share 60' and ties them to 2,500 to 3,300 travellers. `assets/data/world.yaml:124` now says `share_per_mille: 58`, changed in 'chore(data): reroute the roads with danger-aware travel' on 2026-11-22. The 5c1 Outcome says travel 'was not retuned', which is true for its own commits but now only partly describes main. The 'Renumbering' table in 5c1 is likewise a snapshot.

**Impact:** A reader taking the Outcome as current gets the wrong value. Minor, but there is no pointer from the Outcome to the later change.

**Recommendation:** Add 'since changed to 58 (commit subject)' or restate Outcome values as 'as built'.

**Verification:** world.yaml:124 now has share_per_mille: 58. The 5b2 Outcome line 429 gives 'share 60' for the tuning counts, which were true when written. Outcomes are dated snapshots, so this is expected drift. Cosmetic.

### SPECS-BUILT-A-12: warnings Outcome's counts are already stale: '24 members in 10 files' vs 27 NOLINT lines in 12 files

- **Severity:** info · **Verdict:** confirmed · **Category:** stale-doc · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-17-warnings-design.md:10`

The Outcome says '24 members in 10 files keep {} under NOLINTNEXTLINE(readability-redundant-member-init)'. On main there are 27 occurrences in src/ and tests/ (excluding the AGENTS.md line), across 11 source and header files plus one test file. Examples: frame.hpp 11, combat.hpp 3, app.hpp 2, game.hpp 2, manifest.hpp 2. Later chunks added to the count.

**Impact:** Cosmetic. It does show that the suppression pattern the spec chose to keep grows with each new struct, which is the same churn the spec set out to remove for -Weffc++.

**Recommendation:** Re-state as 'as of this branch'. Consider whether the designated-initialiser rule can be avoided for aggregates, so the pattern stops growing.

**Verification:** grep -rn for NOLINTNEXTLINE(readability-redundant-member-init) over src and tests gives 28 lines, against the claimed 24. This count includes the AGENTS.md mention only if that file is under the searched dirs, so it is roughly 27 to 28. The Outcome is a snapshot of the branch. Cosmetic.

### SPECS-BUILT-A-13: Chunk 4 Outcome's profiling window ('1280x720') was shown wrong by 4b but never corrected

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** stale-doc · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk4-abilities-design.md:735`

Chunk 4's measurement paragraphs say 'the arena, 1280x720'. The seals Outcome later records that during each run `niri msg windows` showed the window at 1256x1368, 'so chunk 4's and the glows' 1280x720 were very likely this size'. 5a then calls the earlier runs 'a 720p window (tiled to whatever niri gave it)'. The chunk 4 and glow tables still print 1280x720 as fact, and glow's A/B is unaffected only because both arms share the size.

**Impact:** Anyone comparing pixel-bound numbers across chunks misreads the resolution. The pixel counts differ by about 1.9x.

**Recommendation:** Add a one-line correction in the chunk 4 and glow measurement sections pointing at the 4b finding.

**Verification:** The 4b spec lines 571 and 679 record the correction: the window was 1256x1368, and chunk 4's and glow's '1280x720' were very likely this size. Chunk 4 and glow still print 1280x720 without a back-pointer. So the claim 'never corrected' is wrong, since the correction is in 4b. Only the missing back-reference holds. The claimed '1.9x' pixel difference was not checked.

### SPECS-BUILT-A-14: Three chunk 4 commits do not build on MSVC and were deliberately left

- **Severity:** info (reviewer said low) · **Verdict:** deliberate · **Category:** history-hygiene · **Status:** contradicts-recorded-decision · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk4-abilities-design.md:438`

The Outcome records that three commits ('swing a sword's aim...', 'keep a sword on its target near and after its arrival', 'cover the swing's arrival clamp and the commit boundary') have `volley_tests.cpp` with a temporary inside `CHECK` (C4866) and 'are left as they are, by this decision'. Also, of 80 commits only 30 were ever built on windows-msvc and linux-gcc release.

**Impact:** git bisect across that range fails to build on MSVC. This is acceptable per the user's per-task verification rule, and the C4866 warning is now disabled for tests.

**Recommendation:** No action needed beyond remembering that history is only verified at task heads. Note it in the bisect guidance if one exists.

**Verification:** The Outcome at chunk4 spec line ~441 states the three commits are left 'by this decision'. The finding itself admits this and recommends no action; the C4866 warning is handled for tests (line 303). The status 'contradicts-recorded-decision' is wrong.

### SPECS-BUILT-A-15: The original chunk 5 world-map spec and the chunks 6 to 11 specs carry no note that the work was split or that their facts are unverified

- **Severity:** info · **Verdict:** unverifiable · **Category:** docs-hygiene · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk5-world-map-design.md:3`

Status is 'approved 2026-11-10' with no Outcome, yet the work was built as 5-scenes, 5a, 5b1, 5b2 and 5c1 (5b-look and 5b3 are still open). Later specs say 'its 5c, split here' only on their side. The unbuilt chunk 6 to 11 specs have the same 'approved' status. They sit in the same folder as implemented ones and predate facts learned later: the 20,000 walkers, measure budgets, journeys as records and delta saves.

**Impact:** The history-first process pulls these into reports as design intent. Some contradict what was built (the world is procedural per seed, towns come after a persistence refactor).

**Recommendation:** Mark the chunk 5 spec 'split into ... (see)' and mark unbuilt chunk specs 'not started; revalidate against 5c1 Outcome and game-design brainstorm before planning'.

**Verification:** I did not read the chunk 5 spec header. A docs-hygiene suggestion; it is plausible but I did not check it.

### SPECS-BUILT-A-16: history-first Outcome records state of non-repo global files that cannot be verified and are already out of date

- **Severity:** info · **Verdict:** unverifiable · **Category:** docs-hygiene · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-13-history-first-design.md:146`

The Outcome's table describes ~/.claude/skills/recall-history, ~/.claude/agents/history-recall.md, the reminder hook and a '~/.claude/CLAUDE.md 1,533 bytes'. These live outside the repo, are not versioned, and the CLAUDE.md has since gained other sections (cross-model review and more). The same Outcome folds in an unrelated repo fix ('fix(core): keep glow's half constant unique across unity batches'), now moot since unity builds are gone.

**Impact:** A project spec holds a changelog for files no clone has. Anyone reproducing the setup, or auditing 'the process' from the repo, has no source of truth.

**Recommendation:** Commit the skill, agent and hook (or a copy) under the repo or a dotfiles repo, or reduce the Outcome to behaviours plus 'global files, not versioned'.

**Verification:** The Outcome describes files in ~/.claude that I did not check or compare. The point that they are unversioned is true by construction. This is a docs preference and has little impact.

### SPECS-BUILT-A-17: Chunk 3 Outcome cites commits by hash, in tension with the later 'cite by subject' rule

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** docs-hygiene · **Status:** new · **Reviewer confidence:** low
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk3-text-design.md:227`

'What was built' lists `a3b4943`, `4440e1a`, `8714d27`, ... and says they are main's, after the rebase merge, so they are stable. AGENTS.md now says docs written on a branch cite commits by subject because the rebase rewrites every hash. The chunk 3 Outcome was written on the branch and relies on after-the-fact hash substitution. Chunk 4 onward use subjects.

**Impact:** Minor. The hashes are probably right but were edited in after the merge, which is the very step the rule is meant to avoid. A hash replaced after a later force-rebase of the history would break.

**Recommendation:** Convert to subjects like the later specs, or leave and mark as post-merge hashes.

**Verification:** Chunk 3 Outcome (line ~227) does cite hashes and states they are main's after the rebase merge (PR #20). That is a deliberate, stated post-merge substitution, and hashes are stable once on main, which AGENTS.md permits. It is a style inconsistency with later specs, not an error. I did not check the hashes against git.

### SPECS-BUILT-A-18: Chunk 3's text outcome lists ruling 47-50 core Jobs/TSan fixes outside the chunk scope without a test pointer for later regressions

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** low
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk3-text-design.md:418`

Ruling 47 fixes a `core::Jobs` Pending-reset race with a test that runs the race 150,000 times, yielding. Ruling 50 keeps the TSan fork-edge changes in this branch. The Outcome says `core_tests`, `gfx_tests` and `app_tests` pass under TSan, but TSan is 'on request only' (`check --sanitizers`) and not in the gate or CI. The same applies to the unguarded concurrent `TextLayout` use (ruling 11, 'the header documents it').

**Impact:** Nothing keeps the Jobs race and TSan cleanliness fixed. A 150,000-iteration test is a probabilistic guard that can slow the suite or flake on slow CI.

**Recommendation:** Confirm that the 150,000-iteration test is cheap in CI. Consider one TSan run in CI for core_tests, or record that it is manual.

**Verification:** The test exists at tests/unit/core/jobs_tests.cpp:178 (ROUNDS=150'000). Its comment says a run takes about 0.15 s, so it is cheap, and both spins yield so a one-core runner is not held up. It is also deterministic in effect: the comment says a miss is about e^-18 likely, so the 'probabilistic flake' worry is mostly unfounded. The real point holds: TSan runs only on request, so TSan cleanliness is not gated. Ruling 50 records the fork-edge changes as a deliberate decision. Low value.
