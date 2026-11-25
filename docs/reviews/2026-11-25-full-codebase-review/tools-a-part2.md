# Review: tools-a (part 2)

### TOOLS-A-11: source_files() scans the filesystem, not git, so ignored or stray files are linted

- **Severity:** low · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:1004`

`source_files` does `rglob` over src/ and tests/, while design_reference_findings uses `git ls-files`. A stray `.cpp` or `.hpp` (an editor backup, a gitignored scratch file) is formatted and linted. The tests/ rglob also walks the fuzz corpora on every call.

**Evidence:** lines 1004-1006 vs 1186.

**Impact:** False failures from untracked scratch files, and a wasted directory walk. The two lint families disagree about what the repo's files are.

**Recommendation:** Take the list from `git ls-files --cached --others --exclude-standard`, as the design-reference lint does.

**Verification:** source_files (l.1004-1006) uses rglob over src and tests. design_reference_findings uses git ls-files (l.1186). A stray untracked .cpp/.hpp would be formatted and linted. It also walks the fuzz corpora. Minor and local-only, since CI has clean clones.

### TOOLS-A-12: tests/xmake.lua is read without an encoding and by regex

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** portability · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:758`

`test_names` and `test_programs` call `read_text()` with the locale encoding, while the rest of the file passes `encoding="utf-8"`. On Windows (cp1252), a non-ASCII character in tests/xmake.lua breaks `--help` and `--list-tests`. Both functions also re-read and re-glob the file on every call, and `check_test_target` calls them several times per invocation. The regex handles only one-line `unit_tests(...)` calls, so a reformatted multi-line call silently drops a suite from `--test`.

**Evidence:** lines 758, 776.

**Impact:** A Windows-only crash is possible, along with small repeated I/O. A dropped suite fails loudly later (`no test suite`), which is acceptable.

**Recommendation:** Add `encoding="utf-8"`, cache the parse with functools.cache, and consider failing when `unit_tests(` occurs more often than the regex matches.

**Verification:** read_text() without encoding is true at l.758 and l.776, which would break on cp1252 only with non-ASCII in tests/xmake.lua. The one-line regex limit is explicitly commented at l.757 as a known limitation, so that part is deliberate. Repeated reads are real but trivial.

### TOOLS-A-13: tools/project_n.py is not thin; the single-file rule is straining

- **Severity:** low (reviewer said medium) · **Verdict:** deliberate · **Category:** maintainability · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/AGENTS.md:7`

AGENTS.md says the file is "thin: commands call XMake, Podman, git ...; anything those tools do stays there". In practice, 3372 lines hold a Lua-table parser, a multi-language comment tokenizer, include-graph and facade lints, shader reflection and codegen, a content-addressed clang-tidy cache, cache pruning, a Tracy CSV parser and a measure table. The 'standard library only' constraint does not require one file. `from_import`-style splitting into a `tools/` package (`python -m`, or the file importing siblings) would keep a single entry point.

**Evidence:** wc -l: 3372 lines. Sections: lines 166-231 (lock parser), 1068-1180 (comment lexers), 1418-1573 (tidy cache), 1594+ (shaders).

**Impact:** Slow navigation, large diffs, and a syntax error anywhere breaks every command. `tools/project_n.py` is also in `widens_scope`, so any edit to it re-lints and re-formats the whole repo. Import-time work and module globals such as `swept` are shared mutable state.

**Recommendation:** Keep `project_n.py` as the entry point and move the lint rules, tidy cache, shader tooling and measure into sibling modules under `tools/`. Update the 'one file' rule and `widens_scope` to cover the package.

**Verification:** The file is 3372 lines, but 'Single file, stdlib only' is a recorded design decision: tools/AGENTS.md rule 1 and docs/design/0001-repository-build-environment.md line 404. Splitting is a valid maintainability opinion, but it contradicts a documented rule. The 'thin' claim is also overstated: the AGENTS wording is about delegating to tools, not about size. The widens_scope remark is real, since widens_scope exists at line 1194.

### TOOLS-A-14: Reference compare ignores drawn pictures that have no committed reference, and the orphan case is only checked under --update

- **Severity:** low · **Verdict:** confirmed · **Category:** correctness · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `tools/project_n.py:973`

The 'changed' scan skips a drawn PNG with no committed counterpart. The reverse check, committed PNGs that were not drawn, runs only under `--update` (lines 965-967).

**Evidence:** lines 963-979.

**Impact:** A new view, or a deleted case whose picture stays committed, is not reported by a plain `reference` or `check` run. This is already known in the audit as an orphan-PNG gap.

**Recommendation:** Run the missing check, and a drawn-but-uncommitted check, in every mode.

**Verification:** In the project_n.py reference code, the changed scan skips drawn PNGs that have no committed counterpart (the is_file() check). The missing-committed check runs only under `if update:`. The finding is slightly overstated: a case with no picture fails in the C++ test, and a PNG that was committed but not drawn is not reported outside --update. I did not find this gap recorded in the sweep-up backlog; my search was a loose grep for 'orphan', so it may be recorded under other words.

### TOOLS-A-15: reference_case_count and tidy preprocessing have no timeout

- **Severity:** low · **Verdict:** confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:878`

