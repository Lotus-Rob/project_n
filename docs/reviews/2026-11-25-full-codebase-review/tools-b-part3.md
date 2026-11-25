# Review: tools-b (part 3)

### TOOLS-B-21: Determinism lint covers only libm calls, with a few missing names

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** determinism-tooling · **Status:** contradicts-recorded-decision · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:1314`

PLATFORM_MATHS_RE bans transcendental libm calls in src/core and src/game. It omits sincos and exp10. It does not look at `<random>` distributions, whose output is implementation-defined, or at other implementation-dependent behaviour (unspecified-order containers).

**Impact:** Given the recorded decision (replays deterministic only within one binary, no cross-platform goal) this is acceptable. Cross-stdlib-dependent constructs would still desync a replay if the project ever wants cross-platform replays.

**Recommendation:** Either document that the lint is a partial guard, or add `std::*_distribution` and `std::shuffle` to the banned list for core and game.

**Verification:** PLATFORM_MATHS_RE (line 1314) indeed omits sincos and exp10 and has no <random> or shuffle rules. docs/design/0002-engine-architecture.md section 7.4 records determinism as 'same binary, any machine', so cross-stdlib gaps are not a defect. The finding concedes this. Only a doc note on partial coverage is worth adding.

### TOOLS-B-22: Any pixi.toml edit, even a comment, invalidates the package cache

- **Severity:** info · **Verdict:** deliberate · **Category:** build-cost · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:550`

packages_dir hashes the whole pixi.toml and pixi.lock, the same pair that decides the container image tag and the clean --packages logic. The comment justifies it by activation variables outside the lock. No finer key is used.

**Evidence:** packages_dir (554) and IMAGE_INPUTS (101)

**Impact:** A harmless pixi.toml edit forces a rebuild of every XMake package and a new container image.

**Recommendation:** Hash only the parts that affect builds (dependencies, activation, tasks), or accept this and say so in the tools AGENTS.md Gotchas.

**Verification:** packages_dir docstring and design doc 0001 rule 11 explicitly say pixi.toml is hashed because activation variables are not in the lock. Whole-file hashing is the recorded choice. A comment edit does cause a rebuild, but that is a cost they accept.

### TOOLS-B-23: No-upstream main silently skips commit-check in `check`

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** gate-gap · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:2257`

`commit_range()` returns None on main without an upstream and without PROJECT_N_COMMIT_RANGE. check_steps then skips commit-check entirely (`if rng and ...`), with no message.

**Evidence:** lines 2193-2202 and 2257-2258

**Impact:** A fresh clone or a main branch with no upstream passes `check` without validating messages.

**Recommendation:** Print a skip line, or fall back to the last tag or a bounded range.

**Verification:** commit_range returns None when HEAD is main with no upstream and no PROJECT_N_COMMIT_RANGE, and check_steps (2257) skips commit-check silently. Main normally has an upstream and CI sets the range, and the branch ruleset blocks direct pushes to main, so real exposure is tiny. A printed skip line would still be cheap.

### TOOLS-B-24: DXIL from Linux dxc may be unsigned; Linux- and Windows-built binaries may differ

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** build-reproducibility · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:1732`

The shaders step compiles DXIL with the conda dxc on either host. D3D12 loads only signed DXIL, and signing normally needs dxil.dll, which a Linux build lacks. The same pinned tool on Windows may sign and so embed different bytes. The manifest and stale-shader check rest on the claim that equal pins give equal binaries.

**Evidence:** line 1732 `run([*dxc, "-Wno-ignored-attributes", "-Fo", f"{base}.dxil"])`; unequal_shader_pins only compares versions

**Impact:** If outputs differ per host, running `shaders` on the other OS leaves a diff, or committed DXIL is rejected by D3D12 drivers.

**Recommendation:** Verify on the Windows path that the committed .dxil files equal the Linux ones, and record the result in the tools notes. If they differ, add a DXIL signing and validation step.

**Verification:** docs/design/0002-engine-architecture.md S8 (line 545) records that DXC signs its DXIL on Linux, which covers the unsigned-DXIL worry. The unequal_shader_pins check only compares versions, as stated. Whether Windows-built bytes equal Linux-built bytes is not recorded anywhere, and reading the code can't settle it. Running `shaders` on Windows and checking `git diff` would. The D3D12 signing risk is mostly refuted.

### TOOLS-B-25: Self-tests include slow art-generator tests and read live repo constants

- **Severity:** info (reviewer said low) · **Verdict:** unverifiable · **Category:** test-architecture · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/test_project_n.py:2702`

`selftest` runs PlaceholderTest, which paints every stamp, mist and painting sprite in a process pool, and WorldMapRoadsTest, which runs world_map.main() and routes roads. It also loads art/*.py by path and registers them in sys.modules.

Other tests read live repo files: MEASURED_SHARE in tests/unit/game/shipped_world_tests.cpp, MEASURED in camera_path_tests.cpp, tests/xmake.lua, docs/Doxyfile and assets/data/world.yaml.

The tools AGENTS scope does not mention art tests.

**Evidence:** PlaceholderTest (2702-2724); WorldMapRoadsTest (2745-2953); lines 1525 and 1532

**Impact:** `selftest` is the first step of every `check`, so these slow tests delay every gate. A rename in an unrelated C++ test or a Doxyfile edit breaks tool tests.

**Recommendation:** Move the art tests to art/ with their own discoverable test module, which `selftest` can still run. Tie the C++ constants to the tools via one machine-readable source.

**Verification:** This is a test-architecture opinion. I did not run selftest, so I can't judge whether the slowness is real. The facts it rests on are plausible: tools/test_project_n.py has PlaceholderTest and WorldMapRoadsTest, and AGENTS.md lines 22 and 29 show tests deliberately tying C++ constants such as MEASURED to the tool ('self-tests check both'). Those cross-checks are intentional guards against drift, so the 'brittle' framing is weak. Splitting the art tests out is a legitimate option. Timing the tests would decide it.

### TOOLS-B-26: Functions with no test coverage

- **Severity:** info (reviewer said low) · **Verdict:** unverifiable · **Category:** test-gap · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/test_project_n.py:1`

