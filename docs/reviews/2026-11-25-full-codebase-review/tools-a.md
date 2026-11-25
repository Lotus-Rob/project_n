# Review: tools-a

**Scope:** tools/project_n.py FIRST half by line count (read the whole file for context), tools/AGENTS.md. Python correctness, subprocess/env handling, error paths, portability (Windows/macOS), maintainability of a single huge script.

**Findings:** 26 standing (0 critical, 0 high, 0 medium, 16 low, 10 info), 1 raised but refuted on verification.

**Parts:** tools-a.md, tools-a-part2.md, tools-a-part3.md

## Assessment

## Assessment of tools/project_n.py (lines 1-1686) and tools/AGENTS.md

I read lines 1-1700 in full (lines 1-1686 are the first half, plus the start of `shaders_header`) and all of tools/AGENTS.md. I did not read the second half or `test_project_n.py`, so claims about how the main handler prints errors are from grep only.

**Strengths**
- The code is careful and the reasoning is recorded. Almost every non-obvious behaviour has a rationale and a measurement: the xmake.cmd `endlocal` exit-code trap, lockfile reordering, the cold rule for CI caches, the per-case reference processes and the clang-tidy cache keying.
- The cache key for clang-tidy hashes the preprocessed text plus every repo file named in its line markers, so NOLINT edits are not missed. It also covers the tidy version, its arguments and every `.clang-tidy`. Cache writes are atomic and corrupt entries are treated as misses.
- Lock restoring compares entries, not bytes, and still surfaces real additions.
- Outputs are never piped through filters, subprocess output is decoded as UTF-8 explicitly, and timeouts exist where hangs were seen (reference cases).
- Every lint helper is a pure function over a list of files, which makes it unit-testable.

**Weaknesses**
- The file is 3372 lines, and it is not "thin". It holds a Lua-table parser, a C/Lua/YAML/TOML/Python/batch comment tokenizer, include-graph lints, shader reflection and header generation, a clang-tidy cache and pool, cache pruning, a Tracy export parser and more. AGENTS.md calls it "thin", which is no longer true. Navigation, merge conflicts and import time all suffer, and a bug in one area breaks every command.
- The text-based lints (`unsized_types`, `platform_maths`, `bypassed_facades`) scan line by line with `code_of`. That function does not understand block comments or raw strings, so it gives false positives, and its blind spots give false negatives.
- Error handling is inconsistent. Many `subprocess.run` calls bypass `resolve`, and the main handler only catches `Fail` and `CalledProcessError`. A missing tool therefore ends in a Python traceback instead of a `doctor` hint.
- Several per-unit helpers repeat global work for every unit. These are `tidy_version`, `tidy_configs`, and parsing `compile_commands.json` once per unit.
- tools/AGENTS.md breaks the repo's own "~100 lines, one line per rule" guidance. It has 49 lines, but the Commands and Gotchas entries run to several hundred words each. They mix rules with dated measurements and history, and they restate the code's docstrings, so they will drift.
- Local `.tidy-cache` has no eviction.

**Overall health:** good for a tooling script, with careful engineering and a low bug density in the code I read. The risks are maintainability (one huge file, very long prose docs) and a handful of robustness gaps (the tidy verdict, `NOLINT(*)`, uncaught `OSError`, pin checks).

**Files read in full:** (not reported)

## Findings