`reference_case_count` runs `exe --count` with no timeout, while the per-case calls have one. `preprocessed` (line 1446) runs a compiler `-E` per unit with no timeout and discards stderr, so a failure shows only as 'uncached'. The ASAN or lavapipe startup can hang the whole gate on the count call.

**Evidence:** lines 876-883, 1446.

**Impact:** The gate can hang silently, which the REFERENCE_CASE_TIMEOUT rationale says to avoid.

**Recommendation:** Give both calls a timeout. Log (once, briefly) when preprocessing fails and a unit falls back to uncached linting.

**Verification:** reference_case_count calls subprocess.run([exe, '--count']) with no timeout, and preprocessed() runs subprocess.run with stderr=DEVNULL and no timeout. Both match the description. A hang is only theoretical. The docstring of preprocessed() says that a failure means the unit is linted uncached, so silent fallback is intended.

### TOOLS-A-16: ASAN_OPTIONS detect_leaks=1 is set for every XMake call on macOS too

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** portability · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:57`

`SANITIZER_ENV` always sets `detect_leaks=1` unless `HOST == "windows"`, even for non-sanitizer builds and on macOS. The SANITIZERS table deliberately omits `leak` for macos-clang, because LeakSanitizer is unsupported on Apple Silicon. Apple's ASan can report that unsupported option and fail or warn.

**Evidence:** lines 49-58 versus macos-clang's tuple (no `leak`).

**Impact:** Possible failure or noise in ASan test runs on macOS, which CI's macos job would reveal. I did not verify the runtime's behaviour.

**Recommendation:** Set `detect_leaks=1` only for presets whose SANITIZERS entry lists `leak`. Pass the preset into `xmake_env`.

**Verification:** SANITIZER_ENV at lines 56-57 sets detect_leaks=1 unless HOST == 'windows', and the SANITIZERS table omits leak for macos-clang. So the setting is applied on macOS. Whether Apple's ASan rejects or ignores detect_leaks=1 is not verifiable by reading. Apple Clang's ASan prints a warning, not a failure, for unsupported detect_leaks, but I did not confirm this. I also did not check whether SANITIZER_ENV is applied to non-sanitizer builds, as the finding claims. CI's macOS job would decide.

### TOOLS-A-17: lock_entries parser is a hand-written Lua table reader with silent edge cases

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:166`

Keys are cleaned with `token.strip('[] ').strip('"')`, which removes characters rather than delimiters. Duplicate keys overwrite silently. The `"|"`-count heuristic decides what is a platform table. Lua long strings, comments and escapes in values are unsupported, and in that case the parser returns None and the restore is skipped, which is safe but noisy. A concurrent project_n in another terminal can also have its XMake lock write clobbered by this run's restore.

**Evidence:** lines 166-211; run() lines 234-244.

