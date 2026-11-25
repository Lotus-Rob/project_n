# tools/: build tooling

## Scope
`project_n.py` (the entry point, run as `pixi run project_n` or the root `dev`/`dev.cmd` wrappers), its tests, the Pixi activation script and the gdb init file.

## Rules
- `project_n.py` is one file, standard library only, and thin: commands call XMake, Podman, git, git-cliff, gh and the pinned tools; anything those tools do stays there.
- `dev` and `dev.cmd` only forward to `pixi run project_n`; behaviour belongs in project_n.py.
- `PRESETS`, `IMAGES` and `SCOPES` at the top of the file are the single source of truth. Changing a preset or scope updates design doc 0001 §3/§11 and any AGENTS.md that names it.
- Containers re-enter the same script (`reenter`): add behaviour once, not per environment.
- Never pipe XMake's output through filters; it stalled twice that way (spike S4). Call XMake only through `run()`/`capture()`, never bare: they pass `-P <ROOT>` and restore `xmake-requires.lock`.
- `doctor --docs` checks the agent docs: every quoted command exists, every AGENTS.md below the root is ≤ 8,192 bytes with no line over 300 characters, a SKILL.md ≤ 200 lines, an agent brief ≤ 60 (`doc_budget_problems`); `selftest` runs it on the real tree.
- `env/activate.sh` runs on every `pixi run`: keep it fast and idempotent.
- `lint` refuses design references in code comments (`design_references`), in every comment kind except `*.md`, `docs/`, generated files, the manifest and fuzz corpora. A comment it flags wrongly narrows the pattern, never rewords the comment → the comment above `DESIGN_REFERENCE_PATTERNS`.
- `--help` is read by shell completers (lig): an option with choices opens its help with `[possible values: a, b]`, on one line; `test --list-tests [--suite S]` prints what `--test` takes.
- The cold rule: CI builds on main and tags compile from source (`cold_ref`); a build writes only into an empty object-cache folder → `configure`'s docstring.
- XMake rewrites `xmake-requires.lock` on every call (order, and only the entries the configuration uses); `run` writes the old bytes back unless an entry was added or changed, then it prints the diff: commit it → `restore_lock_unless_added`.
- No compile cache is shared between checkouts, on purpose; the record is in `docs/superpowers/specs/2026-11-13-ci-and-local-speed-design.md` (Outcome, spikes), the why at `BUILD_CACHE_ENV`.
- XMake's build cache is off for GCC everywhere (a stale object after a change to a header the PCH includes) and for MSVC off Windows → `xmake.lua`, `install_cl_wrapper`.

## Commands
- `pixi run project_n selftest` runs the self-checks in `test_project_n.py` (no Podman needed); `check` runs them first.
- `pixi run project_n test --suite <s>`: a suite's file tests run in parallel with no `--test` or doctest arguments, else its binary runs once; fuzz suites and test programs differ → `check_test_target`, `cmd_test`.
- `pixi run project_n prune-worktrees`: removes this project's merged worktrees and their branches, keeping the rest and saying why → `prune_worktrees`.
- `pixi run project_n prune-build-cache` (CI, before each Save objects step) keeps only the cache files this run's builds used and writes `save=true|false` to `GITHUB_OUTPUT` → `cmd_prune_build_cache`.
- `pixi run project_n prune-ci-caches` (CI's cleanup job, `GH_TOKEN`): keeps the newest Actions cache entry per family and version of `GITHUB_REF`, deletes the rest; a 403 prints one line and exits 0 → `cmd_prune_ci_caches`.
- `pixi run project_n measure [--runs world,burst,swords,seals] [--keep]`: smoothness and cost numbers as one table under Tracy; never a gate (not in `check` or CI), it takes the display, run it alone → `cmd_measure`.
- `pixi run project_n clean --stale [--days N]`, also at the start of `build`, `test` and `check` (never in CI or a container): removes build trees unused for `STALE_DAYS` (7), never the native preset's debug and release → `sweep_build`, `sweep_here`.
- `pixi run project_n setup`: once per clone (and per worktree); writes `CLONE_CONFIG` (commit hooks, Git LFS's filters through `pixi run git-lfs`) and fetches the LFS files (design doc 0001.1); `check` refuses a clone without it.

## Skills
- git-commit (for `SCOPES` changes)

## Gotchas
- MSVC `C1382` ("the PCH file has been rebuilt since ...obj") after a package's include paths change: XMake rebuilt the PCH but not the objects using it. `pixi run project_n clean --preset windows-msvc` and build again.
- Leave the tree alone while `check` runs: it lints and builds what is on disk at each step, so a file written meanwhile fails clang-tidy falsely and hides the real failures after it → `cmd_check`.
- `lint` runs one clang-tidy per unit (`tidy_units`), cached by `tidy_key` (cold rule: main and tags read none); its units are the changed `.cpp` files and those whose XMake records name a changed file (`lint_units`).
- `reference` runs each case in its own process with its share of the cores and a timeout (`run_references`, `reference_share`, `REFERENCE_CASE_TIMEOUT`).
- XMake takes an ancestor project over the current directory, and `utils.bin2c` still warns in a worktree's shader step ("we are building the project(<main checkout>)…"): benign → `resolve`.
- `xmake f -v` is on purpose (it serialises package installs) → `configure`. `doctor` exits 3 for "problems found" → `DOCTOR_PROBLEMS`.
- A worktree outside the repo fails to check out (Git LFS's filter needs Pixi to find `pixi.toml` further up) → the root AGENTS.md.