### TOOLS-A-01: tidy_verdict passes a unit that has only ignored findings plus a bracketless compile error

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:1386`

`failed = len(ignored) < len(findings) or returncode not in (0,1) or (returncode == 1 and not findings)`. A unit whose output has at least one listed (ignored) Quill finding and also a compile error that carries no `[check]` suffix is judged passing. The `returncode == 1 and not findings` branch only fires when there are no findings at all. The docstring and AGENTS.md describe this as the reason `run-clang-tidy` was rejected, but the same hole exists per unit.

**Evidence:** tidy_verdict, line 1386; TIDY_FINDING_RE line 1353 requires a trailing `[...]`.

**Impact:** A unit that stops being parseable, for example a missing header reported as `fatal error:` or a clang-diagnostic without a bracket, can pass lint when it also touches the ignored Quill false positive. The result is then cached as a pass.

**Recommendation:** Count findings that have no label (or error lines not matched by the regex) and fail when returncode is 1 and any non-ignored `error:` line exists. Alternatively, fail when returncode is 1 and the number of `error:` lines exceeds the number of ignored findings.

**Verification:** The logic reading is right: tidy_verdict only fails on non-listed bracketed findings, bad return codes, or rc==1 with no findings. But clang-tidy labels compile errors, including `fatal error: file not found`, with `[clang-diagnostic-error]`, so TIDY_FINDING_RE matches them and they count as non-ignored findings. A truly bracketless error alongside an ignored Quill finding is mostly theoretical. Also, clang-tidy returns 1 on a compile error even if findings exist, but the label catches it. Hardening is cheap but impact is low.

### TOOLS-A-02: NOLINT(*) and NOLINT(-*) count as naming the checks

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** lint-policy · **Status:** new · **Reviewer confidence:** high
- **Where:** `tools/project_n.py:1019`

`nolint_explained` accepts any non-empty `(checks)` followed by `: <text>`. A blanket `// NOLINT(*): reason` or `NOLINT(-*)` satisfies it. Only the first NOLINT on a line is examined, and `NOLINTNEXTLINE` or `NOLINTBEGIN` bodies are not examined for scope.

**Evidence:** NOLINT_RE and nolint_explained, lines 1013-1019.

**Impact:** The AGENTS.md rule says to suppress as narrowly as possible, and a wildcard defeats that without any lint failure.

**Recommendation:** Reject `*` and a bare glob in the checks list, and iterate over all NOLINT matches on a line. Add a found and a not-found self-test.

**Verification:** NOLINT_RE takes any non-empty `[^)]*` as checks, so `NOLINT(*): why` passes nolint_explained. Only the first match per line is checked via search(). Nothing rejects wildcards. It is a policy gap, not a bug. Impact is low: a human or reviewer has to write it deliberately, and reviews check suppressions.

### TOOLS-A-03: Local clang-tidy cache is never pruned or aged

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:1401`

Locally, results go to `<build folder>/.tidy-cache`. The stale-tree sweep skips the native debug and release trees (WARM_LEAVES). `age_build_cache` only handles `.build_cache`, and `prune_tidy` runs only from the CI-only prune-build-cache command. Each source or flag change adds a new key, because the key hashes whole preprocessed text, and nothing deletes the old ones. Each result stores the full clang-tidy output.

**Evidence:** tidy_cache_dir line 1401, sweep_build lines 360-384, TIDY_RUN_MARK comment lines 1404-1408.

**Impact:** Unbounded disk growth in the tree that lint runs in (LINT_PRESET, for example linux-clang debug). The tree is never swept because it is warm, and the build already reached 57 GB once.

**Recommendation:** Age `.tidy-cache` in `age_build_cache` by mtime, since hits refresh it with `os.utime`. Alternatively, run `prune_tidy` with the local mark after each lint.

**Verification:** Locally the cache is ROOT/bdir/.tidy-cache (line 1401), and prune_tidy is only called from the CI prune-build-cache path (lines 495/538), so no local pruning exists. Growth is real but slow: it is one JSON of clang-tidy output per changed unit key (small text, mostly empty output), not objects. The 57 GB figure came from build trees, not this. The tree is a warm leaf, so the stale sweep skips it. Low severity. I did not check the age_build_cache body in detail.

### TOOLS-A-04: tidy_key repeats global work for every unit

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `tools/project_n.py:1503`

For each of about 141 units, `tidy_key` calls `tidy_version()` (a clang-tidy subprocess), `tidy_configs()` (a git subprocess plus reads of every `.clang-tidy`), and `compile_entries` (which parses the entire compile_commands.json from disk). The first two are identical for every unit, and the third is re-parsed per unit.

**Evidence:** tidy_key lines 1499-1503, compile_entries line 1424, tidy_version line 1452.

**Impact:** About 141 extra process spawns and 141 JSON parses of a large database, inside the roughly 12 s of key cost that the docs measure. The parses also run on the thread pool, so the GIL partly serialises them.

**Recommendation:** Compute the version, the configs digest and the parsed database once per lint run, with functools.cache or by passing them in.

**Verification:** tidy_key calls tidy_version() (capture subprocess, line 1503), tidy_configs() (git subprocess plus reads) and compile_entries (re-parses compile_commands.json, line 1424) for every unit, with no caching. The cost is a real inefficiency, but the docs measure the total (13 s warm for 141 units), so it is minor. The GIL point is plausible but unmeasured. Correct as a low-priority optimisation.

### TOOLS-A-05: Missing external tool gives a traceback, not a doctor hint

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:599`