**Impact:** Low. Worst case is a spurious diff print or a missed restore. A restore can overwrite a concurrent run's legitimate addition.

**Recommendation:** Document the limitation, or serialise XMake calls per checkout with a file lock. Keep the failure mode (None leads to a printed diff).

**Verification:** The parser behaves as described: strip('[] ').strip('"') strips characters, duplicate keys overwrite, and the "|" count decides platform tables. But the failure mode is safe: None means the diff is printed and no restore happens, as the finding says. Keys are package names and `platform|arch`, which never begin or end with brackets or quotes in practice. The concurrency scenario is speculative. No action needed beyond documenting.

### TOOLS-A-18: Platform-maths lint justifies itself with cross-platform determinism

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** docs · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:1317`

The docstring says core/math.hpp's functions "give the same results on every machine". The recorded decision is that replays are deterministic within one binary only, and that there is no cross-platform determinism goal. The regex also omits `sincos`, `exp10`, `sinpi`/`cospi`, `acospi` and the C++17 special functions (`std::beta`, `std::expint`, `std::cyl_bessel_*`). It does not catch `&std::sin` or a function pointer.

**Evidence:** lines 1313-1325; the decision list in review-history says there is no cross-platform determinism goal.

**Impact:** Misleading rationale. If cross-platform determinism is ever wanted, the lint has gaps.

**Recommendation:** Reword the docstring to the actual goal, such as one binary and stable results across compilers. Extend the regex or note that it is best-effort.

**Verification:** The docstring is consistent with src/core/math.hpp, which says 'one binary gives the same results on every machine' (CPU/library path variance). It does not claim cross-platform determinism, and design 0002 l.591 only rules that out as a goal. So the 'contradicts decision' claim is mostly wrong. The regex gaps (sincos, sinpi, &std::sin, special functions) are real, but a best-effort lint is reasonable.

### TOOLS-A-19: prune_tidy deletes every cached result when lint never reached clang-tidy

- **Severity:** info (reviewer said low) · **Verdict:** deliberate · **Category:** design · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:495`

With no mark file, `start` is infinity, so every file in the tidy cache folder is removed. A PR run that fails before clang-tidy starts (format or commit checks) and still runs prune-build-cache wipes the tidy results restored from main. The object cache keeps the run's `save=true`.

**Evidence:** lines 495-510.

**Impact:** A subsequent run lints cold (about 84 s versus 13 s). The effect is bounded, and it depends on whether the workflow runs prune after a failed lint.

**Recommendation:** With no mark, keep the folder untouched. Treat missing as not run, not nothing used.

**Verification:** The docstring of prune_tidy and tools/AGENTS.md l.20 both state 'no mark, none': lint never reached clang-tidy, so none was used. Wiping the folder in that case is documented behaviour. Whether to change it is a trade-off, not a bug.

### TOOLS-A-20: AGENTS.md entries are essays, not one line per rule, and duplicate code docstrings

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/AGENTS.md:22`

The root rule says an AGENTS.md holds one line per rule and about 100 lines. This file has 49 lines, but the `measure` entry (line 22) and the Gotchas (lines 43-48) are paragraphs of several hundred words each. They carry dated measurements and history (2026-11-12, 2026-11-14 and so on) and restate the implementation (every constant name). They will drift from the code, and a reader has to scan them in full for any task here. The Commands section also mixes usage with implementation detail.

**Evidence:** tools/AGENTS.md lines 22, 43-48.

**Impact:** Higher context cost and stale-doc risk. A change to `measure`'s flags requires editing a 1,500-character line.

**Recommendation:** Cut each entry to the rule and a pointer to the function docstring. Move measurements to the design doc or the spec's Outcome section.

**Verification:** The long entries are real: tools/AGENTS.md has long Commands and Gotchas lines with dated measurements, such as the 2026-11-10 and 2026-11-12 timings. The file is 49 lines, well under the 100-line budget. The root rule says 'one line per rule', and these are one (long) line each, so it is a style nit rather than a violation. The drift risk is plausible.
