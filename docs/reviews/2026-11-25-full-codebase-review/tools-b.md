# Review: tools-b

**Scope:** tools/project_n.py SECOND half by line count (read the whole file for context), tools/test_project_n.py, tools/env, tools/debug, tools/cliff.toml.

**Findings:** 31 standing (0 critical, 0 high, 0 medium, 17 low, 14 info), 0 raised but refuted on verification.

**Parts:** tools-b.md, tools-b-part2.md, tools-b-part3.md, tools-b-part4.md

## Assessment

## Scope
I read all of tools/project_n.py (3372 lines; the second half is roughly line 1686 onward, from the shader and atlas steps to `main`), all of tools/test_project_n.py (3740 lines), tools/cliff.toml, tools/env/activate.sh and tools/debug/gdbinit. I also read the root and tools AGENTS.md. Nothing was run, as instructed.

## Strengths
- The tool is careful about failure modes that are easy to miss.
  - `restore_lock_unless_added` handles XMake's habit of rewriting the lock file.
  - The cold-rule logic is explicit (builds of main and tags compile from source).
  - The reference runner times out each case and names the case that failed.
  - `tidy_key` hashes the preprocessed text plus every repo file named in its line markers, so a NOLINT edit is a cache miss.
  - Prune and sweep skip files they cannot remove rather than aborting.
  - The worktree prune is conservative: it never forces, and it reports why it keeps something.
- The test suite is large and mostly good. Most tests are behavioural and use real temporary git repos. Several guard past incidents: Windows separators in test file patterns, the Lua copy of the test-file pattern, a 403 part-way through cache deletes, and a 404 on a delete race. The design-reference lint has found and not-found samples for each pattern.
- Mechanical lint rules (module direction, facades, sized types, no libm in the simulation, no standard includes in .cpp files) are cheap to run and give a good signal.
- cliff.toml is small and correct for the stated pre-1.0 bump policy.

## Weaknesses and risks
- **Size and layering.** project_n.py is now a 3.4k-line module that mixes build orchestration, container and MSVC management, a regex-based custom linter, a Tracy analytics pipeline, release and publish, the shader compiler, the atlas packer, worktree pruning and CI cache management. The tools rule says "one file ... thin by design", and it is not thin any more. The self-test file has the same problem: it is 3.7k lines and includes tests of art/placeholders.py and art/world_map.py.
- **Lint ergonomics.** `cmd_lint` raises on the first failing category, so one lint round trip fixes one class of problem at a time. The hand-written regex linters have known blind spots: block comments, raw strings, char literals, Lua long comments, and `NOLINT(*)`.
- **Per-unit overhead in `tidy_key`.** It re-parses the compile database and spawns `clang-tidy --version` and `git ls-files` for every unit.
- **Environment drift.** The environment pins Python 3.14, but `doctor` still checks for 3.11. The code needs 3.13 or later.
- **Test coverage gaps.** These functions and branches have no test:
  - `cmd_doctor`, `cmd_clean` with `--images` or `--packages`, `ensure_image`, `ensure_msvc` and `ensure_macos_xmake`.
  - `shaders_header`, and the happy path of `cmd_shaders`.
  - `cmd_commit_check --range`, and `cmd_check`'s sanitizer loop.
  - The raw-string, digit-separator and char-literal branches of `c_comments`.
- **Hermeticity.** Several self-tests read live repo files: C++ constants in tests/unit, tests/xmake.lua, world.yaml and the Doxyfile. They break on unrelated renames, but they are also what catches drift.

## Overall health
Good for a build tool with this many responsibilities. I found no crashing bug in normal use. What I found is mostly latent: stale thresholds, a few cross-worktree hazards (shared `~/.xmake/project_n` package dirs, shared `~/.xmake/project_n-tools` MSVC install), unvalidated inputs in generators, and maintainability debt in the single file.

No finding touches determinism of the simulation. The only related point is that the determinism lint covers libm only, which is consistent with the recorded in-one-binary decision.

**Files read in full:** (not reported)

## Findings

