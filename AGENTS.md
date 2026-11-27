# project_n: agent guide

C++23 game (headers, no modules) built with XMake; tests use doctest.
Every tool comes from the Pixi environment (`pixi.toml` / `pixi.lock`). Never use host compilers or host libraries.
Containers (Podman) are only for artefacts and container checks. Design and rationale: `docs/design/0001-repository-build-environment.md`.

## Map
- `src/` game and engine (src/AGENTS.md) · `tests/` doctest suites (tests/AGENTS.md)
- `tools/project_n.py` the only entry point (tools/AGENTS.md) · `containers/` artefact images (containers/AGENTS.md)
- `shaders/` HLSL, one file per stage (shaders/AGENTS.md); `bin/`: the committed binaries `shaders` writes · `assets/` what the game loads at runtime: sprites, fonts, the atlas (`manifest.yaml`) and `data/`'s YAML tables (which are hashed game data, which presentation, and the C++ each one is read into: assets/AGENTS.md) · `art/` the sprite and scene lists and the art and world-map generators (art/AGENTS.md) · `src/*/generated/`: headers `shaders` and `atlas` write (fmt and lint skip them) · `xmake/repo/` local fixes to xmake-repo packages · `docs/design/` past designs and their rationale: read before redesigning anything · `docs/superpowers/`: `specs/` (designs, `YYYY-MM-DD-<topic>-design.md`) and `plans/` (`YYYY-MM-DD-<feature>.md`), each approved before its code · `windows/` the Windows-only files linked into every windows-msvc executable (`utf8.manifest`: the UTF-8 code page, `utf8_code_page` in xmake.lua)
- `.github/` CI workflows (.github/AGENTS.md)
- `.claude/settings.json` turns superpowers on, hides from the model the skills this repo never uses (`skillOverrides`: still typable as `/name`), denies the Artifact tools (claude.ai pages, unused here; superpowers' local visual companion still works), and swaps the global clangd plugin for `.claude/marketplace/`'s, which starts Pixi's clangd through `pixi run` on every host; the `LSP` tool reads `build/compile_commands.json`, which native builds refresh

## Commands
- `./dev <command>` (Windows `dev`) is shorthand for `pixi run project_n <command>`; either works from anywhere.
- `pixi run project_n setup`: once per clone (commit hooks, Git LFS's filters through Pixi, LFS files; design doc 0001.1) · `pixi run project_n doctor [--docs]` · `build|test|run [--preset linux-gcc|linux-clang|windows-msvc|macos-clang] [--mode debug|release|releasedbg]` (releasedbg: profiling with Tracy; default preset: the host's native one, `NATIVE_PRESET`; `build --preset all` builds every preset and summarises)
- `pixi run project_n clean [--preset P] [--stale [--days N]]`: remove build trees; `--stale` only those unused for 7 days, which `build`, `test` and `check` also do at their start, never the native preset's debug and release (tools/AGENTS.md)
- `pixi run project_n check`: the merge gate (self-tests, commits, format and lint of the diff, build + test every preset, one releasedbg build). A change is done when it passes.
- `pixi run project_n shaders` (Linux or Windows): compile `shaders/` after changing it, and commit `shaders/bin/` and `src/gfx/generated/` with it; `lint` fails on stale binaries
- `pixi run project_n atlas [--placeholders]`: after changing `art/sprites.toml`, `art/scenes.toml` or `assets/sprites/`, commit `assets/manifest.yaml`, `src/core/generated/atlas.hpp` and `art/atlas.sha256` with them; `lint` fails on a stale atlas
- `pixi run python art/world_map.py --roads`: after changing `assets/data/world.yaml`'s grid, rivers, places, `danger_reach` or the power-0 profile, commit its rerouted `roads:` and `fords:` with them; `selftest` fails on stale roads (art/AGENTS.md)
- `pixi run project_n reference [--preset linux-gcc|linux-clang] [--update]`: pictures of fixed scenes against `tests/reference/` on software Vulkan (Linux; part of `check` and `artifact`) → tests/AGENTS.md
- `pixi run project_n coverage` (HTML in build/coverage/) · `fuzz <target> [--time S]` · `docs` (Doxygen, build/docs/) · `profile [--seconds N] [-- <game args>]` (records a releasedbg run with Tracy into build/profiles/; nothing takes the game's focus) · `profiler [file]` (Tracy viewer, live or on a saved capture)
- `pixi run project_n measure [--runs world,burst,swords,seals] [--keep]`: the smoothness and cost numbers (frame percentiles, hitches, input latency, the GPU's share, shimmer) of releasedbg runs fullscreen under Tracy, 2 workers, unpaced, as one table (tools/AGENTS.md); it takes the display, so run it alone; never a gate, not in `check` or CI
- `pixi run project_n check --sanitizers`: also every supported sanitizer; on request only, not part of the gate.
- `pixi run project_n check --container`: the same in clean containers. Run it for any build, environment or dependency change: only a clean container catches host leaks.

## Working with superpowers
The workflow (design, plans, waves, gates, reviews, models per seat, context budget, handover, finishing through a PR, commits, agent docs) is the user's global `~/.claude/CLAUDE.md` and the step rules its hook adds. This repo supplies what those rules refer to:
- Worktrees: inside the repo because Git LFS's filter needs Pixi to find `pixi.toml` further up during the checkout. Setup there is `pixi run project_n setup`, the baseline `pixi run project_n test`; run XMake only through `project_n`.
- Gates: a round runs `pixi run project_n test --suite <s> [--test <t>]`; a task ends with `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD` (native preset); the other targets are `pixi run project_n build --preset linux-gcc --mode release` and `pixi run project_n build --preset windows-msvc --mode release`, in the main checkout (warm build folders; a worktree's would build cold); the merge gate is `pixi run project_n check` (before the PR, and after the final review's fixes); `check --sanitizers` only on request.
- Containers can't see a worktree's shared `.git`, so container gates (`check --container`, `lint --container`) run only from a main checkout; from a worktree, CI's `lint` and `container` jobs on the PR are that gate, and the PR waits for them.
- `review: full` here means game logic, simulation, rendering, determinism, saves and tooling that gives a verdict. Reviewers find callers of changed C++ symbols with `LSP`.
- Tasks touching shaders or the renderer share files, so they stay in order rather than run in one wave.
- "What's next": the Order table in `docs/superpowers/specs/2026-11-25-review-mediums-design.md` (the user, 2026-11-26); answer with the lowest-numbered step not marked done (its areas not yet done) and any later step whose Needs are met, with the project memory's `in-flight.md` for what is open now. A branch that finishes an area marks it implemented in its own PR (the spec's Order says how).
- Extra history source for `recall-history`: the project memory's `sweep-up-backlog.md`; a backlog item a branch closes leaves it once the branch merges.

## Finishing a branch
1. `pixi run project_n lint --fix` (the diff; `--all` for every file). When the pre-commit hook rejects formatting, `pixi run project_n fmt`, stage again and commit.
2. `pixi run project_n check`, plus `--container` for build, environment or dependency changes (from a worktree: CI's container jobs).
3. The PR: `pixi run gh pr create --fill`; main's ruleset (.github/AGENTS.md) needs CI's nine checks green on a branch up to date with main. GitHub deletes the merged branch.
4. After the merge: also drop the closed items from `sweep-up-backlog.md`; from the main checkout run `pixi run project_n prune-worktrees` (it fetches and checks `origin/main` itself; never forced).

## Rules
- Lint suppressions are the last resort, as narrow as possible, when no proper fix exists: `// NOLINT(check-name): why` (`lint` rejects bare ones), a commented pragma, or a nested `.clang-tidy` with a `#` reason. The root `.clang-tidy` and `.clangd` are the user's: never remove, disable or relax a check or option in them. `cppcoreguidelines-*` enforces the C++ Core Guidelines.
- Commits: `feat`/`fix` only for game code; scopes from `SCOPES` in tools/project_n.py → git-commit skill
- Bugs in `src/` get a test that fails because of the bug (what makes it good: tests/AGENTS.md); a fuzzer crash also adds its input as a seed in `tests/fuzz/corpus/<target>/`.
- Comments: Doxygen (`///`) documents the API; `lint` rejects design-process references (`design_references` in tools/project_n.py).
- UTF-8 is the one text encoding: compilers read sources as UTF-8 (`set_encodings`), Windows executables carry a UTF-8 manifest and the log sets the console's, Python runs with `PYTHONUTF8=1` (pixi.toml), paths become text through `core::PathText`; `lint` rejects other encodings and BOMs.
- Pins are exact (`==` in pixi.toml, exact `add_requires` versions). Change one only on purpose, with its lock file in the same commit.
- Don't call podman, xmake or git-cliff directly in docs or scripts; go through project_n.py.
- Only ever stop or remove containers this project started (`project_n-*` images), never all containers.
- Missing host tool: run `doctor` and give the user its install instructions. Never install system packages yourself.
- Agent docs: `doctor --docs` checks their budgets and quoted commands; the root's CLAUDE.md is `.claude/CLAUDE.md` with `@../AGENTS.md`.

## Skills (.claude/skills)
- git-commit: writing commit messages, splitting changes, breaking changes
- release: cutting a release, versions, changelog, artefacts
- add-dependency: adding, bumping or removing a C++ library (`add_requires`, licence record, lock)
- fix-xmake-package: a package that needs host files or drops a feature; overrides in `xmake/repo/` and their upstream fixes
- bump-toolchain: changing a pinned tool, toolchain, base image or MSVC version (containers/AGENTS.md)
- look-at-the-game: seeing what the game draws, screenshots and picture diffs (src/AGENTS.md)

## Agents (.claude/agents): subagents with a fresh context
- log-triage: the root cause of a long build, test or CI log; hand it logs instead of reading them in the main session

## Skill candidates
| Workflow | Evidence | Proposed skill | Owner |
|---|---|---|---|

## Gotchas
- A clang-tidy finding in a header under `tests/` that only `lint --container` or CI's lint job reports: `.clang-tidy`'s `HeaderFilterRegex: 'src/.*'` matches every header under the container's `/src` source root, but no host path outside `src/`. Run `lint --container` from a main checkout when you change a header under `tests/`.
- GCC release (LTO) links failing with undefined references into a module library: our libraries need `gcc-ar`, which indexes LTO bytecode. xmake.lua's `lto` rule sets it per target, never in the toolchain: XMake hands autotools packages no `ARFLAGS` for `gcc-ar`, so their static archives fail (`ar: invalid option -- '.'`).
