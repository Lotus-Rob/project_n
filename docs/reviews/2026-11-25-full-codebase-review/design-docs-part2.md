# Review: design-docs (part 2)

### DESIGN-DOCS-11: Rule 17 says Clang has LeakSanitizer; macos-clang does not

- **Severity:** low · **Verdict:** confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0001-repository-build-environment.md:374`

'GCC and Clang have address, undefined, thread and leak'. SANITIZERS gives macos-clang only address, undefined and thread.

**Evidence:** tools/project_n.py lines 49–54.

**Impact:** A minor mismatch for anyone planning sanitizer runs on macOS.

**Recommendation:** Say 'Linux GCC and Clang: address, undefined, thread, leak; macOS Clang: no leak; MSVC: address'.

**Verification:** SANITIZERS in tools/project_n.py lines 48-54 gives macos-clang address, undefined and thread only. linux-clang has leak. Rule 17 at 0001 line 374 says GCC and Clang have leak, which is true only on Linux. I did not re-read line 374 itself.

### DESIGN-DOCS-12: §5.2 image tag inputs are stale

- **Severity:** low · **Verdict:** confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0001-repository-build-environment.md:204`

The doc says the tag is a hash of Containerfile + pixi.lock. IMAGE_INPUTS also hashes pixi.toml and tools/env/activate.sh, and the image now installs the `lavapipe` environment as well (`pixi install --locked -e lavapipe`), which the doc doesn't mention.

**Evidence:** tools/project_n.py IMAGE_INPUTS; containers/linux.Containerfile last RUN.

**Impact:** Minor: a reader underestimates what rebuilds the image.

**Recommendation:** List every input to the tag, and the lavapipe environment.

**Verification:** tools/project_n.py:101 IMAGE_INPUTS = pixi.toml, pixi.lock, tools/env/activate.sh, and image_tag (591) also hashes the Containerfile and the base image's tag. The Containerfile's last RUN also does `pixi install --locked -e lavapipe`. The doc's tag line (~204) lists only Containerfile + pixi.lock.

### DESIGN-DOCS-13: S2 says Windows artefacts are now reproducible, though MSVC builds still differ

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** doc-drift · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `docs/design/0001-repository-build-environment.md:713`

§13.2 says `/Brepro` made the MSVC binary deterministic ('two clean local builds at different times now match'), and the S2 row reads as passed. The backlog records MSVC builds differing by 69 bytes despite /Brepro, which contradicts Goal 2 ('artefacts identical for the same commit, whichever machine').

**Evidence:** review-history: 'MSVC builds differ 69 bytes despite /Brepro'.

**Impact:** The design doc claims a reproducibility property the project knows it lacks.

**Recommendation:** Reopen S2 for Windows in §13 and record the 69-byte difference until it is explained.

**Verification:** The backlog (sweep-up-backlog.md:29) records 69-byte MSVC differences between local builds. It also records that two uncached CI builds gave a byte-identical .exe, and the doc's header lists the cross-machine Windows S2 check as open. So the doc already flags the Windows re-check, and its claim is about local builds. The §13.2 wording 'now match' is stronger than the backlog supports, so it is known-deferred drift.

