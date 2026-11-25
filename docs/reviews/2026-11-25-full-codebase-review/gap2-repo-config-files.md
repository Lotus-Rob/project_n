# Review: gap2-repo-config-files

**Scope:** Review the tracked root and tooling config files no unit's scope listed: .clang-tidy, .clangd, .clang-format, .editorconfig, dev and dev.cmd, docs/Doxyfile, the six src/*/module.dox files (core's was already judged stale; check the other five against their modules), CHANGELOG.md, and .claude/marketplace/.claude-plugin/marketplace.json. For .clang-tidy (the user owns it: never propose relaxing or removing a check), report whether its HeaderFilterRegex, ExtraArgs and CheckOptions actually do what their comments say, and which disabled checks or missing options let through the bug classes this review found (float-to-integer UB casts, unchecked narrowing, .at() throws, enum-count drift, exception escape from noexcept or job bodies). Propose additions only. For .clangd and .editorconfig, check consistency with .clang-format and the generated-file exclusions. For dev and dev.cmd, check argument and quoting pass-through, exit codes and running from any directory. For Doxyfile, check its INPUT and EXCLUDE (generated headers, tests), its warning settings, and whether `project_n docs` would fail on undocumented or broken references. Earlier units covered the tests/.clang-tidy file, the lint driver in tools/project_n.py and the clangd plugin's .lsp.json; don't repeat those. (Why this unit exists: git ls-files shows these tracked files outside every unit's scope (build-env listed specific root files but not the lint, format, editor or Doxygen config or the dev wrappers). The .clang-tidy profile is the main static safety net behind the 'fix lint properly' rule, and nobody checked whether it is set up to catch the UB and narrowing classes the cross-cutting units kept finding.)

**Findings:** 16 standing (0 critical, 0 high, 1 medium, 8 low, 7 info), 0 raised but refuted on verification.

**Parts:** gap2-repo-config-files.md, gap2-repo-config-files-part2.md

## Assessment

The repo's config files are coherent overall. The tidy profile is broad (bugprone, clang-analyzer, cppcoreguidelines, misc, modernize, performance, portability, readability, concurrency) and runs with WarningsAsErrors. The build adds -Weverything, -Werror, hardened standard libraries and a Doxygen gate on headers.

The gaps are in what none of these catch:
- Runtime float-to-integer UB. Explicit casts after floor/lround hide it from -Wconversion, and clang's `-fsanitize=undefined` does not include float-cast-overflow.
- Exceptions escaping noexcept functions or job bodies, because bugprone-exception-escape is disabled. This is a user decision, so I only suggest a scoped alternative.
- Enum-count drift. clang-tidy has no check for it, so it needs static_asserts, plus -Wswitch-enum (in -Weverything on clang only; GCC has -Wswitch-enum but it is not in the build).
- CERT-family checks, which are not enabled at all.

Stale or misleading items:
- The module.dox files are mostly accurate. gfx's feature list omits line_ribbons, and data's "never throw" is only true at the public API boundary.
- CHANGELOG.md stops at 0.1.0 while 839 commits have landed since the tag. It is regenerated at release, so this is expected.
- The dev wrappers are fine apart from symlink and `%*` edge cases.
- .editorconfig contradicts .gitattributes on dev.cmd's line endings.

I did not run clang-tidy, doxygen or any build (read-only rules). The pixi quoting concern and the Doxygen full-run claim are therefore from reading, not execution.

**Files read in full:** (not reported)

## Findings

### GAP2-REPO-CONFIG-FILES-01: No check or sanitizer catches float-to-integer cast UB

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** static-analysis-gap · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.clang-tidy:1`

The code has many `static_cast<iNN>(std::floor/lround(...))` and `static_cast<u32>(std::lround(...))` patterns (core/world_map.cpp:25, core/ui_colours.cpp:31 and 68, core/display_delay.cpp:11). Explicit casts silence -Wimplicit-float-conversion and bugprone-narrowing-conversions. clang-tidy has no check for NaN or out-of-range double-to-int conversion. The sanitizer build uses XMake's policy build.sanitizer.undefined, which maps to -fsanitize=undefined; on clang that group excludes float-cast-overflow, and GCC's UBSan does not check it by default either.

**Impact:** A NaN or huge coordinate from data, a replay or a fuzz input turns into UB (an unspecified value on x86, different elsewhere). This is the exact bug class the cross-cutting units found, and nothing in the gate catches it.

**Recommendation:** Add `-fsanitize=float-cast-overflow` (clang) alongside the `undefined` sanitizer in xmake.lua. Propose cert-flp30-c, and bugprone-narrowing-conversions with `WarnOnFloatingPointNarrowingConversion` and `WarnWithinTemplateInstantiation` on, in .clang-tidy. In the longer term, route all float-to-int through one checked helper in core/math.hpp and add a custom lint grep for raw `static_cast<` of a `floor`/`lround`/`llround` result outside it.

**Verification:** Gap is real: xmake.lua only maps the sanitizer option to policy build.sanitizer.<name> (lines 12-18) and has no float-cast-overflow flag; that check is outside -fsanitize=undefined on both clang and GCC. But the recommendation is partly wrong: cert-flp30-c is about floating-point loop counters, not float-to-int casts, so it would catch nothing here. bugprone-narrowing-conversions is already enabled via bugprone-*, with the float option on by default, though explicit static_cast bypasses it. Sanitizers are also on request only, so the float-cast-overflow flag would be a partial net. The checked-helper plus lint-grep idea is the sound part. Severity high is overstated.

### GAP2-REPO-CONFIG-FILES-02: bugprone-exception-escape disabled with no replacement for noexcept and job bodies

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** static-analysis-gap · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `.clang-tidy:14`

`-bugprone-exception-escape` is off. core/jobs.hpp hides enkiTS behind a pimpl and job bodies run on worker threads, where an escaping exception calls std::terminate with no context. The code base has only 15 `noexcept` uses, and throws are rare (main.cpp and log.cpp catch). Runtime throwers remain: ~183 `.at(` calls (mostly std::array::at in core headers, e.g. glow.hpp:276 and manifest.hpp:91) and any std::bad_alloc/vector growth.

**Impact:** A throw inside a job or logic-thread callback aborts the process without going through the main.cpp catch handler. Nothing statically warns when an `.at()` or a throwing call is reached from a job body.

**Recommendation:** Keep the disable, since the user owns it, and add something else: `cppcoreguidelines-avoid-do-while`-style checks do not apply. Consider an additive `bugprone-exception-escape` scoped to jobs via a nested .clang-tidy with `FunctionsThatShouldNotThrow` listing the job entry points. At minimum add a `fno-exceptions`-free lint rule: forbid `.at(` in src outside tests, since `-D_GLIBCXX_ASSERTIONS` and hardening already bounds-check `operator[]` in dev builds.

**Verification:** .clang-tidy line 14 does disable -bugprone-exception-escape, and the user owns it; the finding only proposes additions, so the 'contradicts-recorded-decision' status is unwarranted. The scenario (throw in a job body calls terminate) is plausible, but I did not verify the .at( count or the job entry points. The nested-config idea with FunctionsThatShouldNotThrow is a valid additive proposal. The 'forbid .at(' lint rule is a style opinion, and the sentence about do-while checks is noise. Medium is high for a proposal.

### GAP2-REPO-CONFIG-FILES-03: cert-* family and several other relevant checks are not enabled

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** static-analysis-gap · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.clang-tidy:2`

Enabled families: bugprone, clang-analyzer, concurrency, cppcoreguidelines, misc, modernize, performance, portability, readability. Missing: cert-* (cert-flp30-c, cert-err33-c for unchecked return values, cert-err58-cpp for throwing static initialisers, cert-msc50/51 for rand and seeding, cert-dcl50), hicpp-multiway-paths-covered, hicpp-exception-baseclass, hicpp-no-assembler, google-readability-casting, and the abseil-* checks. Four `-hicpp-*` entries in the disable list refer to a family that is never enabled, so they are dead lines.

**Impact:** Unchecked return values from SDL, zstd or file I/O, and non-deterministic randomness in the simulation (rand/random_device), are not statically flagged, although determinism and error handling are core concerns.

**Recommendation:** Add `cert-*` (disable per-check only where noisy) and `hicpp-multiway-paths-covered`, `hicpp-exception-baseclass`. Consider `bugprone-unused-return-value` options and `misc-include-cleaner` with MissingIncludes later. Remove or keep the dead `-hicpp-*` lines for clarity.

**Verification:** The Checks list enables bugprone, clang-analyzer, concurrency, cppcoreguidelines, misc, modernize, performance, portability and readability. No cert-* or hicpp-* family is enabled, so the four -hicpp-* disables (signed-bitwise, special-member-functions, uppercase-literal-suffix, vararg) are dead lines. bugprone-unused-return-value is already on, which covers part of the cert-err33-c claim. Adding cert-* is a reasonable proposal; the impact is speculative.

### GAP2-REPO-CONFIG-FILES-04: Enum-count drift has no static guard

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** static-analysis-gap · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.clang-tidy:2`

Per-enum tables are indexed with `std::to_underlying(...)` (manifest.hpp:91, glow.hpp:276). The profile has `bugprone-switch-missing-default-case` but no default-less switch enforcement. -Wswitch-enum comes with clang's -Weverything. GCC has -Wswitch-enum but the set_warnings("everything") flags do not turn it on, and no check ties a table's size to the enum's COUNT.

**Impact:** Adding an enumerator without extending a lookup table compiles on GCC and yields an out-of-range `.at()` throw or silent wrong data at runtime.

**Recommendation:** Add `-Wswitch-enum -Wcovered-switch-default` explicitly for GCC in xmake.lua (flag set only, not clang-tidy), and require `static_assert(TABLE.size() == std::to_underlying(Enum::COUNT))` next to each table. `readability-enum-initial-value` and `bugprone-switch-missing-default-case` are already on; `misc-use-internal-linkage` helps little here.

**Verification:** xmake.lua line 80 uses set_warnings("everything","pedantic","error"). On clang -Weverything includes -Wswitch-enum. On GCC, -Wswitch-enum is not part of the warning level, so GCC has the gap. The static_assert(size == COUNT) suggestion is sound and cheap. I did not check which tables already have such asserts. The mitigation is partial since clang builds already catch switches, so the impact is overstated.

### GAP2-REPO-CONFIG-FILES-05: HeaderFilterRegex 'src/.*' is unanchored and includes generated headers

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** config-accuracy · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.clang-tidy:42`

The regex has no anchor, so it matches any header path containing `src/` (src/gfx/generated/*.hpp, and any library header whose path contains /src/, such as package include dirs). tools/project_n.py keeps an IGNORED_FINDINGS list for exactly those library hits, which shows the regex is broader than the intended our-code-only reading. AGENTS.md says lint skips generated/, but a finding inside a generated header included from a linted .cpp is still reported through the filter.

**Impact:** Findings in generated headers or third-party headers can fail lint and force growth of IGNORED_FINDINGS. A rename of the source root breaks the filter silently.

**Recommendation:** Use `'(^|.*/)src/(?!.*/generated/).*'` if the clang-tidy regex engine supports lookahead (it does not; POSIX ERE), otherwise anchor with `.*/project_n/src/.*` via lint passing `--header-filter` per run, and put a `// NOLINTBEGIN` guard in the generators' output template. Document in the comment above that generated headers are included.

**Verification:** HeaderFilterRegex 'src/.*' is unanchored, and IGNORED_FINDINGS (tools/project_n.py:1358) does hold one Quill header hit, consistent with the claim. Generated headers such as src/gfx/generated/shaders.hpp contain only array and span declarations, and fmt and lint skip them as files per project_n.py:1597. A finding inside one via an include is possible but unlikely. The recommendation contradicts itself: it proposes a lookahead and then admits clang-tidy's regex lacks it. Real impact is minimal.

### GAP2-REPO-CONFIG-FILES-06: .clangd diagnostics are weaker than the build and lint

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** consistency · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.clangd:1`

The build uses `-Weverything -Werror` (clang). .clangd adds only `-Wall -Wextra -Werror -Wpedantic` and hardcodes `-std=c++23`, `-Isrc/` and `-Wno-c99-designator`. CompilationDatabase is `build`, the directory that `build/compile_commands.json` is written to by native builds (the compile_commands path is stated in AGENTS.md). `Add` flags are appended even when the database entry already has stricter flags, so a clangd editor session reports fewer warnings than the build fails on. When the database comes from a GCC preset, GCC-only flags (-Winvalid-pch, -fno-... ) reach clangd unless `Remove` is set. UnusedIncludes: Strict has no IgnoreHeaders mirroring clang-tidy's misc-include-cleaner IgnoreHeaders (yaml-cpp, SDL3, imgui umbrella headers), so clangd marks those umbrella includes as unused.

**Impact:** The editor flags umbrella includes as unused that lint accepts, and misses -Weverything warnings. Developers see inconsistent signals.

**Recommendation:** Add `Diagnostics.Includes.IgnoreHeader: [yaml-cpp/yaml\.h, SDL3/SDL\.h, imgui\.h]` and `CompileFlags.Remove: [-Winvalid-pch, -fno-*]` for GCC databases. Comment why `-Wno-c99-designator` is needed. Do not change the user's file without asking; this is a proposal.

**Verification:** .clangd does add -Wall -Wextra -Werror -Wpedantic over the database, whose entries already carry -Weverything, so the claim that clangd reports fewer warnings than the build is wrong: Add appends to the database flags and does not replace them. Real, minor points: UnusedIncludes: Strict has no IgnoreHeader mirroring misc-include-cleaner.IgnoreHeaders (yaml-cpp, SDL3, imgui), and -Wno-c99-designator is uncommented. GCC-database flag leakage is speculative, since the plugin runs clangd on a clang database.

### GAP2-REPO-CONFIG-FILES-07: .editorconfig says LF for every file, but dev.cmd is committed with CRLF

- **Severity:** low · **Verdict:** confirmed · **Category:** consistency · **Status:** new · **Reviewer confidence:** high
- **Where:** `.editorconfig:3`

`end_of_line = lf` applies to `[*]`. .gitattributes sets `*.cmd text eol=crlf` because cmd.exe misparses LF-only batch files. An editor following .editorconfig will rewrite dev.cmd to LF, which the .gitattributes comment says breaks it. Also no section for generated files (src/*/generated/, shaders/bin/): .editorconfig and .clang-format have no exclusion, which is consistent with .clang-format being run by project_n over a file list (generated/ skipped there), but an editor with format-on-save would reformat generated headers. [*.md] drops trim_trailing_whitespace but .md files under docs/ are checked by no whitespace rule.

**Impact:** A Windows contributor saving dev.cmd in an editor honouring .editorconfig corrupts the wrapper.

**Recommendation:** Add `[*.{cmd,bat}] end_of_line = crlf` and `[src/*/generated/**] ` with `generated_code = true` (honoured by some editors) or `insert_final_newline = unset`. Add `DisableFormat: true` via a nested src/*/generated/.clang-format so editor format-on-save leaves generated headers alone.

**Verification:** .editorconfig [*] sets end_of_line=lf with no override for *.cmd. .gitattributes line 3 sets '*.cmd text eol=crlf', and git ls-files --eol shows dev.cmd as i/lf w/crlf, so the worktree copy is CRLF. An editor honouring .editorconfig would save it as LF, which the gitattributes comment says breaks cmd.exe. Adding [*.{cmd,bat}] end_of_line=crlf is a valid fix. The generated-file parts are speculative; fmt and lint skip generated/.

### GAP2-REPO-CONFIG-FILES-08: Doxyfile INPUT covers src/*/generated and internal/ headers; only pch.hpp is excluded

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** docs-config · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/Doxyfile:11`

INPUT = src with RECURSIVE and FILE_PATTERNS `*.hpp *.dox`; EXCLUDE_PATTERNS is only `*/pch.hpp`. So `project_n docs` (cmd_docs, project_n.py:2433) documents src/*/generated/ headers (atlas.hpp, shaders.hpp) and gfx/internal/ headers, and WARN_AS_ERROR=FAIL_ON_WARNINGS then fails the whole docs run on any undocumented generated symbol. The `lint` path only passes changed headers, so a full `project_n docs` can fail where lint passed (a header that stopped being changed, or a generated one). Tests are not in INPUT (good). src/build_info.hpp.in is not matched. WARN_NO_PARAMDOC is unset (default NO), so undocumented function parameters are not reported; WARN_IF_DOC_ERROR and WARN_IF_INCOMPLETE_DOC keep their defaults (YES). Doxygen 1.18 treats FAIL_ON_WARNINGS as fail after processing the run, which is the intended behaviour.

**Impact:** The published reference mixes internal and generated headers into module groups; a generated-header warning can fail `docs` while lint is green.

**Recommendation:** Add `*/generated/*` and `*/internal/*` (if those are not meant as public API) to EXCLUDE_PATTERNS, or ensure generators emit `/// @file` and docs for every symbol. Consider `WARN_NO_PARAMDOC = YES` once headers are clean. Add a CI step running the full `project_n docs` so the whole-tree gate exists, not only the changed-file one.

**Verification:** Doxyfile: INPUT=src, RECURSIVE, EXCLUDE_PATTERNS only */pch.hpp, so generated and internal/ headers are included, as stated. WARN_AS_ERROR=FAIL_ON_WARNINGS is set. Whether generated headers actually warn was not shown (the finding is hypothetical there). Including internal/ headers is plausibly intended: the module.dox files describe internal/features. WARN_NO_PARAMDOC unset is true. Whole-tree vs changed-file gap is plausible but unproven.

### GAP2-REPO-CONFIG-FILES-09: gfx/module.dox feature list omits line_ribbons and is hard to maintain

- **Severity:** low · **Verdict:** confirmed · **Category:** stale-doc · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/module.dox:1`

The group description lists features 'sprites, meshes, ribbons, seals, the world map's ground and rivers, the composite', but src/gfx/internal/features/ also contains line_ribbons.{cpp,hpp}. It also hard-wires details (seal layout on the CPU, SealParticleAt mirrored in C++ with shaders/seal.hlsli) that are accurate today (SealParticleAt exists at gfx/seal_draw.hpp:362) but will go stale with the planned 'seal layout on GPU' item. The comment text is 8 lines of one very long wrapped paragraph; the first sentence reads 'the device, the swapchain, pipelines and frames' but the group also holds camera, text layout, shelf packer and image decode.

**Impact:** The Doxygen module page misdescribes the module.

**Recommendation:** List line_ribbons, shorten the group description to the module's responsibility and move the seal pipeline detail into seal_draw.hpp's file comment.

**Verification:** src/gfx/module.dox lists 'sprites, meshes, ribbons, seals, the world map's ground and rivers, the composite' while src/gfx/internal/features/ also holds line_ribbons.{cpp,hpp}. The omission is real. The rest of the finding (maintainability, the intro sentence) is opinion.

### GAP2-REPO-CONFIG-FILES-10: ExtraArgsBefore -Wno-unknown-pragmas hides pragma typos, with no comment

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** config-accuracy · **Status:** new · **Reviewer confidence:** low
- **Where:** `.clang-tidy:41`

`ExtraArgsBefore: ['-Wno-unknown-warning-option','-Wno-unknown-pragmas']` has no explanatory comment, while the other options do. The build uses -Weverything/-Werror, so -Wunknown-pragmas is active in real compilations, yet clang-tidy mutes it. Cases like `#pragma GCC diagnostic` for a clang-only warning name need the first flag; the second is probably for MSVC `#pragma warning` in compile commands. -Wno-unknown-warning-option is also set in .clangd Add.

**Impact:** Harmless in practice, since the compile step still catches them, but it is unexplained. In clangd, `-Wno-unknown-warning-option` is last in the list while others are `-Wall -Wextra -Werror`, so a mistyped `#pragma clang diagnostic` only errors in the real build.

**Recommendation:** Add a `#` comment saying why each flag is there (MSVC and GCC-only pragmas in shared headers) and drop the flag that is not needed. ExtraArgsBefore is applied before the compile database flags, so it is dead for any flag the database sets later; verify each still has an effect.

**Verification:** ExtraArgsBefore at .clang-tidy line 41 has no comment while the CheckOptions do. The finding itself says the impact is harmless and confidence low, and it is a cosmetic suggestion. The claim that ExtraArgsBefore is dead for flags the compile database sets later is speculative: -Wno-* flags placed before are overridden only by the same flag, and the build's own -Weverything does not conflict with a -Wno- before it.
