# Review: build-env

**Scope:** xmake.lua, tests/xmake.lua, xmake/repo/*, xmake-requires.lock, pixi.toml (pixi.lock only spot-checked), containers/*, .githooks/*, .gitignore, .gitattributes, THIRD_PARTY_NOTICES.md, LICENSE, .vscode/*. Build correctness, reproducibility, pin hygiene, licence compliance, supply chain, compiler/linker flags, sanitizer/LTO setup.

**Findings:** 23 standing (0 critical, 0 high, 0 medium, 14 low, 9 info), 1 raised but refuted on verification.

**Parts:** build-env.md, build-env-part2.md, build-env-part3.md

## Assessment

## Build-env assessment

**Strengths**
- Pinning is very disciplined. Pixi tools are exact, with one documented exception. The xmake-repo commit is locked. The container base image is pinned by digest, and the apt snapshot, Pixi release plus checksum, and msvc-wine commit are pinned too. CI actions are pinned by SHA.
- Build-flag rationale is documented per flag and many gotchas are written down. The sanitizer and LTO rules are scoped per target so packages stay unaffected. The shared-PCH scheme is well explained. Windows reproducibility is handled with `/Brepro`, `/experimental:deterministic` and `/pathmap`.
- Packages never resolve from the host (`system=false`). The local overrides (`quill-wine` and the others) fail loudly when an upstream bump invalidates a patch.
- Licence handling is mostly thorough: a notices file with full texts, a font OFL, FreeType FTL and HarfBuzz notices, and a runtime-exception note.
- Containers hold no Microsoft files, so images are pushable. The msvc install is hashed and mounted at run time.

**Weaknesses and risks**
- Linux and macOS release binaries have no path-prefix mapping and no hardened STL or assertions in release. Hardening is a deliberately narrow set (RELRO, stack-protector-strong, fortify 2). It lacks CET, stack-clash protection and explicit PIE. The game parses untrusted saves, replays and YAML, so this matters.
- `xmake-requires.lock` is a weak reproducibility record for build tools. It records versions that differ from the Pixi pins, and it mixes xmake-repo commits across platforms.
- The `shared_pch` mechanism overrides `target.pcoutputfile` (an XMake internal) and depends on flags matching. This is fragile across XMake bumps, though XMake is pinned.
- The notices file is probably incomplete for what SDL statically bundles.
- The hand-written `.gitignore` and `.gitattributes` globs are broader than necessary, which is a latent trap.

**Overall health:** good. There are no critical or high issues in scope. The findings are mostly medium-to-low hardening and hygiene items, plus a few documentation and consistency items.

**Files read in full:** (not reported)

## Findings

### BUILD-ENV-01: No hardened standard library or assertions in release builds

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** security-hardening · **Status:** new · **Reviewer confidence:** medium
- **Where:** `xmake.lua:150`

The hardening macros (`_GLIBCXX_ASSERTIONS`, `_LIBCPP_HARDENING_MODE_EXTENSIVE`, `_MSVC_STL_HARDENING`) are added only `if is_mode("debug")`. Release gets only `_FORTIFY_SOURCE=2`, `-fstack-protector-strong` and full RELRO. PN_ASSERT is also off in release.

**Impact:** The shipped binary parses untrusted saves, replays, YAML and fuzz-style inputs. Out-of-bounds `operator[]`, `front()` and similar misuse is silent UB in release. A bug the fuzzers find only under ASan could become memory corruption for players.

**Recommendation:** Enable the cheap tier in release: libc++ `_LIBCPP_HARDENING_MODE_FAST`, `_GLIBCXX_ASSERTIONS` and MSVC `_MSVC_STL_HARDENING=1`, all of which are ABI-neutral. Measure with `measure`. If you decide against it, record that in the design docs.

**Verification:** Facts hold: xmake.lua 148-153 adds _GLIBCXX_ASSERTIONS, libc++ EXTENSIVE and _MSVC_STL_HARDENING only under is_mode("debug"). Design doc 0001 rule 21 (line 388) states this as the policy ('Debug builds bounds-check the standard library'), so it is a recorded decision, not an oversight. The rationale for release is not recorded, so the 'record it' recommendation is fair. The cost/benefit is a judgement; impact is overstated since fuzzers and ASan cover parsers. Real line is ~148-153.

### BUILD-ENV-02: Missing CET, stack-clash protection and explicit PIE

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** security-hardening · **Status:** new · **Reviewer confidence:** medium
- **Where:** `xmake.lua:146`

Linux flags are only `-fstack-protector-strong` and `-Wl,-z,relro,-z,now`. There is no `-fstack-clash-protection`, no `-fcf-protection=full` and no explicit `-fPIE`/`-pie`. PIE relies on conda GCC/Clang defaults, and nothing asserts that the artefact is PIE. Clang's default is not guaranteed in the conda toolchain. Windows has `/guard:cf` but no `/sdl`, and `/DYNAMICBASE` and `/HIGHENTROPYVA` are left to defaults. macOS gets no extra hardening.

**Impact:** Weaker exploit mitigations than a typical distro build. A silently non-PIE artefact would have no ASLR for the main image.

**Recommendation:** Add the flags explicitly, and have `artifact` check for PIE, RELRO and BIND_NOW in the produced ELF (readelf) so a regression fails.

**Verification:** Flags are as described (-fstack-protector-strong, /guard:cf; no -fcf-protection or -fstack-clash-protection). But design doc 0001 rule 21 (line 388) claims 'shipped Linux binaries are PIE with full RELRO' and I found no readelf/PIE check in tools/project_n.py, so the claim is unasserted. RELRO/BIND_NOW are in the link flags. A missing check is valid; the severity is low.

### BUILD-ENV-03: Linux and macOS release builds embed absolute build paths (no -ffile-prefix-map)

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** reproducibility · **Status:** new · **Reviewer confidence:** medium
- **Where:** `xmake.lua:97`

Windows gets `/pathmap` and `/Brepro` for reproducibility. Linux and macOS have no `-ffile-prefix-map`/`-fdebug-prefix-map`/`-fmacro-prefix-map` and no `-Wl,--build-id=sha1` or `-Wl,-ld_classic` equivalent. `__FILE__` (PN_ASSERT, Quill, doctest) and the debug info carry the checkout path. Native builds also leak the developer's home path into any shipped binary.

**Impact:** Two checkouts, or a native build versus the container build, produce different binaries. Artefact reproducibility claims only hold in the container at the fixed `/src` path. The `.sym` files contain machine-specific paths.

**Recommendation:** Add `-ffile-prefix-map=$(projectdir)=.` (plus the `-fdebug`/`-fmacro` variants if needed), and the CONDA_PREFIX mapping for packages built from source. Add a reproducibility check comparing two builds from different directories.

**Verification:** No -ffile-prefix-map exists in xmake.lua; only the Windows branch has /pathmap and /Brepro. But design doc 0001 S2 (line 674) records that bit-identical Linux artefacts passed across machines on 2026-10-30, and says to add -ffile-prefix-map if needed. Artefacts are built in the container at a fixed /src path, so reproducibility holds where it is promised. Native builds do embed home paths, but those are not shipped. Impact is overstated; medium is too high.

### BUILD-ENV-04: xmake-requires.lock records build-tool versions that differ from the Pixi pins

- **Severity:** low · **Verdict:** confirmed · **Category:** pin-hygiene · **Status:** new · **Reviewer confidence:** medium
- **Where:** `xmake-requires.lock:45`

The lock holds cmake 4.3.4, automake 1.17 and python 3.14.3, but pixi.toml pins cmake 4.4.5, automake 1.19 and python 3.14.10. The tools are excluded from `system=false`, so they come from PATH (Pixi). The lock entries are therefore informational, and if a tool were missing XMake could download a different one.

**Impact:** The lock gives a false impression of which tools build the packages. Build-tool drift would not be detected, and an environment without the Pixi tools would build with different versions silently.

**Recommendation:** Either add `add_requireconfs(... {version=...})` or a check in project_n that the PATH tool versions equal pixi.toml, or note in the lock docs that tool entries are ignored.

**Verification:** Lock has cmake version 4.3.4 (line 45) against pixi.toml cmake==4.4.5 (line 27); automake is pinned ==1.19 in pixi.toml, and python also differs per the finding. The claim is accurate: these entries are informational. Low impact because the tools come from the Pixi PATH.

### BUILD-ENV-05: shared_pch overrides XMake internals (target.pcoutputfile)

- **Severity:** low · **Verdict:** unverifiable · **Category:** fragility · **Status:** new · **Reviewer confidence:** low
- **Where:** `xmake.lua:233`

The `shared_pch` rule replaces `target.pcoutputfile` with a closure that returns the owner's file and disables the `c++.build.pcheader` rule on users. This depends on XMake's private target object layout, on rule enable semantics, and on `build.fence`. Targets with private packages (core with enkits and zstd, platform with SDL, gfx with SDL_ttf) get different include dirs from the owner. This works only because GCC and Clang tolerate include-path differences.

**Impact:** An XMake bump, or a flag added to only one target (a define, `-fsanitize`), silently reverts to rebuild or mismatch errors. Stale-PCH hits are possible if a flag that affects the header's content differs per target. The 2025 note about ccache keyed on the pch_preprocess text shows this area has already bitten.

**Recommendation:** Keep the XMake pin exact and add a CI test that detects a flag divergence, for example comparing compile commands of PCH users against the owner. Upstream the feature to XMake if possible.

**Verification:** I did not open the shared_pch rule. Whether it is fragile against XMake internals depends on the XMake pin and on behaviour that reading cannot settle. The ccache/pch_preprocess note at xmake.lua line 88-91 shows the area was exercised. Decide it by changing a per-target flag and watching the build; the general fragility claim is speculative.

### BUILD-ENV-06: THIRD_PARTY_NOTICES omits components SDL bundles in the static link

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** licence · **Status:** new · **Reviewer confidence:** low
- **Where:** `THIRD_PARTY_NOTICES.md:7`

The SDL section lists only the zlib licence and stb_image. SDL3 statically linked also compiles in other third-party code. Examples are the dlmalloc and libm (Sun fdlibm style) sources under src/stdlib and src/libm, HIDAPI (BSD-3-Clause among its options), the Wayland and xdg protocol glue, Khronos headers, and the yuv2rgb code. Some of these carry their own notice requirements. A grep for `hidapi` in the file finds nothing.

**Impact:** Binary redistribution may not meet the notice conditions of the BSD-style licences that SDL bundles. This is low legal risk but a compliance gap in a project that advertises a complete notices file.

**Recommendation:** Audit SDL 3.4.16's `LICENSE`/`docs` and the actual compiled sources, add the missing notices or state that they are not compiled. Add an `add-dependency` skill step to re-audit bundled code on every SDL bump.

**Verification:** The SDL section (lines 7-31) lists only the zlib licence and stb_image, and grep for hidapi or dlmalloc finds nothing. The omission is real as far as the file goes. But I did not check which bundled sources are compiled into the XMake SDL3 build, and the Linux build may use the system or disabled HIDAPI. Most of the bundled code (dlmalloc is public domain/CC0, fdlibm-style libm is permissive) needs little attribution, so risk is low. An audit would settle it.

### BUILD-ENV-07: Notices file has no check tying it to the lock and add_requires

- **Severity:** low · **Verdict:** confirmed · **Category:** licence · **Status:** new · **Reviewer confidence:** low
- **Where:** `THIRD_PARTY_NOTICES.md:372`

Versions in the notices are typed by hand: SDL 3.4.16, libc++ 22.1.8, FreeType 2.14.1, HarfBuzz 13.1.1, zlib 1.3.2, Quill 13.1.0 and so on. Windows and macOS locks pull in different transitive packages. No test is visible that every lock package, or every statically linked one, has a notice section. Tracy (BSD-3, releasedbg only) is correctly absent only because it is not shipped.

**Impact:** A dependency bump or a new transitive package (a libpng behind FreeType, for example) can leave notices stale without anyone noticing.

**Recommendation:** Add a lint or selftest that parses the lock and `add_requires` lines against the notices headings and versions, with an explicit allowlist of build-only tools.

**Verification:** grep of tools/*.py: THIRD_PARTY_NOTICES.md is only copied into dist (project_n.py:2378) and a selftest fixture mentions it (test_project_n.py:2433). No check ties it to the lock or add_requires. Impact is limited to doc staleness.

### BUILD-ENV-08: Container images and the Pixi download rely on network trust beyond pins

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** supply-chain · **Status:** new · **Reviewer confidence:** low
- **Where:** `containers/msvc.Containerfile:18`

`git clone https://github.com/mstorsjo/msvc-wine` then `checkout` of a SHA is good, and the Pixi tarball is checksummed. But `apt-get install wine wine64 winbind msitools` is not version-pinned (only the snapshot date), and the msvc-wine script downloads Microsoft's MSVC and SDK from Microsoft's manifests at `project_n image` time. The Microsoft side is verified only by the hashes in the manifest, which is itself fetched live. No pin or hash of the manifest is kept here.

**Impact:** The MSVC and SDK content can change under the same `MSVC_VERSION` and `SDK_VERSION` strings, so the msvc artefacts' reproducibility is not guaranteed. A compromised or changed channel manifest would go unnoticed.

**Recommendation:** Record the expected hash of the downloaded MSVC and SDK payload, or of the channel manifest, in the image inputs. Fail if they differ.

**Verification:** Wine and its apt packages are pinned by the apt snapshot date (design 0001 line 209), so the 'unpinned apt' claim is wrong. The msvc-wine commit and the MSVC/SDK versions are pinned (containers/AGENTS.md:9). The Microsoft payload comes from the live manifest (vsdownload.py in MSVC_INSTALL), and the manifest is not hashed. Version strings can drift at the patch level, and project_n.py:2283 records the resolved versions in the artefact manifest.json, which partly mitigates it.

### BUILD-ENV-09: text=auto can rewrite binary fuzz corpus and replay seeds

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** hygiene · **Status:** new · **Reviewer confidence:** low
- **Where:** `.gitattributes:2`

`* text=auto eol=lf` is the default. Only png, jpg, spv and dxil are marked binary. Fuzz corpus seeds in tests/fuzz/corpus, saves (zstd), replay files and audio (wav/ogg) rely on Git's content sniffing. A seed that has no NUL byte but holds CRLF pairs can be classified as text, and its CRLF would be normalised on commit.

**Impact:** A fuzz seed or fixture would be altered between a developer's checkout and the repo, so a reproduced crash could stop reproducing. Windows CI would see different bytes. The `.msl` text shaders are correctly hashed because they are LF.

**Recommendation:** Add `tests/fuzz/corpus/** -text` (or binary) and mark other binary types (`*.wav`, `*.ogg`, `*.zst`, `*.bin`) explicitly.

**Verification:** .gitattributes is `* text=auto eol=lf`. Only png, jpg, spv and dxil are binary, and otf and ttf are -text. git check-attr on tests/fuzz/corpus/replay/seed shows `text: auto`, so the claim is accurate. Git's auto detection treats a file with NULs, or with lone CR, as binary, but a seed of pure text CRLF would be normalised. That is plausible for the yaml and save fuzz targets. It is still hypothetical: no concrete seed is shown to be affected. Marking the corpus -text would be cheap hardening.

### BUILD-ENV-10: Wine test runner uses the host's default prefix and is silent on setup failures

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** tests · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/xmake.lua:5`

`test_under_wine` calls `wine` found on the host PATH with no WINEPREFIX, no WINEDEBUG and no timeout, outside the container. The comment says the point is no reliance on host binfmt, yet the Wine prefix is `~/.wine`, shared between all checkouts and with the user's other Wine use. `try = true` with a missing `wine` raises through `assert`. Output goes to files under the autogendir, which are removed before a run.

**Impact:** Hangs or prefix corruption in Wine stall the gate without a timeout. Host state affects results, so a test can pass on one machine and fail on another.

**Recommendation:** Run with a project-local `WINEPREFIX` and `WINEDEBUG=-all`, and a timeout (xmake `os.execv` option). Document the prefix initialisation.

**Verification:** tests/xmake.lua test_under_wine uses find_tool('wine'), with no WINEPREFIX, WINEDEBUG or timeout, as stated. It runs only for windows-msvc tests built on Linux, which the design says normally run in the container (a fresh HOME). A host run uses ~/.wine. The failure scenario is real but narrow, since native Windows is authoritative (design 0001 line 65). The missing-wine assert is a deliberate loud failure rather than a silent one.
