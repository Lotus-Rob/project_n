# Review: build-env (part 2)

### BUILD-ENV-11: Per-test `add_tests` pattern matching is fragile (suffix patterns, substring stems)

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** tests · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/xmake.lua:33`

`add_file_tests` builds a doctest `--source-file` filter of `*/<stem>_tests.cpp,*\<stem>_tests.cpp` for each file. It builds the `rest` run from `--source-file-exclude` of all of them. Stems are taken from basenames only, so two test files in different directories of one target with the same name would collide in `add_tests` names. The `rest` selection depends on doctest's matching of `__FILE__`, which can be a relative path or a path with `..`. A file named `x_tests.cpp` would also exclude from rest any file whose path ends `/x_tests.cpp` in another area.

**Impact:** A renamed or relocated test file could be run twice or silently dropped from every run if its `__FILE__` form does not match the patterns. The `rest` run passes if it selects nothing, which hides the loss.

**Recommendation:** Add a selftest (project_n) that compares doctest's `--list-test-cases` totals with the union of the per-file runs, so a dropped case fails.

**Verification:** tests/xmake.lua:33 builds the filters from basenames, and its comment says the `rest` run exists so 'no case drops out'. test_file_pattern in tools/project_n.py:814 mirrors it. The selftest already forbids rest_tests.cpp. A same-stem collision within one target and doctest's __FILE__ matching are plausible but untested edge cases. I found no list-test-cases union check. project_n.py:908 uses --list-test-cases only for reference cases. The 'rest passes if it selects nothing' point is documented in the comment.

### BUILD-ENV-12: profiler environment depends on an alpha libcapstone from a dev label

- **Severity:** low · **Verdict:** confirmed · **Category:** supply-chain · **Status:** new · **Reviewer confidence:** low
- **Where:** `pixi.toml:127`

The profiler feature adds the channel `conda-forge/label/capstone_dev` and `libcapstone ==6.0.0a4` (an alpha) on Linux and macOS. It is exact-pinned and lock-hashed, but the label channel is mutable and may be removed. It is a dev-only tool, not shipped.

**Impact:** Developer tooling (`profile`, `profiler`) could break if the label is pruned, and the label channel gets weaker scrutiny than the main channel. The effect on shipped artefacts is none.

**Recommendation:** Keep the lock and note a fallback (build the viewer from source, or use the stable capstone once released). Watch conda-forge for a stable capstone 6.

**Verification:** pixi.toml lines 120-132 confirm libcapstone ==6.0.0a4 from conda-forge/label/capstone_dev on linux and osx. It is exact-pinned, in the profiler feature only, and the comment explains why (only published under this label). The label risk is real but limited to dev tooling.

### BUILD-ENV-13: Absolute CONDA_PREFIX baked into Linux ldflags at file scope when CONDA_PREFIX is unset

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** low
- **Where:** `xmake.lua:283`

`local prefix = os.getenv("CONDA_PREFIX") or ""` is used at file scope for `-Wl,--whole-archive,<prefix>/lib/libc++abi.a` and the macOS rpath. If XMake is run outside `pixi run` the toolchain's `assert(prefix)` fires only when that toolchain loads. A bogus `/lib/libc++abi.a` path (host's) would be in the ldflags before that. Any toolchain-less invocation (a plain `xmake f`) gets the empty prefix.

**Impact:** Confusing link failures, or on a host with `/lib/libc++abi.a`, a silent host library leak that violates the repo's no-host-library rule.

**Recommendation:** Assert `CONDA_PREFIX` once at the top of xmake.lua (outside description scope if XMake permits), or skip these flags when it is empty.

**Verification:** The cited line is wrong: the code is at xmake.lua:215 (Linux) and :228 (macOS). The empty-prefix fallback is real. In the pixi-clang Linux non-sanitized case it produces `-Wl,--whole-archive,/lib/libc++abi.a`. However, the pixi-clang toolchain already asserts CONDA_PREFIX when it loads (lines 186-187), and that toolchain is the only one that uses this flag. A host libc++abi.a leak without pixi is only possible if a toolchain is chosen some other way, and repo tooling always goes through pixi. The impact is minor.

### BUILD-ENV-14: Hooks require pixi on PATH and run through `pixi run` for every commit

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** dx · **Status:** new · **Reviewer confidence:** low
- **Where:** `.githooks/pre-commit:4`

The three hooks `exec pixi run --quiet ...` and have no fallback when pixi is not on PATH. This applies to GUI git clients and some IDEs, which have a minimal PATH. pre-commit runs `fmt --staged`, which startup cost is paid on every commit. The pre-push hook needs git-lfs from the Pixi env.

**Impact:** Commits and pushes fail from a GUI client with an unhelpful `pixi: not found` message. `--no-verify` bypasses the hooks, and CI is then the only gate. The comment-level rule that `lint` stays out of pre-commit is sound.

**Recommendation:** Print a clear message when pixi is missing (`command -v pixi || { echo ...; exit 1; }`), and mention it in `setup` output.

**Verification:** .githooks/pre-commit is `exec pixi run --quiet project_n fmt --staged`, with no check for pixi on PATH. So a missing pixi gives a plain 'not found' error. This is a real minor DX gap. Hooks are only enabled by `setup`, which requires pixi anyway, and pixi is the repo's mandatory entry point per AGENTS.md. Lint is deliberately left out of the hook, as the comment says.

### BUILD-ENV-15: Lock pins different xmake-repo commits per platform

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** pin-hygiene · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `xmake-requires.lock:434`

64 entries use xmake-repo commit cf09fff0. The macOS entries for cmake, doctest, egl-headers, ninja and opengl-headers use 6d79f3da. The Windows meson, pkgconf and python entries use 7060d07c. The lock was written incrementally on different hosts and never refreshed.

**Impact:** The same package (doctest, ninja) can resolve from different recipe revisions per platform. A recipe fix or checksum change in one commit may not reach the other platforms. XMake may also re-clone or update the repo at inconsistent revisions.

**Recommendation:** Regenerate the lock so all platforms use one commit, or add a lint check that every `repo.commit` in the lock is identical. macOS entries need hardware, which is already known.

**Verification:** The lock holds mixed repo commits (cf09fff0 plus other hashes). It is marked known-deferred: sweep-up-backlog.md line 20 says macOS entries need a Mac, and CI's macos job passes without them. The Windows 7060d07c part I did not confirm separately. Low-value.

### BUILD-ENV-16: Tracy is built twice (on and off) in every mode and preset

- **Severity:** info · **Verdict:** deliberate · **Category:** build-time · **Status:** new · **Reviewer confidence:** low
- **Where:** `xmake.lua:266`

Both `tracy_on` and `tracy_off` are `add_requires` unconditionally, spelled `v0.14.1` and `0.14.1` to get two lock keys. Only one is used per mode. The two spellings are a hack that depends on XMake's key format. The comment says the lock must hold both.

**Impact:** Wasted package build time on every cold build and in CI. Fragile if XMake normalises version strings in a future version.

**Recommendation:** Require only the variant the mode needs and have a dedicated lock-refresh step write the other, or switch to one package with a mode-dependent config if XMake allows it. Add a selftest that the lock holds both keys.

**Verification:** The two add_requires are at lines 266-267. Design doc 0001 line 376 documents that the lock holds both variants, and that XMake drops the unused one from its platform's section, with project_n writing back the dropped entries. So only one is built per mode; the claim that Tracy is built twice is likely wrong. The two spellings are a deliberate way to get two lock keys.

### BUILD-ENV-17: Image tag hashes pixi.toml and pixi.lock, so any edit rebuilds images

- **Severity:** info · **Verdict:** confirmed · **Category:** build-time · **Status:** new · **Reviewer confidence:** low
- **Where:** `containers/.containerignore:4`

IMAGE_INPUTS holds pixi.toml, pixi.lock and activate.sh. The image bakes `pixi install --locked`, so a comment-only edit to pixi.toml, or a bump of an unrelated osx-only entry, changes the hash and rebuilds the Linux image. The same input list also reaches the msvc image through BASE_IMAGE.

**Impact:** Wasted CI and local rebuild time (a full toolchain install) for changes that do not affect the Linux image.

**Recommendation:** Accept it for simplicity, or hash only the resolved Linux and lavapipe environments.

**Verification:** IMAGE_INPUTS hashes pixi.toml and pixi.lock, and the image runs `pixi install --locked`. Any edit therefore rebuilds the image. This is a simplicity trade-off with no correctness impact, and the finding itself says to accept it.

### BUILD-ENV-18: .gitignore patterns are broader than intended

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** hygiene · **Status:** new · **Reviewer confidence:** low
- **Where:** `.gitignore:22`

`build/`, `dist/`, `logs/` and `*.key` etc. have no leading slash, so they match at any depth. A future `src/build/`, `tools/build/`, `art/logs/` or an asset named `*.key` is ignored silently. `typos.toml` is ignored even though a project-wide config might one day be wanted. `.env.*` ignores `.env.example`.

**Impact:** New source files may fail to be added without an error (`git add` skips ignored files), which is hard to diagnose.

**Recommendation:** Anchor with a leading slash (`/build/`, `/dist/`, `/logs/`) and add `!.env.example` if one is wanted.

**Verification:** .gitignore does have unanchored `build/`, `dist/`, `logs/`, `*.key` and `.env.*`, as claimed. Only these directories are meant to be ignored, and the repo already has src/ and tools/ with no such subdirectories. `logs/` is commented as 'never here'. The risk is hypothetical, and `git add` of an ignored file does give a hint. typos.toml is deliberately per-developer, per the comment in .gitignore.

### BUILD-ENV-19: -Wno-padded, -Wno-effc++ and the C++98 warnings are disabled globally

- **Severity:** info · **Verdict:** deliberate · **Category:** flags · **Status:** contradicts-recorded-decision · **Reviewer confidence:** low
- **Where:** `xmake.lua:153`

The "everything" warning level is applied but with Clang's `-Wno-padded` and several compat warnings, GCC's `-Wno-effc++`, and MSVC's /wd for 4820, 4710, 4711, 4514, 5045, 4625, 4626, 5026 and 5027. Each has a stated rationale. `-Wno-padded` is a recorded decision.

**Impact:** Padding waste in hot structs (replay ticks, sprite instances, saves) is invisible to the compiler. It is cheap to check separately.

**Recommendation:** Consider a one-off per-module `-Wpadded` report in `measure` or a size `static_assert` for the hot structs instead of a global warning.

**Verification:** docs/design/0001 rule 15 records -Wpadded (and MSVC C4820) as excluded 'kept by the user's call, 2026-11-05', and -Weffc++ removal (user, 2026-11-17). The suggestion of a separate report is an optional idea only.

### BUILD-ENV-20: set_xmakever is a minimum, not the exact pin

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** pin-hygiene · **Status:** new · **Reviewer confidence:** low
- **Where:** `xmake.lua:3`

`set_xmakever("3.1.2")` means at least 3.1.2. pixi.toml pins exactly 3.1.2 on Linux and Windows. macOS downloads an upstream bundle with a checksum, and the comment says to keep them equal by hand.

**Impact:** If the macOS bundle or a host xmake were newer, the shared-PCH monkey-patching and lock format could behave differently without a warning.

**Recommendation:** Have project_n assert `xmake --version` equals pixi.toml's pin at startup and test that the macOS URL version matches.

**Verification:** set_xmakever("3.1.2") is a minimum, as stated. But tools/project_n.py doctor (about line 3078) already compares `xmake --version` with pinned_xmake() (read from xmake.lua) and reports a mismatch. So a check exists, in doctor rather than at startup. pixi.toml pins xmake==3.1.2. macOS URL/version is derived from set_xmakever per the comment at project_n.py:41. The risk is small.