`ensure_image` runs `subprocess.run(["podman", ...])` directly and does not go through `resolve`. `run` and `capture` fall back to the bare program name when `shutil.which` fails, which raises FileNotFoundError. The main handler (line 3362 area, seen only via grep) catches `Fail` and `CalledProcessError`. `build_all` also catches only those two, so a missing podman aborts `build --preset all` instead of marking one preset as failed. `pixi_prefix`'s `next(...)` raises a bare StopIteration if the environment is absent from `pixi info`.

**Evidence:** lines 599-604, 740-746, 869-870, 246-249.

**Impact:** A user without podman or clang-tidy sees a Python traceback and loses the `build --preset all` summary. The AGENTS.md rule says missing tools should lead to `doctor`'s instructions.

**Recommendation:** Convert FileNotFoundError from `resolve`/`run`/`capture` into `Fail("<tool> not found; run doctor")`, and route all direct `subprocess.run` calls through one helper. Catch OSError in `build_all`.

**Verification:** resolve() (line 148) falls back to the bare name, so a missing tool raises FileNotFoundError. main() (3362-3368) and build_all (743-746) catch only Fail and CalledProcessError, so the user gets a traceback, and a missing podman would abort build --preset all. ensure_image uses subprocess.run directly. But build_all only runs presets buildable here, and podman is only needed for container presets, so the case is narrow. pixi_prefix's next() StopIteration (870) is real but needs a pixi env that `install -e` just created, so it is very unlikely. Impact is a poor error message, not wrong behaviour.

### TOOLS-A-06: build() rewrites build/compile_commands.json for any native preset, mode or toolchain

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:710`

Every native build with `sanitizer == "none"` re-exports `build/compile_commands.json` from that build's configuration. Building linux-gcc release, or windows-msvc natively, therefore points clangd and the `LSP` tool at that preset's flags (GCC flags, a different PCH). A releasedbg build can do the same.

**Evidence:** build(), lines 707-713; .claude/CLAUDE.md says the LSP tool reads build/compile_commands.json.

**Impact:** clangd diagnostics and clang-tidy-with-`-p build` flip depending on the last build. AGENTS.md says lint reads `ROOT/bdir/compile_commands.json`, so lint is unaffected, but editor and LSP results change.

**Recommendation:** Export the root compile database only for the preset or mode that the clangd config expects (LINT_PRESET debug), or document which build wins.

**Verification:** Confirmed in code at 707-713: any native build with sanitizer none re-exports build/compile_commands.json from that build's configuration, so the last build wins for clangd or LSP. The comment says it is deliberate ("Native builds keep clangd current"). The finding is right that gcc, release and releasedbg builds could point clangd at different flags. Lint uses its own bdir, as the finding concedes. Low-severity design wart, partly deliberate.

### TOOLS-A-07: Text lints use code_of, which ignores block comments and raw strings

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:1332`

`code_of` blanks only ordinary string literals and cuts at `//`. Doxygen block lines (` * a long value`), `/* ... */` comments, multi-line raw strings and char literals containing a quote are scanned as code by `unsized_types`, `platform_maths` and `bypassed_facades`. Words such as `int`, `long`, `float`, `double`, `std::cout`, `printf(` and `log(` in prose are false positives. The reverse also holds: code after a `"` inside a char literal is blanked.

