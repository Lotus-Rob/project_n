# R1: agent docs to one line per rule — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every AGENTS.md below the root fits a byte and line-length budget that `doctor --docs` enforces, with every
fact cut from it kept in the one place it belongs.

**Architecture:** Each oversized AGENTS.md is cut to one short line per rule plus pointers; what it held moves to the
tier that owns it (0001 §10.1): behaviour to Doxygen on the class or module it describes, procedure to a skill, a why
about one function to a comment at that function, rationale, dates and measurements to the spec Outcome or design doc
they came from. Then `doctor --docs` gains a size check, the selftest runs it on the real tree, and 0001 §10.1 and the
root rule state the budgets.

**Tech Stack:** Markdown, Doxygen comments in C++ headers and `module.dox`, Python (stdlib) in `tools/project_n.py`
with `unittest` in `tools/test_project_n.py`.

**Spec:** `docs/superpowers/specs/2026-11-25-review-mediums-design.md`, area R1 (findings X-ARCHITECTURE-08,
DESIGN-DOCS-02, GD-ROADMAP-SCOPE-07; their cluster AGENT-DOCS-02, CORE-INFRA-20, SPECS-BUILT-A-09, GAME-SCENES-13,
TOOLS-A-20; review files under `docs/reviews/2026-11-25-full-codebase-review/`).

## History

- The rule today: 0001 §10.1, "≤ ~100 lines; one line per rule" for AGENTS.md, SKILL.md ≤ ~200 lines, agent briefs
  ≤ ~60 lines; a rule needing more than a line moves to a skill, leaving `<rule> → <skill>`. One fact lives in one tier.
- No earlier attempt to shrink these files: they grew by appends (`docs(agents): ...` commits adding to the nearest
  line), and a line count cannot see a 15 KB line. A 2026-11-16 session measured ~47k tokens of start-up context and
  noted the long lines; nothing was done.