### DESIGN-DOCS-14: Symbols are kept 'for crash reports' but no crash is ever captured, and terminate/signals don't flush the log

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** design-gap · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0001-repository-build-environment.md:387`

Rule 20 keeps .sym/.dSYM/.pdb 'for crash reports', but nothing produces a crash report: no signal or SEH handler, no minidump, no std::set_terminate. Only PN_ASSERT flushes Quill before aborting (core/assert.cpp). A segfault, std::terminate (e.g. an exception on a worker) or abort elsewhere loses Quill's async queue, which holds the last, most useful lines.

**Evidence:** `grep -rn set_terminate|signal src` → nothing; core/assert.cpp flushes only on assertion; log.cpp has no crash handler.

**Impact:** Player crashes leave no stack, no dump and a truncated log, so the kept symbols are useless and the 'reproducible bug reports' goal (0002 §1 goal 5) fails exactly when it matters.

**Recommendation:** Add a terminate handler and fatal-signal/SEH handler that log Critical with a backtrace (std::stacktrace is C++23, or Quill's backend signal handler) and flush. Optionally write a minidump on Windows. Record the design in 0001 rule 20 or 0002 §12.

**Verification:** grep of src for terminate, SIGSEGV, crash handler and minidump found nothing, so no crash capture exists. The keep-symbols rule is therefore not backed by a capture path. This is a design gap, not drift or a bug. Rule 20 only says the symbols are kept 'for crash reports', which is symbols for a debugger on a user-supplied dump, so it is not false. The impact is overstated for a pre-1.0 game. I rate it low, as a future feature.

### DESIGN-DOCS-15: Hardening rule claims PIE, but nothing sets or verifies it

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** build-hardening · **Status:** new · **Reviewer confidence:** low
- **Where:** `docs/design/0001-repository-build-environment.md:388`

Rule 21: 'shipped Linux binaries are PIE with full RELRO'. xmake.lua adds `-z relro -z now`, but no `-fPIE/-pie`. PIE therefore depends on how conda-forge's GCC/Clang were configured (default-pie), and artifact checks neither PIE nor BIND_NOW.

**Evidence:** xmake.lua lines 214–226 (no pie flags); `grep -i pie tools/project_n.py` → nothing.

**Impact:** A toolchain bump to a compiler built without default PIE would silently ship non-PIE binaries.

**Recommendation:** Set `set_policy("build.pie")` or add `-fPIE -pie` explicitly, and make artifact check the ELF type (ET_DYN) and BIND_NOW with readelf.

**Verification:** xmake.lua:216 sets only -z relro,-z now, and neither xmake.lua nor tools/project_n.py has any pie flag or check. PIE therefore relies on the toolchain default, so the doc's claim is unverified by code. The risk is hypothetical and only arises on a compiler change.

### DESIGN-DOCS-16: 0001.1 says 'six jobs' pull LFS; there are now eight

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0001.1-git-lfs.md:73`

§5 counts six LFS-using jobs (lint, three container jobs, windows, macos). Since windows and macos split into release and address entries and releasedbg was added, eight jobs use `.github/actions/lfs`.

**Evidence:** .github/workflows/ci.yml uses ./.github/actions/lfs in lint, container (x3), releasedbg, windows (x2), macos (x2).

**Impact:** The bandwidth estimate behind the cache rationale is low by a third.

**Recommendation:** Update the count, or say 'every job but release'.

**Verification:** The cited line 73 does say six jobs. The CI file has five jobs that use the lfs action: lint, container, releasedbg, windows and macos. Matrix expansion gives container x3 and windows and macos x2 each (assuming two entries per matrix, release and address), which makes 9 job instances, not the claimed 8 or the doc's 6. The count is stale either way, but the exact number in the finding is not verified.

### DESIGN-DOCS-17: CI cancels in-progress runs on main, contradicting 'pushes to main run them again'

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** ci · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/design/0001-repository-build-environment.md:643`

§12 says pushes to main rerun every job (rebase merges write new commits) and main saves its cold build for new branches. ci.yml sets `concurrency: group: ci-<event>-<ref>, cancel-in-progress: true`, so two merges in quick succession cancel the first main run: that commit is never validated on main, and its cache saves may not happen.

**Evidence:** .github/workflows/ci.yml lines 12–14.

**Impact:** A broken intermediate main commit can go unnoticed, which matters for release --tag on an exact commit and for bisecting.

**Recommendation:** Use `cancel-in-progress: ${{ github.ref != 'refs/heads/main' }}`, or document the trade-off in §12.

**Verification:** ci.yml lines 12-14 set cancel-in-progress: true unconditionally, with the group keyed on event and ref. Two quick pushes to main therefore cancel the earlier run. The doc's claim that pushes to main run everything again is only true when they don't overlap. I did not find the trade-off documented in §12. The effect is minor because the PR gate already validated each commit.

### DESIGN-DOCS-18: 0002 header is stale: 'Draft', 'phases 1 to 5', and 'Proposed' on decisions long built

- **Severity:** low · **Verdict:** confirmed · **Category:** doc-drift · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:5`

