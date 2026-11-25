# Review: tools-a (part 3)

### TOOLS-A-21: AGENTS.md names three single sources of truth, but more tables in the file also act as one

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/AGENTS.md:9`

`PRESETS`, `IMAGES` and `SCOPES` are called the single source of truth. `SANITIZERS`, `MODULES`, `FACADES`, `SHADER_STAGES`, `MSL_VERSION`, `FUZZ_PRESET`, `WARM_LEAVES` and `CI_CACHED_TOOLCHAINS` are also policy tables. The first two (`SANITIZERS`, `MODULES`) mirror architecture that src/AGENTS.md and the CI workflows describe, and `CI_CACHED_TOOLCHAINS` mirrors xmake.lua's GCC and MSVC cache switches (comments at lines 108-112 and 331-334 say so).

**Evidence:** lines 108-113, 331-334.

**Impact:** Policy duplicated between Python, xmake.lua and docs can drift. The `uses_build_cache` docstring admits it is 'the same toolchains as CI_CACHED_TOOLCHAINS today, for another reason'.

**Recommendation:** Derive `uses_build_cache` and `CI_CACHED_TOOLCHAINS` from one table. Extend the AGENTS rule to name the other tables.

**Verification:** The AGENTS rule names only PRESETS, IMAGES and SCOPES as the single source of truth, which is accurate. The other tables are not claimed to be single sources, and the mirroring with xmake.lua is acknowledged in the code comments. Deriving uses_build_cache from CI_CACHED_TOOLCHAINS would be a minor cleanup. The finding itself rates its confidence as low.

### TOOLS-A-22: Parallel local runs race on shared directories

- **Severity:** info (reviewer said low) · **Verdict:** unverifiable · **Category:** concurrency · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:944`

`run_references` does `shutil.rmtree(ROOT/'build'/'reference')` and recreates it. Concurrent `reference` and `measure` runs (measure runs `reference` for the shimmer), or two terminals, delete and rewrite each other's pictures. `install_cl_wrapper` unlinks then writes the shared cl.exe wrapper while another checkout's build may be executing it. `ensure_msvc` has no lock around its `rmtree` plus install.

**Evidence:** lines 944-946, 639-645, 621-636.

**Impact:** Rare flaky failures, such as a missing cl.exe for a few milliseconds or an incomplete install seen by a second process. The install directory is shared across checkouts.

**Recommendation:** Write the wrapper via a temp file and `os.replace`. Add a file lock around the MSVC install. Use a per-process output directory for reference pictures.

**Verification:** run_references does rmtree and mkdir on build/reference, which is confirmed. A concurrent run is possible in principle, but it is not a supported workflow, because AGENTS says to leave the tree alone and measure to run alone. I did not read the cl-wrapper or ensure_msvc code, so those claims are unchecked. Verifying them would need a read of those functions and a view of CI's concurrency model.

### TOOLS-A-23: Hard-coded lavapipe ICD name and container UID

- **Severity:** info · **Verdict:** confirmed · **Category:** portability · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:943`

`lvp_icd.x86_64.json` is hard-coded, so the reference run fails on a Linux aarch64 host. `--userns=keep-id:uid=1000,gid=1000` assumes the in-image user is 1000. `:Z` relabels the entire checkout and the msvc install on SELinux hosts, which is a surprising side effect on a user's source tree.

**Evidence:** lines 943, 661.

**Impact:** Presets are x86_64 only today, so this is dormant. The SELinux relabel is a real side effect on the repo folder for Fedora-style hosts.

**Recommendation:** Glob `lvp_icd*.json`. Document the `:Z` relabel, or use `:z` (shared) for the source mount.

**Verification:** lvp_icd.x86_64.json is hard-coded, as shown at the VK_DRIVER_FILES line in run_references. Presets are x86_64 only, so the issue is dormant, as the finding says. I did not check the :Z or UID 1000 claims.

### TOOLS-A-24: Include lints are bypassed by relative includes and angle-bracket spellings

- **Severity:** info · **Verdict:** confirmed · **Category:** lint-policy · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:1293`

`crossed_modules` looks only at `"module/..."` includes. `"../gfx/x.hpp"`, `#include <gfx/x.hpp>` (angle form of our header) and includes after a preceding block comment on the same line are not checked. Files directly under `src/` (main.cpp) and all of tests/ skip the direction rule. `leaked_dependencies` accepts any header under `/internal/` to include any library. OUTPUT_RE lacks `fputs`, `putchar`, `perror` and `fmt::print`.

