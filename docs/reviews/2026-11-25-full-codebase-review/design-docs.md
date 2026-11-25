# Review: design-docs

**Scope:** docs/design/*.md (all ten): soundness of each decision, internal contradictions, and DRIFT versus the code as it is now (verify claims against src/, tools/, xmake.lua).

**Findings:** 36 standing (0 critical, 0 high, 6 medium, 24 low, 6 info), 0 raised but refuted on verification.

**Parts:** design-docs.md, design-docs-part2.md, design-docs-part3.md, design-docs-part4.md

## Assessment

## Design docs (docs/design/*.md): assessment

**Strengths**
- The decisions are unusually well argued. Almost every choice gives its reason, the alternatives it beat (0001 Appendix A, 0002 Appendix A), and the measurement or upstream source it rests on (0002 Appendix B). The spike tables (S1–S14) set pass criteria before the work, and 0001 §7.1's numbered rules each go back to a concrete failure. That is the doc set's best feature: a reader can tell why the build looks the way it does.
- The core runtime architecture is sound and matches the code closely. Points checked against src/:
  - the handover's three channels and five buffers (`core/handover.hpp`: WAITING=2, BUFFERS=5, the 30+3+1 stale margin);
  - the TickClock and adaptive display delay constants;
  - counter-based RNG;
  - the `core::math` lint (PLATFORM_MATHS_RE limited to core/game);
  - the FP environment set on worker start through enkiTS callbacks;
  - fork-join with fixed chunks, and the 1-vs-N worker tests;
  - stable IDs, with unordered containers used only for lookup;
  - the module DAG enforced by XMake deps and `MODULES`;
  - facades (`FACADES`), the shared PCH rules, sanitizer/LTO per-target rules, and the `/std:c++23preview` and `/Brepro` handling;
  - the LFS filter wiring, and CI's nine required checks.
- The sub-docs (0002.1–0002.4.2) mark their departures and "as built" notes honestly, and point to the superpowers spec Outcomes when they are superseded.

**Weaknesses**
- **Drift is the main problem.**
  - 0001 still describes the pre-superpowers agent process: skills and agents removed on 2026-11-10 (agent-docs, fix-bug, review-branch, branch-reviewer, bug-reproducer), a root AGENTS.md "draft", and an outdated command table.
  - 0002 is still "Draft" with "phases 1 to 5", and its module list, instance layout, buffer counts and replay description lag the code. 0002.2's replay API and file format are three format versions behind, and it contradicts itself about sorting.
  - The project knows 0002/0002.x need bringing in line, but 0001 has no such entry.
  - The doc set breaks its own rules: "design docs superseded rather than edited", "a fact lives in one tier", and "AGENTS.md ≤ ~100 lines, one line per rule". src/AGENTS.md is 45 KB with a 15,404-character "line".
- **Rules stated as already followed but never built:**
  - per-tick and per-worker arenas, and the 16 MB arena budget;
  - cross-entity command merging by (target, source, order);
  - the spatial hash grid;
  - Tracy plots and statistics;
  - focus-pause as a setting;
  - FP-environment checks on workers.
  Readers will take these as how the code works.
- **Robustness claims overstate the code:**
  - "saves can't be corrupted by a crash" has no fsync;
  - "exceptions end at main's or the logic thread's handlers" ignores enkiTS workers, which run the save writer and the month-step chunks, and there is no terminate handler or log flush;
  - "symbols kept for crash reports" has nothing that captures a crash;
  - "PIE" is never set or checked.

**Design risks to raise at the planned full review**
1. The delta-save scheme (0002 §7.6) depends on a hand-bumped `GENERATOR_VERSION`. It has stayed at 1 while world generation kept changing (travel profiles, danger pricing), and nothing checks the regenerated base. A forgotten bump mis-applies a save's delta to a different base without any error.
2. Memory and time under the delta scheme. It keeps a full base world beside the running one (and a scratch base, and copies during load), and every load regenerates the whole world. Neither appears in §13's 50 MB budget, and both scale with procedural map size.
3. Positional SpriteIds contradict §9's "stable asset ID". Saves don't record the manifest, so a reordered sprites.toml redraws loaded entities with the wrong sprites instead of refusing the save.
4. Stagnant DragonBones (S14 unrun), and uncapped rendering by default (S10 unrun).
5. Non-goals (modding, localisation) contradict the backlog and 0001 §11.3.

**Overall health:** good architecture and good rationale, with significant, mostly known doc drift. Before phase 7, 0001 needs a sweep to match the superpowers process, 0002 should be promoted out of "Draft" with its unbuilt rules marked as such, and the three robustness claims (save durability, worker exceptions, crash capture) should be fixed in code or softened in the docs.

**Files read in full:** docs/design/0001-repository-build-environment.md, docs/design/0001.1-git-lfs.md, docs/design/0002-engine-architecture.md, docs/design/0002.1-app-and-game.md, docs/design/0002.2-replays.md, docs/design/0002.3-input-data-scenes.md, docs/design/0002.3.1-manifest-and-scene-loading.md, docs/design/0002.4-ui-and-text.md, docs/design/0002.4.1-scene-target-and-composite.md, docs/design/0002.4.2-text-and-mesh-path.md, xmake.lua, pixi.toml, /tmp/claude-1000/-home-lotus-Development-project-n/5e986e63-9457-419a-a1f3-2b39adbcdc44/scratchpad/review-history.md

## Findings

### DESIGN-DOCS-01: 0001 still documents removed skills/agents and the pre-superpowers agent process

- **Severity:** medium · **Verdict:** confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0001-repository-build-environment.md:116`

§4 layout lists `.claude/agents/` branch-reviewer, log-triage, bug-reproducer and skills agent-docs, fix-bug, review-branch; §10.4 says 'The agent-docs skill encodes these rules'; §10.5 marks fix-bug, review-branch, branch-reviewer and bug-reproducer as created; §15 phase 2 lists the agent-docs skill. All but log-triage were deleted on 2026-11-10 (commit 'docs(agents): hand the process to superpowers'), and root AGENTS.md now says skills are written with superpowers writing-skills and reviews run through requesting-code-review. 0001's Revised log never mentions the change.

**Evidence:** `ls .claude/skills` → add-dependency bump-toolchain fix-xmake-package git-commit handover look-at-the-game release; `ls .claude/agents` → log-triage.md only; commit 34a1e238 body: 'Removed: the fix-bug, review-branch and agent-docs skills and the branch-reviewer and bug-reproducer agents'.

**Impact:** An agent or human reading the rationale tier will look for skills that don't exist, or follow the 3-round branch-reviewer process that superpowers replaced. This contradicts the AGENTS.md rule 'read docs/design before redesigning anything'.

**Recommendation:** Add a 2026-11-10 revision to 0001: update §4's .claude lines, rewrite §10.4/§10.5 to point at superpowers (writing-skills, requesting-code-review, test-driven-development) and the skills that exist now, and mark the backlog table historical.

**Verification:** 0001 lines 116-118, 511, 528, 545, 559-563 and 759 still name agent-docs, fix-bug, review-branch, branch-reviewer and bug-reproducer. `ls .claude` shows only log-triage in agents, and the skills are add-dependency, bump-toolchain, fix-xmake-package, git-commit, handover, look-at-the-game and release. grep for '2026-11-10' and 'superpowers' in 0001 finds no revision entry for the removal.

### DESIGN-DOCS-02: AGENTS.md size rule (≤~100 lines, one line per rule) is met only on paper; src/AGENTS.md is 45 KB

- **Severity:** medium · **Verdict:** confirmed · **Category:** design-weakness · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0001-repository-build-environment.md:467`

§10.1 limits AGENTS.md to '≤ ~100 lines; one line per rule' so that always-loaded agent context stays small, and moves any rule needing more than a line into a skill. Measured, the line counts pass but the files have grown huge: src/AGENTS.md is 49 lines and 45,264 bytes, with one line of 15,404 characters (the scene/host rule); tests/AGENTS.md is 19 KB (longest line 5,806); tools/AGENTS.md is 21 KB; .github/AGENTS.md is 8.5 KB with a 3,251-character line.

**Evidence:** wc/awk over every AGENTS.md: src/AGENTS.md lines=49 bytes=45264 longest=15404; tests 19275/5806; tools 21137/2984.

**Impact:** Roughly 100 KB of 'always loaded' text whenever an agent works in src/ or tests/, which goes against D10's stated goal ('keeps agent context small'), costs tokens on every turn, and buries the one-line rules. The metric (lines) can't detect the problem.

**Recommendation:** Change the rule to a byte or token budget (e.g. ≤ 8 KB per AGENTS.md, ≤ ~300 characters per rule line) and add it to `doctor --docs` or lint. Move the long scene/host/save narrative into the specs' Outcomes or Doxygen, leaving a one-line pointer.

**Verification:** `wc -c` gives src/AGENTS.md 45264 bytes, tests 19275, tools 21137 and .github 8504. The §10.1 rule limits lines, so a line-count check passes. A byte budget is a design choice, but the drift from the stated context-small goal is real.

### DESIGN-DOCS-03: Per-tick/per-worker arenas and the command-merge rule are presented as followed, but neither exists

- **Severity:** medium · **Verdict:** confirmed · **Category:** architecture-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:244`

The doc presents several mechanisms as rules 'game code follows from phase 1': §7.2 'Per-tick memory comes from arenas reset each tick (one per worker); hot systems don't touch the global heap'; §7.5 rule 5 'Per-worker arenas; no global allocation in hot systems'; §13 'Per-tick arenas 16 MB'; and §7.2/§7.4 'cross-entity writes are commands ... merged sorted by (target, source, emission order)'. None of this exists: there is no arena or pmr allocator anywhere in src/, and no command type. Hot paths allocate freely (e.g. combat.cpp's Tag() returns a fresh std::vector per strike), and the month step merges per-chunk outputs in chunk order instead.

**Evidence:** `grep -rn 'pmr|monotonic|Arena' src` → none; `grep -rln Command src/game src/core` → none; game/combat.cpp line 72.

**Impact:** Reviewers and implementers take as given a memory model and a determinism mechanism that don't exist. Heap churn in hot systems at the 100,000-instance or 50,000-NPC scale is unmeasured against the stated budget.

**Recommendation:** Either build them (a reset-per-tick arena per worker, and a command buffer with a defined merge order) or rewrite the rules to what the code does (chunk-order merges; allocation measured with Tracy). Remove the 16 MB arena budget from §13.

**Verification:** Line 244 (arenas reset each tick) and line 286 (per-worker arenas) are stated as rules. grep finds no pmr:: or monotonic_buffer in src; the only 'Arena' hits are the arena scene. No arena allocator exists. I did not re-verify the command-merge claim. The month step merges in chunk order, as the AGENTS.md text describes. Line 537 still budgets 16 MB for per-tick arenas. Doc drift. The measured impact on heap churn is unestablished.

### DESIGN-DOCS-04: Delta saves trust a hand-bumped GENERATOR_VERSION; generator code changes can mis-apply a save without error

- **Severity:** medium · **Verdict:** partially-confirmed · **Category:** save-integrity · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:293`

§7.6 treats the world a seed makes as a pure function of seed, world data (BaseHash) and `game::GENERATOR_VERSION`. A save holds only the delta, with removals encoded by position in the base's stable-ID and journey order. GENERATOR_VERSION has been 1 since it was introduced. Generation code has changed several times since (e.g. 'feat(game)!: send travellers by the profile their power picks' changed Travel::Seed, and 'mark ground near danger sites' changed route costs), which was caught only because WORLD_VERSION happened to be bumped for format changes. A pure generation change (StartingPower bands, spawn spread, seeded journeys) without a format change would let an old save's positional delta apply to a different base.

**Evidence:** src/game/world_state.hpp line 30 GENERATOR_VERSION = 1; `git log -S'GENERATOR_VERSION = '` shows only its introduction; host.cpp line 164 stores no hash of the regenerated base.

**Impact:** Wrong NPCs deleted or modified, and journeys rebuilt over different routes: a corrupt but 'valid' world, with no log line. This is the failure the delta design must refuse.

**Recommendation:** Store the base world's hash (Host::WorldHash of m_base at generation) in the save. After regenerating at load, compare and refuse on mismatch, naming the cause. GENERATOR_VERSION then becomes a fast pre-check rather than the only guard.

**Verification:** GENERATOR_VERSION = 1 is hand-set (world_state.hpp line 30). The save stores seed, GENERATOR_VERSION and GameData::BaseHash() (host.cpp lines 164 and 288). That hash covers only the world data (map and names), not the generated base. So a pure generation-logic change without a version bump would apply a positional delta to a different base. The finding is right that the guard is manual and that no hash of the generated base is stored. The doc states this design openly in line 293, and there is no compatibility before 1.0. The claim that 'no log line' appears is not checked. The impact is real but pre-1.0 it is low to medium.

### DESIGN-DOCS-05: 'Saves can't be corrupted by a crash' is overstated: no fsync and close errors unchecked

- **Severity:** medium · **Verdict:** confirmed · **Category:** data-integrity · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:295`

§7.6: 'the worker writes a temporary file, flushes it, then renames it over the old save'. WriteSaveFile calls std::ofstream::flush(), which only moves the stream buffer into the OS cache, then renames. There is no fsync/FlushFileBuffers of the file (or of the directory on POSIX), and the stream's close() result after the scope is never checked.

**Evidence:** src/core/save_file.cpp lines 78–95.

**Impact:** A process crash is survivable, but a power loss or OS crash soon after a save can leave a zero-length or partial file under the save's name: rename is ordered before the data blocks on some filesystems, and ext4's auto_da_alloc only mitigates. A deferred write error (full disk, network folder) shows up only at close and is ignored, so a truncated save replaces a good one.

**Recommendation:** Write, flush, fsync (FlushFileBuffers on Windows), close and check, then rename, then fsync the directory on POSIX. Keep the previous save as .bak until the new one is verified. Adjust the doc's claim to the guarantee actually given.

**Verification:** save_file.cpp 70-95 checked: write, flush, then the `!output` check, then the stream is destroyed with no close() check. There is no fsync and no directory sync. Rename follows directly. A close-time error would go unnoticed. The doc's 'flushes' claim is true but weaker than the finding's reading of crash safety. Power-loss durability is not guaranteed.

### DESIGN-DOCS-06: Exceptions on job-system workers terminate the game unlogged, contrary to §12

- **Severity:** medium · **Verdict:** confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:500`

§12: 'anything that escapes ends at main's handlers, or on the logic thread at the thread's entry, which logs Critical'. enkiTS worker threads run ParallelFor chunks (month steps, publishing), Start decodes and the Background save writer. jobs.cpp's ExecuteRange bodies have no try/catch, and there is no std::set_terminate, so std::bad_alloc on a worker (WriteSaveFile allocates ZSTD_compressBound(state) plus a copy) or a std::array::at out_of_range (e.g. scenes.cpp line 96) calls std::terminate, and Quill's queue is not flushed. 'Our code throws none' is also not quite true, since .at() is used.

**Evidence:** src/core/jobs.cpp lines 40, 77, 124 (no try); app.cpp line 282 wraps only the logic thread; main.cpp line 210 only the main.

**Impact:** An autosave or month step that fails in a hard-to-reproduce way leaves no log, which defeats the logging design.

**Recommendation:** Catch in each ExecuteRange, store the exception and rethrow on the joining thread (or log Critical, flush and abort). Install a terminate handler that logs and flushes. Update §12 to cover workers.

**Verification:** grep for try/catch in jobs.cpp finds nothing, and no std::set_terminate exists in src. An exception on an enkiTS worker therefore reaches terminate. The doc's §12 wording covers only main and the logic thread. Impact is modest because the code mostly doesn't throw, apart from bad_alloc and .at().

### DESIGN-DOCS-07: Doc-tier rules contradict practice: accepted design docs are edited in place, and facts are deliberately duplicated

- **Severity:** low · **Verdict:** confirmed · **Category:** doc-process · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0001-repository-build-environment.md:469`

§10.1 says design docs are 'superseded rather than edited once accepted', and that 'a fact lives in exactly one tier'. In practice 0001 (Accepted) has a revision log of more than 20 in-place edits, 0002.2 'revised §3 and §4 to what was built', and §5.0 tells authors to keep the install table in three places (README, `doctor` INSTALL_HINTS, this doc). §10.3 also embeds a full 'Root AGENTS.md draft' that has since diverged from the real AGENTS.md.

**Evidence:** 0001 header 'Revised' cell; 0002.2 status line; §5.0 'Keep the three in sync'; §10.3 code block vs AGENTS.md.

**Impact:** The rules mislead about how docs evolve, and the duplicates drift (the §10.3 draft already has).

**Recommendation:** Restate the policy as 'living docs with a revision log; superseded only by a redesign'. Delete the §10.3 draft (link AGENTS.md instead) and make README or INSTALL_HINTS the single source for host install commands.

**Verification:** 0001 line 469 says 'superseded rather than edited' once accepted and line 472 says 'a fact lives in exactly one tier'. 0001 is an Accepted doc with a long in-place revision log, and §10.3 (line 486) holds a draft root AGENTS.md copy. The practice contradicts the stated policy. I did not check the §5.0 'keep the three in sync' text or the exact divergence of the draft from the real AGENTS.md.

### DESIGN-DOCS-08: 0001 §8 command table is missing commands and options that exist

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0001-repository-build-environment.md:408`

The 'Does' table omits several things project_n.py's parser defines and root AGENTS.md documents: `measure`, `prune-worktrees`, `lfs-key`, `lfs-prune`, `prune-build-cache`, `prune-ci-caches`, `clean --stale/--days/--images`, and `reference --preset/--update`.

**Evidence:** make_parser() in tools/project_n.py ~line 3256–3352; AGENTS.md Commands section.

**Impact:** The design doc no longer lists the build interface, which 0001 §11.3 counts as a breaking-change surface from 1.0.

**Recommendation:** Add the missing rows, or replace the table with a pointer to `project_n --help` plus the rationale for each non-obvious command.

**Verification:** grep of 0001 finds no `measure`, `prune-worktrees` or `lfs-prune` row in the §8 table. `lfs-key` and `lfs-prune` appear only in the CI cache prose at line 660, and 0001.1 line 66 mentions `lfs-key`. So the table is incomplete, as claimed. I did not verify the other omitted commands or options individually.

### DESIGN-DOCS-09: 0001 §11.4 commit example uses a scope commit-check rejects

- **Severity:** low · **Verdict:** confirmed · **Category:** doc-error · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0001-repository-build-environment.md:635`

The example `feat(render): recreate swapchain on window resize` uses scope `render`, which is not in SCOPES (repo, env, containers, tools, xmake, deps, tests, design, agents, release, core, data, platform, gfx, game, app). `build(env): pin gcc 15.2 and clang 21.1` also cites versions that were never pinned (16.2 / 22.1.8).

**Evidence:** tools/project_n.py line 117 SCOPES.

**Impact:** Someone copying the example gets a rejected commit.

**Recommendation:** Use `feat(gfx): ...` and current versions, or label the block as illustrative only.

**Verification:** 0001 lines 631 and 635 hold `build(env): pin gcc 15.2 and clang 21.1` and `feat(render): ...`. SCOPES at tools/project_n.py line 117 has no `render`. The pins are 22.1.8 for LLVM. It is an example block and probably meant as illustrative, so impact is minimal.

### DESIGN-DOCS-10: 0001 §3 says libcxx is Linux-only; macOS links and ships conda's libc++

- **Severity:** low · **Verdict:** confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0001-repository-build-environment.md:77`

The pin table says '`libcxx` is used on Linux only', but pixi.toml pins libcxx/libcxx-devel for osx-arm64 too, and §5.7 and xmake.lua link `$CONDA_PREFIX/lib/libc++.1.dylib` and ship it in the app bundle.

**Evidence:** pixi.toml [target.osx-arm64.dependencies] libcxx = "==22.1.8"; xmake.lua lines 198–203, 227–232.

**Impact:** Misleads a toolchain bump (bump-toolchain skill) on macOS.

**Recommendation:** Say libcxx is used on Linux (static) and macOS (dylib, shipped in the bundle).

**Verification:** 0001 line 77 says libcxx is used on Linux only. pixi.toml lines 69-70 pin libcxx and libcxx-devel at ==22.1.8, and the finding cites the osx-arm64 target for these pins. I did not re-read the xmake.lua dylib link lines.