### TOOLS-B-01: doctor's Python check is stale; the tool needs 3.13+ (3.14 is pinned)

- **Severity:** low · **Verdict:** confirmed · **Category:** docs-vs-code · **Status:** new · **Reviewer confidence:** high
- **Where:** `tools/project_n.py:3076`

cmd_doctor reports `sys.version_info >= (3, 11)` as the requirement. The code uses `os.process_cpu_count()` (3.13+, line 1377) and a nested same-quote f-string at line 1590 (3.12+). pixi.toml pins python ==3.14.10.

**Evidence:** line 3076 `report(sys.version_info >= (3, 11), ...)`; line 1377 `os.process_cpu_count()`; pixi.toml:12 `python = "==3.14.10"`

**Impact:** A host or container with Python 3.11 or 3.12 passes `doctor` and then crashes with AttributeError or a SyntaxError. The check misleads and checks nothing the pin does not already guarantee.

**Recommendation:** Make the threshold match the real floor (3.13), or derive it from pixi.toml. Add a self-test tying the number to `jobs()`.

**Verification:** Line 3076 checks >= 3.11 and jobs() at 1377 uses os.process_cpu_count (3.13+). The nested same-quote f-string at 1590 is real (3.12+). The check is stale. Impact is small because Pixi pins Python.

### TOOLS-B-02: tidy_key redoes invariant work for every unit

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `tools/project_n.py:1492`

Each unit's key calls compile_entries, which re-reads and parses the whole compile_commands.json. It also calls tidy_version(), a `clang-tidy --version` subprocess, and tidy_configs(), a `git ls-files` subprocess. This runs once per unit (~141) inside the thread pool. Regex scanning of ~5 MB of preprocessed text per unit holds the GIL.

**Evidence:** tidy_key (1499-1503) calls `compile_entries(bdir, unit)`, `tidy_version()` and `tidy_configs()` on each call.

**Impact:** The documented ~12 s key cost has an avoidable part: ~280 process spawns and ~141 JSON parses of a multi-MB file. It grows linearly with unit count.

**Recommendation:** Cache the compile database, the tidy version and the config bytes once per lint run (`functools.cache`, or pass them in). Parse the compile database once and index it by file.

**Verification:** tidy_key calls compile_entries, which re-reads and parses the whole JSON on every call. It also calls tidy_version(), which runs a subprocess, and tidy_configs(), which runs git ls-files, with no caching. Both subprocess calls are plain functions with no functools.cache. The cost is real but modest next to the preprocessing.

### TOOLS-B-03: lint stops at the first failing category

- **Severity:** low · **Verdict:** confirmed · **Category:** ergonomics · **Status:** new · **Reviewer confidence:** high
- **Where:** `tools/project_n.py:2117`

cmd_lint raises Fail at each of design references, suppressions, leaked deps, module crossings, standard includes, sized types, facades, platform maths, doc comments, shader pins, stale shaders, stale atlas and LFS pointers. Only the first failing category is reported. Clang-tidy runs only after all of them pass.

**Evidence:** lines 2123-2163, one `raise Fail` after each check

**Impact:** A branch with several kinds of finding needs one full lint round trip per kind. The cheap checks could all be reported together before the expensive build.

**Recommendation:** Collect the cheap checks' findings into one list and raise once. Optionally run them before and independently of the build.

**Verification:** cmd_lint raises Fail right after the first non-empty check (design_reference_findings at 2123-2125, and the same shape for the others). This is an ergonomics choice with no recorded decision found. I did not check the later checks line by line.

### TOOLS-B-04: Regex linters have unhandled lexical cases and no tests for them

- **Severity:** low · **Verdict:** unverifiable · **Category:** lint-gaps · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:1332`

`code_of` strips only "..." strings and `//` comments.
- `unsized_types`, `platform_maths` and `bypassed_facades` therefore flag words inside `/* */` or ` * ` block-comment lines and raw strings.
- A char literal `'"'` opens a bogus string.
- `c_comments` treats a prefixed char literal such as u8'"' as a digit separator.
- `c_comments` with line_marker "--" ignores Lua `--[[ ]]` and `[[ ]]` long strings, so design references inside them are missed and quotes can desync.