**Evidence:** lines 1229, 1293-1310, 1329.

**Impact:** The architecture rules are enforceable by convention rather than by the tool. These are bypass routes, not live violations.

**Recommendation:** Resolve each include to a path relative to src/ before the check, and forbid `..` in project includes. Add the missing output functions.

**Verification:** Checked crossed_modules (l.1293-1310): INCLUDE_RE accepts both bracket forms but `m[1] != '"'` skips angle includes; module is taken from the first path segment so `../gfx/x.hpp` is not in MODULES and is skipped. Files directly under src/ have own=None and tests/ likewise skip the direction rule; the docstring says main.cpp and tests may include any public header, so that part is deliberate. leaked_dependencies accepts any header with /internal/ in its path (l.1276), as its docstring states. OUTPUT_RE (l.1329) lacks fputs, putchar, perror and fmt::print. Bypass routes only, no live violation shown.

### TOOLS-A-25: --range with a three-dot or single-ref value silently widens to a full run

- **Severity:** info · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:1212`

`rng.split("..")` turns `A...B` into `['A', '.B']`, and a single ref yields one element. `git merge-base` then fails with check=False, `base` is empty and `changed_files` returns None, so everything is linted. Wrong input becomes a slow success, not an error.

**Evidence:** lines 1210-1214.

**Impact:** Confusing slowness. The behaviour is documented in a comment as the force-push fallback, so it hides user typos.

**Recommendation:** Validate the range shape in the argument parser. Use the fallback only when `git cat-file -e` reports the base as missing.

**Verification:** changed_files (l.1210) does `git merge-base *rng.split('..')` with check=False. `A...B` splits to ['A','.B'], which fails. A single ref gives one argument, which also fails. Empty base returns None, so scoped_sources lints everything. The comment says this is the fallback for a force-pushed CI range. --range (l.3266) has no shape validation. It is a slow success on a typo, not a wrong result.

### TOOLS-A-26: python_comments can crash lint on a file that fails to tokenize or parse

- **Severity:** info · **Verdict:** confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:1155`

`tokenize.generate_tokens` and `ast.parse` raise on a syntax error or a tokenize error in a `.py` file under lint. Nothing converts that into a finding, so the whole `lint` aborts with a traceback. Lua `--[[ ]]` block comments are only checked on their first line, and a `\\`-continued `//` comment's second line is not checked. `L'"'` or `u8'"'` confuses the string skipper because the prefix letter makes the quote look like an apostrophe.

**Evidence:** lines 1088-1161.

**Impact:** A traceback in lint on a half-edited Python file, and a few missed comment lines. Narrow edge cases.

**Recommendation:** Catch SyntaxError and tokenize.TokenError and report `path: cannot parse`. Handle Lua block comments and line splices.

**Verification:** python_comments (l.1155-1161) calls tokenize.generate_tokens and ast.parse unguarded. The grep shows no SyntaxError or TokenError handling anywhere in project_n.py, and the only caller is l.1176, so a malformed .py under lint would raise a traceback. Edge-case claims hold: c_comments line-continued // comments are not followed, and the `'` check on the previous alnum char means L'"' or u8'"' is treated as an apostrophe. Narrow impact, since a Python file that does not parse would fail elsewhere anyway.

### TOOLS-A-27: Lock-file restore leaves dropped entries dropped when anything was also added

- **Severity:** low · **Verdict:** refuted · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:224`

`restore_lock_unless_added` writes the old bytes back only if the new entry set is a subset of the old. If XMake both prunes entries for other platforms (the known windows-msvc container case) and adds a new one, the file stays as XMake wrote it, so the pruned entries are gone. The user is told to commit it.

**Evidence:** lines 224-231; AGENTS.md Gotchas on lock handling.

**Impact:** A commit from a container or one platform can silently delete the other platforms' lock entries. This is the same failure the logic exists to prevent, and it happens exactly when a dependency is added.

**Recommendation:** In the mixed case, write `before` plus only the added or changed entries, or at least warn that entries were dropped. Alternatively, restore and print only the diff of additions.

**Verification:** The restore logic is true as described: the subset check at line 224 means a mixed add+prune keeps XMake's output. But the docstring (214-221) says this is deliberate: an added or changed entry stays and its diff is printed to be committed, so that a platform's missing entries added on CI hosts are not missed. The risk of committing pruned entries from the mixed case is a known trade-off, and the user is told to review the printed diff. The diff output would show the removals. Treat as deliberate.