- `doctor --docs` today is `stale_doc_commands()` (tools/project_n.py, near line 3046): quoted commands only, no size
  check. `test_agent_docs_quote_only_real_commands` runs it on the real tree in `selftest` (CI's `lint` job).
- Approved calls (the spec): the budgets apply to every AGENTS.md below the root; the root's process rules are the
  user's and stay as written, and the check exempts the root, saying why. `.clang-tidy` and `.clangd` are not touched.
- shared-routes' stage 1 tasks also edit `src/AGENTS.md` and `tests/AGENTS.md` (one-line rules): whichever lands
  second rebases, and its lines must meet these budgets.

## Global Constraints

- **Budgets:** every tracked `AGENTS.md` except the root's: ≤ 8,192 bytes (UTF-8) and every line ≤ 300 characters
  (Unicode code points, code spans included). `.claude/skills/*/SKILL.md` ≤ 200 lines; `.claude/agents/*.md` ≤ 60
  lines. The root `AGENTS.md` is exempt and its process rules are not edited, except Task 6's one rule line.
- **Nothing is deleted without a home.** Each task's report carries a fact map: every fact cut from an AGENTS.md →
  where it now lives (`path:line`), or "already at `path:line`" (the Doxygen often holds it already), or "dropped:
  <why>" (only for what is stale or false today, checked against the code).
- **Homes, by kind (0001 §10.1):** behaviour of a class or module → its `///` Doxygen or `src/<module>/module.dox`;
  how to do a job → the skill that owns it (`references/` file if long); a why about one function or one CI step → a
  comment at it; rationale, dates, spike results, measurements → the spec Outcome or design doc they came from (most
  are there already); a rule true only in one module → `src/<module>/AGENTS.md` with a sibling `CLAUDE.md` holding
  only `@AGENTS.md` (same budgets).
- **One line per rule.** A rule line states the rule and, if it needs more, points: `→ <path or skill>`. Sub-bullets
  (two-space indent) are allowed for a list a rule really is (log levels). Meanings don't change; wording may shorten.
- **Code comments** follow `src/AGENTS.md`: no citation of specs, plans, design docs, dates, phases or spikes
  (`lint`'s `design_references`); a plain comment only says why; `///` may say what.
- **Pointers resolve:** every backticked path in a rewritten AGENTS.md exists, every quoted command exists
  (`pixi run project_n doctor --docs`).
- **Commits:** `docs(agents)` for AGENTS.md, CLAUDE.md and skills; `docs(core|data|platform|gfx|game|app)` for Doxygen
  moved into `src/<module>/`; `docs(tools)` for `project_n.py` docstrings and comments; `docs(tests)` for comments in
  `tests/`; `docs(design)` for specs and design docs; `ci` for comment changes in `.github/workflows/ci.yml`;
  `build(tools)` for Task 6's check (a tooling feature is `build`: the git-commit skill).
- **Gates per task:** `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD`; Tasks 1-5
  also run the budget probe below on the files they own and `pixi run project_n doctor --docs`.

Budget probe (until Task 6's check exists):

```bash
for f in <files>; do printf '%s %s bytes\n' "$f" "$(wc -c <"$f")"; awk -v f="$f" 'length > 300 {print f ":" FNR ": " length " chars"}' "$f"; done
```

(`awk` in a UTF-8 locale counts characters; Pixi's env sets one. Over budget: keep cutting.)

## Review Focus

1. A rule silently lost or its meaning changed in the cut → every task's fact map; the reviewer reads the old line
   beside its new home for each entry.
2. A Doxygen block moved into a header attaches to the wrong declaration (a `///` straight after the `/// @file` block
   joins it: src/AGENTS.md Gotchas) → Tasks 1 and 2 run `pixi run project_n docs` and compare its warnings with the
   base's.
3. A pointer names a file or symbol that doesn't exist → Tasks 1-5's pointer probe step.
4. Multi-byte characters (`·`, `→`, `—`) counted as bytes for the line budget, failing honest lines → Task 6's test
   `test_a_line_is_measured_in_characters`.
5. A moved comment carrying a date or a spec citation into code → `lint` (`design_references`) at each task's gate.

## Waves

| Wave | Tasks | Needs |
|---|---|---|
| 1 | 1 (src: game side), 3 (tests), 4 (tools, .github), 5 (art, assets) | nothing; disjoint files except `tools/project_n.py` docstrings (Task 3: test, reference, fuzz, coverage commands only; Task 4: the rest; different hunks) |
| 2 | 2 (src: gfx, app, rules, gotchas) | Task 1 (same file) |
| 3 | 6 (the check, 0001, root rule) | Tasks 1-5 landed: its real-tree test must pass |

---

### Task 1: src/AGENTS.md, the game side (Scope line and the scenes/host line)

`implementer: sonnet` · `review: light`

**Files:**
- Modify: `src/AGENTS.md` (line 4, Scope, 7.3 KB; line 27, the arena/scenes/host rule, 15.4 KB)
- Modify (Doxygen only): `src/game/module.dox`, `src/game/host.hpp`, `src/game/scene.hpp`, `src/game/scenes.hpp`,
  `src/game/scenes/arena.hpp`, `src/game/world_state.hpp`, `src/game/travel.hpp`, `src/data/module.dox`, others the
  fact map needs under `src/game/`, `src/data/`, `src/core/`
- Create if needed: `src/game/AGENTS.md`, `src/game/CLAUDE.md` (`@AGENTS.md`)

**Interfaces:** none (docs). Leaves lines 7-26 and 28-49 of `src/AGENTS.md` to Task 2.

- [ ] **Step 1: Map line 4.** Split it into facts: the module list and their dependency direction (→ `0002` §4 and
  each `module.dox`: check), the YAML data flow (which file is hashed game data, which presentation, which C++ reads
  it: `assets/AGENTS.md` holds this per the root map; check it does, add only what it lacks there in one-line form),
  and "phases 1 to 5 of §16 implemented" (stale: AGENT-DOCS-04; drop, or point at 0002 §16 without a count).
- [ ] **Step 2: Rewrite Scope** as ≤ 3 lines of ≤ 300 characters: what `src/` holds, the module list with one pointer
  each to its `module.dox`, the pointer to `assets/AGENTS.md` for data.
- [ ] **Step 3: Map line 27** fact by fact to the headers it describes: scenes and their registry (`game/scene.hpp`,
  `game/scenes.hpp`, `game/scenes.cpp`), the arena's list rule (`game/scenes/arena.hpp`), the host's worlds, saves,
  travel, fog, walk tables and player route (`game/host.hpp`, `game/world_state.hpp`, `game/travel.hpp`,
  `core/fog.hpp`, `core/calendar.hpp`, `core/road_curve.hpp`). For each, read the header's existing `///` first;
  write only what is missing, as API docs (what and contract), never a date or a spec number.
- [ ] **Step 4: Rewrite line 27** as rules only: "A new game or engine feature shows up in the arena in the same
  branch and joins `game/scenes/arena.hpp`'s list", "Scenes are stateless classes behind `game/scene.hpp`, registered
  in `game/scenes.cpp` → its header", "The host's behaviour: `game/host.hpp`" and whatever else in it is a rule (an
  instruction to whoever changes code), each ≤ 300 characters. Rules true only in `game` go to `src/game/AGENTS.md`.
- [ ] **Step 5: Verify.** Budget probe on `src/AGENTS.md` lines you own (4, 27 and their replacements) and on
  `src/game/AGENTS.md` if created: no line over 300. Pointer probe:
  `grep -o '\`[^\`]*/[^\` ]*\.\(hpp\|cpp\|md\|py\|yaml\|toml\|dox\)\`' src/AGENTS.md | tr -d '\`' | sort -u | while read p; do [ -e "$p" ] || [ -e "src/$p" ] || echo "missing: $p"; done`
  prints nothing. `pixi run project_n docs`: no Doxygen warning beyond those it printed at the task's base (run it once
  before Step 3 and keep the list). `pixi run project_n doctor --docs` passes.
- [ ] **Step 6: Gates.** `pixi run project_n test`; `pixi run project_n lint --range <base>..HEAD`.
- [ ] **Step 7: Commit** the Doxygen as `docs(game): ...` (and `docs(data)`/`docs(core)` if touched), the AGENTS.md
  cut as `docs(agents): ...`. Report with the fact map.

### Task 2: src/AGENTS.md, the rest (rendering, run flags, rules, gotchas) to budget

`implementer: sonnet` · `review: light`

**Files:**
- Modify: `src/AGENTS.md` (everything but Task 1's lines)
- Modify (Doxygen only): `src/gfx/module.dox`, `src/gfx/renderer.hpp`, the draw headers line 24 names
  (`gfx/ground.hpp`, `gfx/terrain_draw.hpp`, `gfx/route_draw.hpp`, `gfx/stamp_draw.hpp`, `gfx/seal_draw.hpp`,
  `gfx/volley_draw.hpp`, `gfx/tile_hash.hpp`, `gfx/internal/features/*.hpp`), `src/app/module.dox`,
  `src/app/bindings.cpp`'s or `app/camera_path.hpp`'s docs where line 31 describes them
- Create if needed: `src/gfx/AGENTS.md` + `src/gfx/CLAUDE.md`, `src/app/AGENTS.md` + `src/app/CLAUDE.md`
- Modify if a procedure moves there: `.claude/skills/look-at-the-game/SKILL.md` (stays ≤ 200 lines)

**Interfaces:** consumes Task 1's rewritten Scope and line 27; ends with `src/AGENTS.md` and every nested
`src/*/AGENTS.md` within budget.

- [ ] **Step 1: Line 24 (rendering, 7.3 KB):** the draw order and each pass's job → `gfx/renderer.hpp`'s Doxygen on
  the world pass (or `gfx/module.dox` "Draw order" section) and each feature's header; the rule left behind: "The
  world pass's draw order: `gfx/renderer.hpp`; a new layer goes in that list" (or whatever in it is a rule).
- [ ] **Step 2: Line 31 (`./dev run` flags and keys, 5.4 KB):** flags and debug keys → `src/app/module.dox` (a "Running
  the game" section) unless the app's own `--help` output already lists them; the line left behind: the command with
  its common flags and `→ src/app/module.dox`. Procedures for looking at the game → the look-at-the-game skill.
- [ ] **Step 3: Rules (lines 7-23, 25-28):** each ≤ 300 characters; the long ones (logging 543, profiling 537,
  entities 697, comments 559, headers 486) keep the rule and point at the facade header (`core/log.hpp`,
  `core/profile.hpp`, `core/ecs.hpp`) whose Doxygen holds the detail (check; add what's missing there).
- [ ] **Step 4: Gotchas (lines 37-49):** each ≤ 300 characters. A gotcha about one function or one header moves to a
  comment at it and leaves the file; MSVC warning gotchas stay (they bite anywhere) in one line each.
- [ ] **Step 5: Verify** as Task 1 Step 5, over the whole of `src/AGENTS.md` and every `src/*/AGENTS.md`: each
  ≤ 8,192 bytes, no line over 300 characters, the pointer probe silent, no new Doxygen warnings, `doctor --docs` passes.
- [ ] **Step 6: Gates** as Task 1. **Step 7: Commit** `docs(gfx)`, `docs(app)` for Doxygen, `docs(agents)` for the
  AGENTS.md and skill. Report with the fact map.

### Task 3: tests/AGENTS.md to budget

`implementer: sonnet` · `review: light`

**Files:**
- Modify: `tests/AGENTS.md` (19.3 KB; line 20, the reference tests, 5.8 KB; line 22, fuzz targets, 2.1 KB)
- Modify (comments/docstrings only): `tests/reference/*.cpp` and `*.hpp` file headers, `tests/fuzz/` file headers,
  `tools/project_n.py` docstrings of `cmd_reference`, `cmd_test`, `cmd_fuzz`, `cmd_coverage` and their helpers only
- Modify if a procedure moves there: `.claude/skills/look-at-the-game/SKILL.md` or a new
  `.claude/skills/look-at-the-game/references/reference-pictures.md` (judging drift, `--update`)

**Interfaces:** none.

- [ ] **Step 1: Map line 20 and line 22** to: the reference runner's mechanics → `tests/reference/` file headers and
  `cmd_reference`'s docstring; how to judge and update pictures → the look-at-the-game skill (`references/` file);
  fuzz target conventions → one rule line plus the fuzz harness's file header.
- [ ] **Step 2: Cut every other line** (rules 7-25, commands 28-36, gotchas 39-46) to ≤ 300 characters, same homes as
  the Global Constraints. "About 20 named samples" and the test-shape rules stay as rules.
- [ ] **Step 3: Verify:** budget probe on `tests/AGENTS.md` (≤ 8,192 bytes, no line over 300), the pointer probe
  (Task 1 Step 5's, on this file), `pixi run project_n doctor --docs`.
- [ ] **Step 4: Gates; commit** `docs(tests)` for comments in tests/, `docs(tools)` for docstrings, `docs(agents)` for
  the AGENTS.md and skill. Report with the fact map.

### Task 4: tools/AGENTS.md and .github/AGENTS.md to budget

`implementer: sonnet` · `review: light`

**Files:**
- Modify: `tools/AGENTS.md` (21.1 KB; `measure` line 22, 3 KB; gotchas 30-49, several 1-2.4 KB),
  `.github/AGENTS.md` (8.5 KB; the cache line 13, 3.3 KB; line 9, 1.2 KB)
- Modify (comments/docstrings only): `tools/project_n.py` (every function except Task 3's), `tools/test_project_n.py`
  if a gotcha is about a test, `.github/workflows/ci.yml` comments at the steps the cache and job lines describe
- Modify if measurements or dated results have no home yet: the spec Outcome they came from (`docs/superpowers/specs/`)
  or `docs/design/0001-repository-build-environment.md`'s notes

**Interfaces:** none.

- [ ] **Step 1: `measure` (line 22):** flags, runs and columns → `cmd_measure`'s docstring and `MEASURE_RUNS`'s
  comment; the line left: the command, "never a gate; takes the display, run it alone", `→ cmd_measure`.
- [ ] **Step 2: Gotchas:** each about one function (`tidy_units`, `run_reference_case`, `install_cl_wrapper`,
  `packages_dir`, the lock rewrite, the build cache) → a why-comment or docstring at that function, and leaves the
  file unless it bites someone who isn't editing that function (then one line with the pointer). Dated spike results →
  the Outcome or 0001 note that records them (most are there: check).
- [ ] **Step 3: `.github/AGENTS.md`:** the cache layers (line 13) → comments at each cache step in `ci.yml`, leaving
  "Three cache layers (setup, objects, packages): see the comments at their steps in `ci.yml`; the 10 GB budget rule"
  as rules; line 9's job split → the job names with one pointer. Rules (pin by SHA, never upload Microsoft's files)
  stay.
- [ ] **Step 4: Verify:** budget probe on both files, pointer probe on both, `pixi run project_n selftest` (docstrings
  and comments don't break it), `pixi run project_n doctor --docs`, and `pixi run project_n lint --range <base>..HEAD`
  (actionlint covers `ci.yml`).
- [ ] **Step 5: Commit** `docs(tools)` for project_n.py, `ci: ...` for `ci.yml` comments, `docs(agents)` for the two
  AGENTS.md. Report with the fact map.

### Task 5: art/ and assets/ AGENTS.md lines to budget

`implementer: sonnet` · `review: light` (may share Task 3's review)

**Files:**
- Modify: `art/AGENTS.md` (a 1,206-character line), `assets/AGENTS.md` (a 558-character line)
- Modify (docstrings only): `art/world_map.py` or the generator the long line describes

**Interfaces:** none.

- [ ] **Step 1:** Cut each line over 300 characters to its rule plus a pointer; the detail → the generator's module
  docstring (art) or the C++ reader's Doxygen named in the line (assets).
- [ ] **Step 2: Verify:** budget probe and pointer probe on both files; `pixi run project_n selftest` (the roads check
  reads `art/`); `pixi run project_n doctor --docs`.
- [ ] **Step 3: Gates; commit** `docs(agents)` for both AGENTS.md, `docs: ...` (no scope: `art/` has none in `SCOPES`)
  for the docstring. Report with the fact map.

### Task 6: `doctor --docs` checks the budgets; 0001 and the root rule state them

`implementer: sonnet` · `review: full`

**Files:**
- Modify: `tools/project_n.py` (beside `stale_doc_commands`, `cmd_doctor`, the `--docs` help), `tools/test_project_n.py`
- Modify: `docs/design/0001-repository-build-environment.md` (§10.1 table and Rule, §10.2 table, the Revised row),
  `AGENTS.md` (the root's "Agent docs:" rule line only), `tools/AGENTS.md` and `.github/AGENTS.md` where they describe
  what `doctor --docs` checks, `docs/superpowers/specs/2026-11-25-review-mediums-design.md` (Closing column)

**Interfaces:**
- Produces:

```python
AGENTS_MD_BYTES = 8192      # below the root
DOC_LINE_CHARS = 300        # AGENTS.md lines, below the root
SKILL_LINES = 200           # .claude/skills/*/SKILL.md
AGENT_BRIEF_LINES = 60      # .claude/agents/*.md

def doc_budget_problems(path: str, text: str) -> list[str]: ...   # path is repo-relative, '/'-separated
def oversized_agent_docs() -> list[str]: ...                       # every tracked or new doc, like stale_doc_commands
```

Messages, exactly: `"<path>: <n> bytes, over the 8192-byte budget"`, `"<path>:<line>: <n> characters, over the
300-character line budget"`, `"<path>: <n> lines, over the 200-line budget"` (60 for briefs).

- [ ] **Step 1: Write the failing tests** in `tools/test_project_n.py` (a new `TestDocBudgets` class):

```python
def test_an_agents_md_at_the_byte_budget_passes(self):
    self.assertEqual(project_n.doc_budget_problems("src/AGENTS.md", "a" * 8191 + "\n"), [])
def test_an_agents_md_over_the_byte_budget_fails(self):
    self.assertEqual(project_n.doc_budget_problems("src/AGENTS.md", ("a" * 99 + "\n") * 82),
                     ["src/AGENTS.md: 8200 bytes, over the 8192-byte budget"])
def test_a_line_over_300_characters_fails(self):
    self.assertEqual(project_n.doc_budget_problems("tests/AGENTS.md", "x\n" + "b" * 301 + "\n"),
                     ["tests/AGENTS.md:2: 301 characters, over the 300-character line budget"])
def test_a_line_is_measured_in_characters(self):
    self.assertEqual(project_n.doc_budget_problems("tools/AGENTS.md", "→" * 300 + "\n"), [])  # 900 bytes
def test_the_root_agents_md_is_exempt(self):
    self.assertEqual(project_n.doc_budget_problems("AGENTS.md", "c" * 20000 + "\n"), [])
def test_a_skill_over_200_lines_fails(self):
    self.assertEqual(project_n.doc_budget_problems(".claude/skills/x/SKILL.md", "l\n" * 201),
                     [".claude/skills/x/SKILL.md: 201 lines, over the 200-line budget"])
def test_a_skill_at_200_lines_passes_with_long_lines(self):
    self.assertEqual(project_n.doc_budget_problems(".claude/skills/x/SKILL.md", ("d" * 400 + "\n") * 200), [])
def test_an_agent_brief_over_60_lines_fails(self):
    self.assertEqual(project_n.doc_budget_problems(".claude/agents/x.md", "l\n" * 61),
                     [".claude/agents/x.md: 61 lines, over the 60-line budget"])
def test_a_nested_agents_md_is_held_to_the_budget(self):
    self.assertEqual(len(project_n.doc_budget_problems("src/game/AGENTS.md", "e" * 301 + "\n")), 1)
def test_agent_docs_fit_their_budgets(self):  # the real tree, as test_agent_docs_quote_only_real_commands
    self.assertEqual(project_n.oversized_agent_docs(), [])
```

- [ ] **Step 2: Run** `pixi run project_n selftest`: the new cases fail with `AttributeError` (no
  `doc_budget_problems`).
- [ ] **Step 3: Implement** the four constants and two functions beside `stale_doc_commands`. Bytes are
  `len(text.encode("utf-8"))`, characters `len(line)` per `text.splitlines()`. A comment at the root's exemption says
  why: the root's process rules are the user's and stay as written. `oversized_agent_docs` lists files with the same
  `git ls-files` patterns and `is_file` guard as `stale_doc_commands` (share the listing: one helper both call).
  `cmd_doctor --docs` prints both functions' findings, or `doctor --docs: every quoted command exists and every agent
  doc fits its budget`, and returns 1 on any finding; `--docs`'s help says it checks both.
- [ ] **Step 4: Run** `pixi run project_n selftest` and `pixi run project_n doctor --docs`: all pass (Tasks 1-5 landed).
- [ ] **Step 5: State the rule.** 0001 §10.1's AGENTS.md row: size rule "below the root: ≤ 8 KB (8,192 bytes), one
  line per rule of ≤ 300 characters; the root's process rules are the user's (exempt); `doctor --docs` checks";
  SKILL.md and brief rows add "(`doctor --docs` checks)"; the Rule paragraph adds: a fact about one function lives at
  that function. §10.2: the `src/AGENTS.md` row drops "(filled in by the architecture doc)" and rows are added for
  each nested AGENTS.md Tasks 1-2 created. Revised row: prepend `2026-11-25, agent docs' size rule is a byte and
  line-length budget that `doctor --docs` checks (§10.1).` Root `AGENTS.md`'s "Agent docs:" line: "(~100 lines, one
  line per rule)" → "(below the root ≤ 8 KB, one line of ≤ 300 characters per rule; `doctor --docs` checks)".
  `tools/AGENTS.md` and `.github/AGENTS.md`: what `doctor --docs` checks. The spec's findings table: Closing
  `amended` for X-ARCHITECTURE-08, DESIGN-DOCS-02, GD-ROADMAP-SCOPE-07.
- [ ] **Step 6: Gates:** `pixi run project_n test`, `pixi run project_n lint --range <base>..HEAD`,
  `pixi run project_n doctor --docs`.
- [ ] **Step 7: Commit** the check and its tests as `build(tools): ...`, then `docs(design)` for 0001 and the spec,
  `docs(agents)` for the AGENTS.md lines.