The docstring promises C++ raw strings and digit separators, but the tests have no sample for raw strings, digit separators or char literals, and none for Lua long comments.

**Evidence:** code_of (1332-1334); c_comments (1088-1124); no 'R"' or '--[[' sample in test_project_n.py

**Impact:** False positives and negatives in the gating lint, all as yet unseen in the repo. The raw-string and digit-separator branches are untested code.

**Recommendation:** Add tests for these branches, then handle block comments in `code_of` or run those checks on the comment-stripped text from `c_comments`. Handle Lua long brackets, or document the gap.

**Verification:** I did not read code_of or c_comments, so the lexical claims are unchecked. I did grep test_project_n.py for raw-string and digit-separator samples and found none, which is consistent with the finding. I also found no recorded decision.

### TOOLS-B-05: Suppression check misses wildcard NOLINT, MSVC warning pragmas that suppress, and non-C++ suppressions

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** lint-gaps · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:1013`

`nolint_explained` accepts `NOLINT(*)` with a reason. PRAGMA_RE flags `#pragma warning(disable` but not `#pragma warning(push, 0)` or `#pragma warning(suppress: ...)`. Only C++ sources are scanned: `# noqa`, `# type: ignore` in Python and compiler `-Wno-` flags in xmake.lua are not.

**Evidence:** NOLINT_RE and PRAGMA_RE (1013-1014); nolint_explained (1017-1019)

**Impact:** The rule says suppressions must be narrow and explained. A wildcard can suppress everything on a line and still pass.

**Recommendation:** Reject `NOLINT(*)`, widen PRAGMA_RE to the MSVC forms, and consider checking `# noqa` in tools and art.

**Verification:** NOLINT_RE and PRAGMA_RE are as described. nolint_explained accepts any non-empty checks group followed by a reason, so NOLINT(*): why passes. PRAGMA_RE matches only disable, not push,0 or suppress. Whether the scan covers non-C++ files was not checked. The wildcard with a stated reason arguably meets the rule (narrow and explained), so impact is minor.

### TOOLS-B-06: unit selection ignores changed generated headers

- **Severity:** low · **Verdict:** confirmed · **Category:** lint-coverage · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:1939`

`lint_units` selects units whose dependency record names a file in `sources`. `sources` excludes headers under /generated/ (`is_source`). A regenerated src/core/generated/atlas.hpp (SpriteId enum) or src/gfx/generated/shaders.hpp does not select the units that include it, unless their own files changed.

**Evidence:** lint_units(sources, dependencies) with `changed = set(sources)`; is_source excludes generated headers (line 1001)

**Impact:** A change to the generated atlas header, for example removing a sprite, can leave includers un-linted until a full lint. Their build failures are caught, but new clang-tidy findings are not.

**Recommendation:** Feed changed generated files into the dependency match (use `changed`, not `sources`), while still never linting the generated file itself.

**Verification:** scoped_sources returns [f for f in source_files() if f in changed], and source_files uses is_source, which excludes /generated/. lint_units(sources, ...) therefore never sees a changed generated header. Includers are not selected unless their own files changed. The impact is small: generated headers are excluded from lint by design, and a build error would still surface.

### TOOLS-B-07: clean --packages deletes package caches of other worktrees and branches

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** data-loss-risk · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:3141`

`packages_dir()` keys XMake's packages on a hash of pixi.toml and pixi.lock under the shared `~/.xmake/project_n/`. `cmd_clean --packages` removes every sibling of the current hash. Worktrees on different branches, such as a branch bumping a pin, routinely have different hashes. It also calls `shutil.rmtree(d)` on any entry of `~/.xmake/project_n`, so a stray file raises NotADirectoryError.

**Evidence:** lines 3141-3146

**Impact:** Cleaning in one checkout wipes the multi-hour package builds another live branch needs. `check` on that branch then rebuilds everything.

**Recommendation:** Only delete hashes no worktree's pixi.toml and pixi.lock produce (compute them from `git worktree list`), or add an age cutoff. Tolerate non-directories.