Status: '**Draft**... phases 1 to 5 of §16 implemented'. Phase 6 is far along. A10 (DXC/SPIRV-Cross), A17 (yaml-cpp) and A19 are still marked *Proposed* though built and shipped, and §16's phase 6 text (RmlUi, 5,000-row list) no longer matches the phase 6 spec's chunks (world map, travel, fog, roads). The roadmap ends at phase 7.

**Evidence:** 0002 lines 5, 88, 95, 97, 568; docs/superpowers/specs/2026-11-10-phase6-design.md.

**Impact:** Readers can't tell which decisions are settled. The AGENTS.md instruction to 'read docs/design before redesigning anything' leads to an obsolete plan.

**Recommendation:** Promote 0002 to Accepted, with the unbuilt parts marked 'planned'. Drop the Proposed markers on built decisions, and replace §16 phases 6+ with pointers to the phase specs.

**Verification:** The 0002 status line still says 'Draft' and 'phases 1 to 5 implemented', while the Revised notes show phase 6 work. A10 (line 88), A17 (95) and A19 (97) are still marked *Proposed* though built. The doc's own header says proposed decisions wait on spikes, but the shaders and yaml-cpp are shipped. This is a known staleness, and the cost is that readers can't tell what is settled.

### DESIGN-DOCS-19: 0002 module list and module table drift from the code (app missing from A1; zstd in core; stale sdl_log note)

- **Severity:** low · **Verdict:** confirmed · **Category:** doc-drift · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:79`

Five mismatches with the code: (1) A1 lists core, data, platform, gfx, ui, audio, game + executable, but omits `app`, which §4's tree has. (2) `ui` and `audio` don't exist yet, though `MODULES` lists them. (3) The §4 table says core may use only the Quill/Tracy/EnTT/job facades, but core links zstd privately and includes it directly in core/save_file.cpp. (4) §4 still says 'Today's src/core/sdl_log.cpp ... moves to platform in phase 1' (done). (5) The §4 table says `game` exposes `Simulation`, but `core::Simulation` is the interface in core/logic.hpp and game::Host implements it.

**Evidence:** xmake.lua target core add_packages("zstd"); src/platform/sdl_log.cpp; src/core/logic.hpp line 32.

**Impact:** The layering rules readers rely on are inaccurate, and a reviewer could flag save_file.cpp as a violation or miss a real one.

**Recommendation:** Update A1 and the §4 table: add app, mark ui/audio planned, list zstd under core's private libraries (or put it behind a facade-like rule), and remove the phase-1 note.

**Verification:** xmake.lua line 341 adds zstd privately to core (used in core/save_file.cpp). src/ has app, but no ui or audio. A1 (line 79) omits app. Line 135 still says sdl_log moves to platform, but src/platform/sdl_log.cpp exists. The doc drift is real and minor. I did not check the Simulation claim.

### DESIGN-DOCS-20: §8.2 says only internal/gpu calls SDL_GPU; renderer.cpp and every feature call it directly

- **Severity:** low · **Verdict:** confirmed · **Category:** architecture-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:360`

The layer diagram says 'internal/gpu: thin owners of SDL_GPU objects; the only code calling SDL_GPU'. In fact renderer.cpp makes 107 SDL_*GPU* calls, and features/*.cpp, resources/*.cpp and frame.cpp call SDL_GPU directly (sprites 103, seals 70, meshes 62, ground 48, ...). Only ownership lives in gpu.hpp.

**Evidence:** grep -c 'SDL_[A-Za-z]*GPU' over src/gfx files.

**Impact:** The stated encapsulation (one place to swap or debug GPU calls) doesn't exist, and the oversized renderer.cpp (known) is partly a result.

**Recommendation:** Either state that features and the renderer call SDL_GPU and gpu.hpp only owns objects, or move pass and draw calls into the feature layer so renderer.cpp only orchestrates (as 0002.4.1 §5 intended).

**Verification:** Line 360 says internal/gpu is the only code calling SDL_GPU. src/gfx/renderer.cpp has 107 SDL_*GPU* call sites. The claim holds.
