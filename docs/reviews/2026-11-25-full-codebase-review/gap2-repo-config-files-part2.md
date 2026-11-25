# Review: gap2-repo-config-files (part 2)

### GAP2-REPO-CONFIG-FILES-11: dev wrapper breaks under a symlink and passes the caller's cwd, not the repo, to project_n

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** tooling · **Status:** new · **Reviewer confidence:** low
- **Where:** `dev:3`

`exec pixi run --manifest-path "$(dirname "$0")/pixi.toml" project_n "$@"`. `$0` is the symlink path when `dev` is linked into ~/bin, so dirname points at the link's directory and pixi.toml is not found. `exec` and the quoted "$@" preserve exit codes and arguments at the shell level. Whether `pixi run` re-splits arguments containing spaces or shell metacharacters (it passes them through its task shell) was not verified. The comment claims 'from any directory', which holds for the manifest but project_n.py must itself use ROOT rather than the cwd for relative arguments such as `--range` or file paths.

**Impact:** A symlinked wrapper fails with a pixi manifest error. Paths given relative to the caller's cwd may be resolved against the wrong directory.

**Recommendation:** Resolve with `dir=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)` (readlink -f is not on stock macOS; use a loop or `realpath`). Add a one-line note on relative path arguments. Test `./dev lint --range "a..b"` and an argument with a space.

**Verification:** dev is `exec pixi run --manifest-path "$(dirname "$0")/pixi.toml" project_n "$@"`. A symlinked $0 would indeed break it, but nothing in the repo or AGENTS.md says to symlink it (it is invoked as ./dev). The cwd/relative-path worry is speculative and was unverified by the finding itself. Exec and quoted "$@" preserve exit codes and args.

### GAP2-REPO-CONFIG-FILES-12: dev.cmd: %* mangles & ^ % in arguments and no explicit exit code

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** tooling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `dev.cmd:2`

`@pixi run --manifest-path "%~dp0pixi.toml" project_n %*`. cmd.exe expands `%*` before parsing, so unquoted `&`, `|`, `^` and `%VAR%` in arguments are interpreted by cmd. The batch file's exit code is that of its last command, so pixi's code is propagated; if pixi is itself a .cmd shim (it is an exe), `call` would be needed. `%~dp0` ends in a backslash and is inside quotes, which cmd handles, but a path ending in `\"` can confuse some programs.

**Impact:** Commit-range or message arguments containing `^` or `&` misbehave on Windows. Low likelihood for this tool's arguments.

**Recommendation:** Document the limitation or add `exit /b %ERRORLEVEL%`; recommend running through `pixi run project_n` for exotic arguments.

**Verification:** dev.cmd is `@pixi run --manifest-path "%~dp0pixi.toml" project_n %*`. cmd does interpret unquoted & ^ in %*, which is standard batch behaviour. Exit code propagates since pixi is the last command, so the 'no explicit exit code' point is no defect. AGENTS.md already offers `pixi run project_n` as the alternative. Negligible.

### GAP2-REPO-CONFIG-FILES-13: data/module.dox claims readers 'never throw' but manifest.cpp and world_map.cpp catch YAML::Exception inside

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** stale-doc · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/data/module.dox:1`

The doc says the readers return errors with the line they were found on and never throw. data/manifest.cpp:202 and data/world_map.cpp:44 catch `YAML::Exception`, so the claim holds at the public boundary only if every other reader does the same. The first sentence is a 150-character line running past the 128-column limit used elsewhere (Doxygen comments are not clang-format-wrapped). The doc also says data reads 'into core's types', while data/game_data.hpp, data/names.hpp and data/manifest.hpp are their own types (core/game_data.hpp exists in core).

**Impact:** Minor. A reader may assume no try/catch exists in data; a new reader that forgets the catch would escape an exception to the logic thread.

**Recommendation:** Reword to 'throw nothing across the module boundary: yaml-cpp's exceptions are caught in the readers', and wrap the line. Pair with a lint check that every `YAML::Load*` call sits inside a try.

**Verification:** data/module.dox says the readers 'never throw'. Every src/data/*.cpp reader I grepped (animations, game_data, names, manifest, settings, abilities, seals, world_map, world_look, yaml, effects) catches YAML::Exception, so the claim holds in practice, and the inner catches at manifest.cpp:202 and world_map.cpp:44 are the boundary catches that make it true. The finding's 'claim is doubtful' framing is mostly refuted. Wording about 'core's types' versus data's own types is a nit.

### GAP2-REPO-CONFIG-FILES-14: platform vs core both own a paths module

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/platform/module.dox:1`

platform/module.dox says platform covers 'window, events, time and paths, over SDL', and core/module.dox says core links no SDL. Both src/core/paths.{cpp,hpp} and src/platform/paths.{cpp,hpp} exist, as do core timing and platform/clock. The module descriptions do not say which owns which half (pure path computation in core, SDL base paths in platform is the likely split).

**Impact:** Confusing placement for new code; risk of duplication.

**Recommendation:** State the split in each module.dox ('core: path values, platform: where SDL says they are') or merge.

**Verification:** Both src/core/paths.{cpp,hpp} and src/platform/paths.{cpp,hpp} exist, and platform/module.dox says only 'The window, events, time and paths, over SDL.' The split is not described. Whether it is duplication was not shown. Placement is a doc clarity nit.

### GAP2-REPO-CONFIG-FILES-15: CHANGELOG.md is 839 commits behind and the header says 'Do not edit'

- **Severity:** info · **Verdict:** deliberate · **Category:** docs · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `CHANGELOG.md:3`

Only 0.1.0 (2026-10-31) exists in the file; the tag v0.1.0 is the only tag and `git log v0.1.0..HEAD` has 839 commits. The file's 163 lines are regenerated by git-cliff at release (chore(release) commits), so this is by design. The sampled entries include early items like 'Make the hello window clang-tidy clean', and the Features group ('Cap frames at 60 per second when vsync is unavailable') describes behaviour that has since changed.

**Impact:** Anyone reading CHANGELOG.md on main sees none of the game's actual features.

**Recommendation:** Add an 'Unreleased' section generated by `git cliff --unreleased` into release notes, or say in README/AGENTS.md that the file only changes at release. No action needed for the gate.

**Verification:** CHANGELOG.md header says 'generated by git-cliff from commit messages. Do not edit.' Only the v0.1.0 tag exists, and the file is regenerated at release by design (release skill). The finding itself marks it known-deferred.

### GAP2-REPO-CONFIG-FILES-16: marketplace.json declares an owner name and no version or metadata

- **Severity:** info · **Verdict:** confirmed · **Category:** tooling · **Status:** new · **Reviewer confidence:** low
- **Where:** `.claude/marketplace/.claude-plugin/marketplace.json:1`

The manifest lists one plugin, clangd, with source ./plugins/clangd. It has no `metadata.version` or `description` for the marketplace, only a plugin description. The `owner.name` is the project name, not a person (fine). The earlier unit covered .lsp.json, so I only checked that the source path exists (plugins/clangd is present).

**Impact:** None functional.

**Recommendation:** No change needed; optionally add a marketplace-level version so plugin updates are detectable.

**Verification:** The finding says no change is needed. The file is a minimal marketplace manifest and nothing functional is wrong. Not an actionable defect.