**Verification:** cmd_clean --packages (3141-3146) does rmtree every sibling hash dir, and a stray file would raise NotADirectoryError. But design doc 0001 rule 11 and the table at line 428 document it as 'removes package directories for locks other than the current one', and it is an explicit opt-in flag. Removing other worktrees' caches is therefore the documented behaviour. The only new points are the missing worktree awareness and the non-directory crash. Medium overstates it.

### TOOLS-B-08: publish uploads whatever zips are in dist/, without checking they belong to the tagged commit

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** release-safety · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:2968`

`release_archives` only checks that the 2x4 expected file names exist. `cmd_publish` then runs `gh release create` with them. Nothing compares the zips' embedded manifest.json (commit, version) with HEAD.

**Evidence:** release_archives (2968-2974); cmd_publish (2977-2986)

**Impact:** When run locally, a leftover dist/ from an earlier dev build of the same version number is published under the tag, with the wrong binaries. CI normally downloads fresh artifacts, which hides the risk.

**Recommendation:** Read dist/<preset>/manifest.json (or the zip's) and require `commit == HEAD` and `version == read_version()` before publishing.

**Verification:** release_archives only checks that the expected file names exist in dist/. cmd_publish checks HEAD is tagged but not the zips' contents. A stale local dist/ of the same version would be uploaded. The upload is --draft, which gives the maintainer a review step, and CI normally supplies fresh artifacts. Medium is too high.

### TOOLS-B-09: release leaves a modified branch behind on failure and has no already-tagged or no-change guard

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:2911`

After `git switch -c release/vX` and rewriting xmake.lua, a failure in git-cliff or `git commit` leaves the user on the new branch with an edited working tree and no cleanup. `release/vX` then blocks the next attempt. `--bumped-version` is never checked against existing tags or against `read_version()`. With no releasable commits it can name the current version.

**Evidence:** lines 2921-2931

**Impact:** Manual cleanup after a failed release, and possible release commits with an unchanged version.

**Recommendation:** Wrap the mutating steps in try/except that switches back and deletes the branch. Refuse when `v{version}` already exists as a tag.

**Verification:** cmd_release does git switch -c, then writes xmake.lua, runs cliff and commits, with no rollback. A failure leaves you on release/vX with a dirty tree, and the existing-branch check Fail message tells the user to delete it. The no-change case is not shown: --bumped-version comes from cliff, and I found no tag-exists check. tag_release does catch a mismatch later, and the release lands via PR review. It is a minor robustness gap.

### TOOLS-B-10: commit-check gaps: type/scope rule for game code unenforced, `#` lines stripped from stored messages, footers ignored

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** rules-enforcement · **Status:** new · **Reviewer confidence:** high
- **Where:** `tools/project_n.py:3009`

1. check_message drops every line starting with '#', which git does only for the editor message, not for stored messages checked in `--range` mode. A body line `#123` or a markdown heading is hidden from the checks.
2. The AGENTS rule that `feat`/`fix` are only for game code is not checked; any scope is accepted with any type.
3. `BREAKING CHANGE:` footers are not validated against the `!` marker, though cliff's bump policy depends on them.

**Evidence:** line 3011 `lines = [line for line in message.splitlines() if not line.startswith("#")]`; no type-scope cross check in 3026-3029

**Impact:** The merge gate accepts messages that break the stated conventions. A `feat(tools)` would add a changelog 'Features' entry and bump the minor version for tooling.

**Recommendation:** Apply the comment stripping only for the message-file (hook) path. Enforce feat/fix only with game scopes (core, data, platform, gfx, game, app), or document the rule as advisory.

**Verification:** Line 3011 does strip '#' lines for every message, including stored messages in --range mode, so a '#123' body line is hidden. It only matters for the checks that run on later lines, which are just the blank-line-after-header check. check_message has no type/scope cross-check for feat/fix, and no BREAKING CHANGE footer validation. Point 2 holds only as an unenforced AGENTS rule. Cosmetic overall.