**Evidence:** code_of lines 1332-1334; callers at 1267, 1323, 1347.

**Impact:** Spurious lint failures that push authors toward rewording comments. The unsized-types rule also ignores `'` and raw strings, so a missed violation is possible.

**Recommendation:** Reuse `c_comments`, or a small shared lexer that strips comments and literals once per file, for all line-based lints. Test block-comment and raw-string samples.

**Verification:** code_of (l.1332) does only strip "..." and cut at //, and c_comments (l.1088) exists. But the project's Doxygen is `///` (math.hpp), so block comments and ` * ` lines are rare; raw strings and char-literal quotes are real but unproven gaps. Mechanism is true, impact is small.

### TOOLS-A-08: Shader tool pin check cannot see a tool pinned on only one platform

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:1641`

`shader_tool_pins` collects the set of `tool = "==x"` lines across pixi.toml. A tool pinned only under linux-64, or not pinned at all (the result is `["unpinned"]`), gives a one-element list. `unequal_shader_pins` flags only multi-element lists, so "unpinned on Windows" and "entirely unpinned" pass. Table-form dependencies (`tool = { version = "==x" }`) are invisible to the regex and are reported as unpinned.

**Evidence:** lines 1641-1651.

**Impact:** Linux and Windows could build different shader binaries with no lint failure, and the manifest would record `# tool ==unpinned`.

**Recommendation:** Parse pixi.toml with `tomllib`, which is already imported. Require a pin in both platform tables, and fail on unpinned.

**Verification:** Logic as described holds: pixi.toml lines 35-37 and 63-65 pin all three tools on both win-64 and linux-64, so today it passes correctly. A tool pinned on only one platform, or on none, gives a single-element list and is not flagged (l.1641-1651). The gap is hypothetical, and the regex also matches pins in any table.

### TOOLS-A-09: widens_scope omits several inputs that change every file's lint result

- **Severity:** low · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:1194`

Only `.clang-*`, `xmake.lua` (at any depth), `pixi.lock`, `tools/project_n.py`, `docs/Doxyfile` and `xmake/repo/` widen the scope. A change to `pixi.toml` that touches activation environment or flags without a lock change, to `src/pch.hpp` or `tests/unit/pch.hpp`, or to `.editorconfig`, does not. The pre-commit hook and `check` therefore lint and format only the diff.

**Evidence:** lines 1194-1198.

**Impact:** Mostly covered by tidy's dependency tracking, since a changed PCH is named in `.deps`. fmt has no equivalent, and a `.clang-format`-adjacent or pixi.toml-only flag change can leave stale violations until `--all` or CI.

**Recommendation:** Add `pixi.toml` and the PCH headers, or document why they are excluded.

**Verification:** widens_scope (l.1194-1198) lists exactly those paths. pixi.toml, the PCH headers and .editorconfig are absent. Tidy's .deps tracking covers the PCH case, and CI lints with --all, so impact is small. The comment mentions 'the tool versions' (pixi.lock), which suggests a deliberate choice.

### TOOLS-A-10: git path lists are read without -z, so non-ASCII paths are quoted and mismatch

- **Severity:** low · **Verdict:** confirmed · **Category:** portability · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:1216`

`git diff --name-only`, `ls-files --others` and `ls-files --cached --others` are parsed with `.splitlines()`. With the default `core.quotepath`, a path with non-ASCII or special characters is emitted quoted and octal-escaped, and it no longer matches `source_files()`. `tidy_configs` already uses `-z`.

**Evidence:** lines 1186, 1216-1217; contrast tidy_configs line 1457.

**Impact:** A changed file with such a name is silently skipped by fmt, lint and design_reference_findings, and a path with a space or quote is also affected. Currently theoretical, since repo names are ASCII.

**Recommendation:** Use `-z` and split on NUL, or pass `-c core.quotepath=false`.

**Verification:** l.1216-1217 and l.1186 use splitlines on git output without -z or core.quotepath=false. Non-ASCII paths would be quoted and missed. Repo paths are ASCII, so it is theoretical today.