No test touches these functions:
- cmd_doctor, cmd_clean's images and packages paths, ensure_image, ensure_msvc, ensure_macos_xmake.
- shaders_header, and cmd_shaders' success path.
- cmd_commit_check --range, cmd_check's sanitizer loop, cmd_lint's ordering and gating.
- lock_entries on malformed or duplicate keys, lfs_listing malformed lines.
- The hash_comments shell paths.
- Atlas manifest YAML injection.

**Evidence:** grep of test names against function list

**Impact:** Regressions in release and setup paths are only caught by running them on CI or by hand.

**Recommendation:** Add focused tests with mocked subprocess for the doctor/clean/ensure_* paths, and a golden-file test for shaders_header.

**Verification:** I only grepped test names. There is no test with doctor or shaders_header in its name. I did not do a function-by-function audit, so I can't confirm the full list. Several named functions need a container, Wine or network, which makes mocking low-value. The project's gates (check, check --container, CI) exercise them end to end. A golden-file test for shaders_header would be a reasonable addition. Low value overall.

### TOOLS-B-27: prune_worktrees ignores ignored files and treats an ancestor branch as 'no commits yet'

- **Severity:** info · **Verdict:** confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:3169`

`git status --porcelain` omits ignored files. `git worktree remove` then deletes them (session ledger, handover notes, build outputs). A branch whose tip is an ancestor of origin/main gives an empty `git cherry` and is reported as 'no commits of its own yet', and is kept forever.

**Evidence:** worktree_unremovable (3178) and branch_unmerged (3188-3190)

**Impact:** Low. Ignored files are by definition disposable, but the ledger is cited as something to copy out before removal. The 'no commits' state is a safe keep but can never be pruned automatically.

**Recommendation:** Mention ignored session files when removing, or check `git status --ignored` for `.superpowers`. Distinguish an ancestor branch (safe to delete) from a never-committed one.

**Verification:** Checked tools/project_n.py 3169-3190. `git status --porcelain` omits ignored files. An ancestor branch gives an empty `git cherry`, so it is reported as 'no commits of its own yet' and kept. The code comment says this is deliberate, to protect a just-made worktree. The finding itself concedes this is a safe keep, so the impact is negligible.

### TOOLS-B-28: check_staged_format runs clang-format serially, one subprocess pair per file

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:2089`

When .clang-format or pixi.lock is staged, every indexed source is checked in a loop with `git show` followed by clang-format. fmt over a few hundred files can exceed the Windows command-line limit in `cmd_fmt --all` and `tidy_fix`, which pass every path as an argument.

**Evidence:** lines 2097-2102; 2114; 1535

**Impact:** A slow commit in the rare case of a format-version bump. Possible command-line length failure on Windows as the tree grows.

**Recommendation:** Use the thread pool for the staged check. Chunk the file lists passed to clang-format and clang-tidy --fix.

**Verification:** The staged format check does run serially, but only when .clang-format or pixi.lock is staged, which is rare. The Windows command-line limit claim is speculative: Windows allows about 32k characters and the finding gives no measurement of the file lists. Not an actual current problem.

### TOOLS-B-29: artifact zips are not reproducible and the Windows/macOS dependency check is partial

- **Severity:** info · **Verdict:** confirmed · **Category:** release · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:2402`

`shutil.make_archive` embeds file mtimes and uses directory iteration order. The manifest hashes the files but not the zips. The Linux leak check only greps `ldd` for '.pixi'.

**Evidence:** lines 2401-2403

**Impact:** Two artefact builds of one commit do not yield byte-identical zips, which weakens the manifest's reproducibility story. Low risk otherwise.

**Recommendation:** If reproducibility is a goal, build the zip with fixed timestamps and sorted entries, or hash the zip instead.

**Verification:** Lines 2401-2403 use shutil.make_archive, which embeds mtimes and does not sort entries. Nothing in the code records reproducibility as a goal. Cosmetic.

### TOOLS-B-30: cliff.toml puts every conventional type into the player-facing changelog

- **Severity:** info · **Verdict:** unverifiable · **Category:** release-notes · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/cliff.toml:27`

All types (test, docs, ci, build, style, chore, refactor) map to groups, so CHANGELOG.md and the GitHub release notes (via release_notes) list internal commits. The release notes carry a player-facing note about signing.

**Evidence:** commit_parsers lines 27-38; release_notes (2960-2965)

**Impact:** Noisy notes for players. A pure tooling release still bumps the patch version.

**Recommendation:** Keep detailed groups in CHANGELOG.md but publish only Features, Bug fixes and Performance in the release notes, or skip internal groups in cliff and keep them in a separate file.

**Verification:** I did not open cliff.toml or the release notes code. The question of whether internal commit types should appear in the changelog is a policy choice. Deciding it needs the project's release policy, which I did not read.
