# Code comments without design references: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** No code comment cites the design process (specs, chunks, phases, plans, rulings, tasks, design docs), and
plain comments explain only why; a `lint` check and agent-doc rules keep it that way.

**Architecture:** five comment passes over disjoint file sets, run in parallel; then one tooling task adds the `lint`
check (test-first in `project_n.py`'s self-tests) and the AGENTS.md rules, and proves the tree clean with `lint --all`.
No behaviour changes: the build, tests and reference pictures stay as they are.

**Tech Stack:** C++23, HLSL, Python 3 (`tools/`, `art/`), YAML; doctest; XMake through `project_n`, all from Pixi.

**Status:** approved 2026-11-16 (the user).

**Spec:** none: the user's decisions of 2026-11-16 below are the whole design (a cleanup with no design choices left).
On branch `phase6-chunk5b2`, before its wave C (the user's choice: 5b2's new code has references too, and a separate
branch would conflict with nearly every 5b2 file).

## The user's decisions (2026-11-16)

- "I want to remove all and never leave any references to the design spec in implementation code comments, such as
  "(the phase 6 chunk 4 spec)" and " (chunk 4b)"".
- Asked whether design docs, phases, rulings and task numbers go too: "All of them. Comments outside of doxygen
  documentation should only explain why on specific, targeted parts, not just "what"".
- All code, on 5b2's branch, with a lint rule and AGENTS.md lines so waves C-J never add one.

## Global Constraints

- **Code** means every file that isn't documentation: `src/`, `tests/`, `shaders/`, `tools/`, `art/*.py` and
  `art/*.toml`, `assets/data/*.yaml`, `xmake.lua`, `xmake/`, `.github/workflows/`, `containers/`. **Documentation**
  keeps its references: `*.md` (AGENTS.md, skills, specs, plans, design docs), `docs/`, `CHANGELOG`, `.dox` files only
  if they are module overviews (`src/<module>/module.dox`: treat as code, since Doxygen renders them as the API docs).
  Generated files (`src/*/generated/`, `assets/manifest.yaml`) are written by tools: fix their generator's template,
  then regenerate with `pixi run project_n shaders` / `pixi run project_n atlas`.
- **A reference** is any pointer into the process: "spec", "the phase 6 chunk 4 spec", "chunk 4b", "phase 4",
  "5b1", "5b2", "5b3", "5b-look", "5c", "design doc 0002 §7.6", "0002.3 §9", "§", "ruling", "R6a", "P3", "Task 7",
  "wave C", "the plan", "the ledger", "Outcome", "History", "the brainstorm", "the user's call". A date alone
  ("2026-11-15") in a comment is one too.
- **Replace, don't just delete:** where a reference carried a reason (why a value, why an order), the comment keeps
  the reason in its own words; where it only pointed elsewhere, it goes. Never lose a why.
- **Plain comments** (`//` and `/* */` that aren't Doxygen, `#` in Python and YAML) explain only **why**, on a
  specific, targeted part: a non-obvious reason, a constraint, a workaround with its cause, a tie to something
  elsewhere. A comment that restates what the next lines do goes; a section-heading comment over obvious code goes.
  Keep: `NOLINT(check): why` reasons, pragma reasons, IWYU pragmas, licence headers, `.clang-tidy` `#` reasons.
- **Doxygen** (`///`, `/** */`, `@file`, module.dox) documents the API (what a name is and does, its contract); it
  may say what, but follows the no-reference rule. The why-only rule never applies to it: no Doxygen comment is
  deleted or trimmed for saying what; only its references are reworded. Every file's `@file`, and every class,
  struct, function, method, member, enum and constant keeps its Doxygen (src/AGENTS.md); `lint`'s `@file` and doc
  checks and `pixi run project_n docs` still pass.
- **Test names and messages** are code too: a `TEST_CASE` name or a `CAPTURE`/`INFO` string citing a chunk or ruling
  is renamed in its own words. Fuzz corpus files are data: leave them.
- **No behaviour change:** string literals the program prints or logs stay as they are unless they hold a reference
  (then reword the same way, keeping any test that matches them in step). Code, values and test assertions don't move.
- **Gates (root AGENTS.md):** each pass ends with `pixi run project_n test`, `pixi run project_n lint --range <task
  base>..HEAD`, and `pixi run project_n docs` (Doxygen warnings are errors there) where it touched `///`; the pass
  touching `shaders/` also runs `pixi run project_n shaders` and commits `shaders/bin/` and `src/gfx/generated/` if
  they change (comments shouldn't change binaries: if they do, say so). The branch head runs `pixi run project_n
  check` with 5b2's finishing, not here.
- **Commits:** `style` is for formatting only and `feat`/`fix` for behaviour, so comment rewrites in `src/`,
  `tests/` and `shaders/` are `docs(<scope>): ...` (comments are documentation; scopes from `SCOPES`) and in tooling
  `build(tools): ...` (the git-commit skill: tooling is `build`), one commit per pass, e.g.
  `docs(core): drop design references from core's comments`.

## Review Focus

1. A reason lost: a reference removed where the comment needed its substance (a magic number, an order, a workaround)
   and nothing replaced it.
2. A what-comment kept, or a why-comment removed as if it were a what.
3. Any reference left in the pass's files (the reviewer greps the pattern list above).
4. Anything that isn't a comment changed.

## Waves

| Wave | Tasks | What each needs |
|---|---|---|
| A | 1 (`src/core/`, light), 2 (`src/gfx/`, `shaders/`, light), 3 (`src/game/`, `src/app/`, `src/data/`, `src/platform/`, `src/main.cpp`, every `src/*/module.dox`, light), 4 (`tests/`, light), 5 (`tools/`, `art/`, `assets/data/`, `xmake.lua`, `xmake/`, `.github/workflows/`, `containers/`, light) | nothing; disjoint files (module.dox files all in Task 3) |
| B | 6 (the `lint` check and the agent docs, full) | Tasks 1-5 landed (the check's `--all` run must find nothing) |

Light reviews run on Sonnet; consecutive light tasks of wave A may share one reviewer per two tasks.

---

### Task 1: core's comments

**review: light** (comments only)

**Files:** every file in `src/core/` except `src/core/generated/` (whose generator Task 5 owns: `tools/`).

- [ ] **Step 1:** list the references: `grep -rnEi '\b(spec|specs|chunk|phase [0-9]|5b[0-9]|5b-look|5c\b|design doc|0001|0002|§|ruling|wave [A-J]|task [0-9]|the plan|ledger|outcome|brainstorm)' src/core --include='*.?pp' | grep -v /generated/`, plus `R[0-9][a-z]?\b`, `P[0-9]\b` and dates; read every plain comment in the module.
- [ ] **Step 2:** rewrite per the Global Constraints, file by file.
- [ ] **Step 3:** rerun Step 1's grep: no hits in comments (hits in code identifiers, e.g. a `phase` variable, are fine: say which in the report).
- [ ] **Step 4:** `pixi run project_n test`, `pixi run project_n docs`, `pixi run project_n lint --range <base>..HEAD`.
- [ ] **Step 5:** commit `docs(core): drop design references from core's comments`.

### Task 2: gfx's and the shaders' comments

**review: light**

**Files:** `src/gfx/` (except `generated/`), `shaders/*.hlsl`.

Steps as Task 1 over these files; plus `pixi run project_n shaders` (commit `shaders/bin/` and `src/gfx/generated/` only
if they changed, saying why) and `pixi run project_n reference`. Commit `docs(gfx): drop design references from gfx's
and the shaders' comments`.

### Task 3: game's, app's, data's and platform's comments

**review: light**

**Files:** `src/game/`, `src/app/`, `src/data/`, `src/platform/`, `src/main.cpp`, every `src/*/module.dox`, `src/pch.hpp`
and any other file directly in `src/`.

Steps as Task 1 over these files. Commit `docs(game): drop design references from the game and app comments`.

### Task 4: the tests' comments

**review: light**

**Files:** `tests/` (C++, Python, `xmake.lua`; not `tests/fuzz/corpus/`, not `tests/reference/*.png`).

Steps as Task 1 over these files, including `TEST_CASE` names and `CAPTURE`/`INFO`/`MESSAGE` strings. A renamed
`TEST_CASE` that `--test` filters in docs or CI name is renamed there too (grep `.github/`, `*.md`, `tools/`). Commit
`docs(tests): drop design references from the tests' comments`.

### Task 5: tools', scripts', data files' and build files' comments

**review: light**

**Files:** `tools/` (`project_n.py`, `cliff.toml`, others), `art/*.py`, `art/*.toml`, `assets/data/*.yaml`,
`xmake.lua`, `xmake/`, `.github/workflows/*.yml`, `containers/` (Containerfiles, scripts), and the templates in
`tools/` that write `src/*/generated/` (regenerate after: `pixi run project_n shaders`, `pixi run project_n atlas`;
commit their outputs and `art/atlas.sha256` with them).

Steps as Task 1 over these files with `#` comments; `assets/data/*.yaml` changes are comments only (a hashed file's
hash covers parsed values, not comments: confirm `pixi run project_n test` shows no data-hash expectation moved). CLI
help strings and messages that cite a chunk or design doc are reworded. Commit `build(tools): drop design references
from tools', scripts' and data comments` (the git-commit skill: tooling is `build`).

### Task 6: the `lint` check and the agent docs

**review: full** (tooling that gives a verdict)

**Files:** `tools/project_n.py` (the check and its self-tests), root `AGENTS.md` (Rules), `src/AGENTS.md`,
`tests/AGENTS.md`, `tools/AGENTS.md`, the `.claude/skills/` that tell agents how to write code comments if any
(grep `comment`).

- [ ] **Step 1: Write the failing self-tests** (the self-tests `check` runs first, next to `nolint_explained`'s):
  `design_references(path, text)` returns the offending lines with a message; samples (about 12, named): `// (the
  phase 6 chunk 4 spec)`, `/// see design doc 0002 §7.6`, `// chunk 4b`, `# phase 5's gate`, `// ruling R6a`,
  `// Task 7's test`, `// 5b-look restyles it` → found; `int phase = 0;` (code, not comment), `"chunk"` in a string
  literal, `// the third phase of the fight` (no digit), `// NOLINT(check): why`, an `.md` file → not found.
- [ ] **Step 2: Run** `pixi run project_n test` (or the self-test entry `check` uses): FAIL.
- [ ] **Step 3: Implement:** scan comments only (C++ `//`, `///`, `/* */`; Python, YAML, TOML, Containerfile `#`;
  HLSL as C++), in the files the Global Constraints call code, skipping generated outputs (fixed at their generator);
  part of `lint`'s diff run and of `lint --all`; the message says the rule ("code comments don't cite specs, chunks,
  phases, plans, rulings, tasks or design docs: say why in your own words").
- [ ] **Step 4:** `pixi run project_n lint --all`: no design-reference finding (Tasks 1-5 cleaned the tree).
- [ ] **Step 5: Agent docs**, one line each, in this commit: root AGENTS.md Rules ("Code comments never cite the
  design process ... ; plain comments explain only why, on specific targeted parts; `lint` checks the first"),
  src/AGENTS.md and tests/AGENTS.md (the same rule where their comment and Doxygen rules sit), tools/AGENTS.md (the
  check exists, where its self-tests are). `pixi run project_n doctor --docs` passes.
- [ ] **Step 6:** `pixi run project_n test`, `pixi run project_n lint --range <base>..HEAD`.
- [ ] **Step 7:** commit `build(tools): refuse design references in code comments`.

## After this plan

5b2's ledger constraints gain the rule, so its waves C-J briefs and reviews carry it; 5b2 resumes at wave C.
