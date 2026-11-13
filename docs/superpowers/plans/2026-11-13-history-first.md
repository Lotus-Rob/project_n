# History First Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every superpowers step (design, plan, debugging, review) in every project starts by recalling history through
a global `recall-history` skill fired by a global hook; superpowers and MemPalace become global defaults; ponytail is
removed.

**Architecture:** Four global pieces in `~/.claude/` (a thin skill, the subagent it dispatches so only a short report reaches the main session, a standard-library Python hook on the Skill tool, a
paragraph in `CLAUDE.md`), plugin and hook changes in `~/.claude/settings.json`, and two small edits in project_n. Only
project_n's edits and this branch's docs are committed: `~/.claude/` is not versioned, so the spec records what it holds.

**Tech Stack:** Claude Code hooks and skills, Python 3 standard library (`json`, `sys`, `unittest`), the `claude` CLI.

**Spec:** `docs/superpowers/specs/2026-11-13-history-first-design.md`

## Global Constraints

- The hook never blocks or fails a tool call: any unexpected input prints nothing and exits 0.
- Triggering skills, exactly: `superpowers:brainstorming`, `superpowers:writing-plans`,
  `superpowers:systematic-debugging`, `superpowers:requesting-code-review`.
- No writes to the palace (`palace_exec`) anywhere in the skill.
- The skill is project-agnostic: no project_n paths in it; a project's own history sources come from its CLAUDE.md or
  AGENTS.md.
- `~/.claude/settings.json` and `~/.claude/CLAUDE.md` are copied to `*.bak-2026-11-13` before their first edit.
- project_n keeps `"superpowers@claude-plugins-official": true`; other projects are not touched.

## Review Focus

- A skill name in another form (`brainstorming` without the plugin prefix, or another plugin's `x:brainstorming`):
  no reminder, no error. Test in Task 1.
- Hook input that isn't JSON, or JSON without `tool_input`: nothing printed, exit 0. Test in Task 1.
- A project with no palace wing (or no MemPalace tools): the skill says so in **Searched** and still reports the
  repo and code history. Covered by Task 2's second baseline project.
- A worktree session: the wing is the main repository's folder name, not the worktree's. Task 2's skill text and its
  project_n run, done from this worktree.
- Settings left invalid JSON after the edits: Claude Code would drop them silently. Task 3's `json.tool` check.

## Waves

| Wave | Tasks | Needs |
|---|---|---|
| A | 1, 2, 4 | nothing; no shared files |
| B | 3 | Task 1's script path, Task 2's skill name |
| C | 5 | all |

Each task runs in the main session (Native), not in implementer worktrees: Tasks 1–3 write to `~/.claude/`, which a
worktree can't isolate.

---

### Task 1: The reminder hook script, test first

review: light

**Files:**
- Create: `~/.claude/hooks/recall_history_reminder.py`
- Test: `~/.claude/hooks/test_recall_history_reminder.py`

**Interfaces:**
- Produces: a command reading one hook event as JSON on stdin. For a PostToolUse event with `tool_name == "Skill"` and
  `tool_input.skill` in the four triggering skills, it prints
  `{"hookSpecificOutput": {"hookEventName": "PostToolUse", "additionalContext": REMINDER}}` and exits 0; otherwise it
  prints nothing and exits 0. `REMINDER` (exact copy):
  `History first: run the recall-history skill on this step's topic before going on, unless its history is already in context (a spec's History section, an earlier report).`
  The script imports only `json` and `sys`, and prints nothing at all when it doesn't fire.

- [ ] **Step 1: Write the failing tests** (`unittest`, each runs the script with `subprocess.run([sys.executable, SCRIPT], input=..., capture_output=True, text=True)`):
  - `test_each_triggering_skill_gets_the_reminder`: for each of the four skills, exit 0 and
    `json.loads(stdout)["hookSpecificOutput"] == {"hookEventName": "PostToolUse", "additionalContext": REMINDER}`.
  - `test_other_skills_get_nothing`: `superpowers:test-driven-development`, `brainstorming`, `other:brainstorming`,
    `recall-history` → exit 0, stdout `""`.
  - `test_other_tools_get_nothing`: `tool_name` `"Bash"` with `tool_input.skill` `superpowers:brainstorming` → `""`.
  - `test_bad_input_gets_nothing`: `"not json"`, `"{}"`, `"[]"`, `{"tool_name": "Skill"}`,
    `{"tool_name": "Skill", "tool_input": "x"}` → exit 0, stdout `""`.
- [ ] **Step 2:** Run `python3 -m unittest discover -s ~/.claude/hooks -v`. Expected: errors (script missing).
- [ ] **Step 3:** Implement the script: `json.load(sys.stdin)` inside one `try`; any exception or non-dict → exit 0.
- [ ] **Step 4:** Run the same command. Expected: 4 tests OK.

### Task 2: The `recall-history` skill and `history-recall` agent, through superpowers writing-skills

review: full

**Files:**
- Create: `~/.claude/skills/recall-history/SKILL.md`, `~/.claude/agents/history-recall.md`

**Interfaces:**
- Produces: a skill named `recall-history` that dispatches the agent `history-recall`, whose report (≤ 30 lines) has
  these headings, in order: **Prior work**, **Decided**, **Tried and rejected**, **Cleanup candidates**, **Searched**.

- [ ] **Step 1: Baseline (RED).** Run two fresh general-purpose subagents without the skill, each asked "before we
  design it, what do we already know?", and note each run's total tokens (the Agent result reports them):
  - project_n (from this worktree): "make `lint` lint only the `.cpp` files a changed header reaches" (designed and
    landed in `2026-11-12-dev-loop-speedups-design.md`).
  - another project with a palace wing: a topic picked from `FIND <keywords> IN <wing> LIMIT 5` that has history there.
  Record verbatim what each found and missed (the earlier spec? the palace? git churn? existing code?).
- [ ] **Step 2: Write the skill** (≤ ~30 lines; frontmatter `name: recall-history`, a description of one sentence
  starting "Use when", ≤ ~150 characters). Its body: skip if this topic's history is already in context; otherwise
  dispatch `history-recall` (Agent tool, `subagent_type: history-recall`) with the topic, the paths and symbols
  involved, the error text if debugging, and the size (small change or project); then use the report where the
  global CLAUDE.md says, never pasting it twice.
- [ ] **Step 2b: Write the agent** (≤ ~80 lines; frontmatter `name: history-recall`, `model: sonnet`,
  `tools: Bash, Read, Grep, Glob, LSP, mcp__mempalace-light__palace_query`): the procedure of the spec's design
  item 1, in order palace → repo → code → report, within design item 0's limits. It must say: the wing is
  the main repository's folder name (`git rev-parse --path-format=absolute --git-common-dir`, its parent's name), also
  in a worktree; queries are `FIND <2-4 keywords> IN <wing> LIMIT 5`, at most three (topic, file or symbol names,
  error text), widened (no `IN`) only if all are empty; skip `Launching skill:` lines and bare tool echoes; open a promising
  drawer with `DRAWER <id>`; quote verbatim; no MemPalace tools → say so under **Searched** and go on; read the history
  sources the project's CLAUDE.md or AGENTS.md names; `git log --oneline -n 20 -- <paths>` and churn
  (`git log --since=<3 months ago> --name-only --format= -- <dir> | sort | uniq -c | sort -rn | head`); find existing
  code with the `LSP` tool where available, else grep; each cleanup candidate ends with "this branch", "its own task"
  or "backlog"; scale: a one-line change gets one query and one `git log`; the report ≤ 30 lines, no raw JSON or
  command output in it.
- [ ] **Step 3: GREEN.** Rerun both topics through the skill. Expected: both reports have the five headings and are
  ≤ 30 lines; the project_n one names `2026-11-12-dev-loop-speedups-design.md` under **Prior work** or **Decided**;
  neither pastes whole drawers; each run's total tokens at or below its baseline's (recorded for the Outcome).
- [ ] **Step 4: REFACTOR.** Close any loophole the runs showed, rerun the failing one.

### Task 3: Global settings, rules, superpowers on, ponytail off

review: light

**Files:**
- Modify: `~/.claude/settings.json`, `~/.claude/CLAUDE.md`
- Delete: `~/.claude/ponytail-statusline.sh`, `~/.claude/.ponytail-active`, `~/.claude/.ponytail-statusline-nudged`,
  `~/.claude/ponytail-modes/`

**Interfaces:**
- Consumes: Task 1's `~/.claude/hooks/recall_history_reminder.py`; Task 2's skill name `recall-history`.

- [ ] **Step 1:** `cp ~/.claude/settings.json ~/.claude/settings.json.bak-2026-11-13` and the same for `CLAUDE.md`.
- [ ] **Step 2:** `claude plugin install superpowers@claude-plugins-official --scope user`;
  `claude plugin uninstall ponytail@ponytail --scope user`; `claude plugin marketplace remove ponytail`.
- [ ] **Step 3:** In `settings.json`: `enabledPlugins` has `"superpowers@claude-plugins-official": true` and no
  ponytail key; no `ponytail` marketplace; `hooks` gains
  `"PostToolUse": [{"matcher": "Skill", "hooks": [{"type": "command", "command": "python3 ~/.claude/hooks/recall_history_reminder.py", "timeout": 10}]}]`
  and `"SessionEnd": [{"hooks": [{"type": "command", "command": "<mempalace checkout>/hooks/mempal_session_end_hook.sh", "timeout": 10}]}]`;
  everything else unchanged.
- [ ] **Step 4:** Delete the four ponytail leftovers; `claude plugin disable skill-creator@claude-plugins-official --scope user`
  (the spec's Lean global context: `anthropic-skills:skill-creator` stays).
- [ ] **Step 5:** Rewrite `CLAUDE.md` tight (≤ 1.9 KB; `wc -c`), every current rule kept (shelly commands and the
  read-only exception; palace search syntax, wings, scoping, verbatim quotes, no writes). Fold into the MemPalace
  section a "History first" paragraph: superpowers brainstorming, writing-plans, systematic-debugging and
  requesting-code-review run `recall-history` first (a hook reminds), once per topic; results go: the spec's
  **History** section; each plan task carries the history it needs, a cleanup worth doing first is its own task;
  debugging starts from past fixes; reviewers check for code duplicating an existing abstraction and for anything an
  Outcome ruled out. A project names its extra history sources in its CLAUDE.md or AGENTS.md.
- [ ] **Step 6: Verify.** `python3 -m json.tool ~/.claude/settings.json >/dev/null` exits 0; `claude plugin list`
  shows superpowers at user scope enabled, skill-creator disabled and no ponytail; `claude plugin marketplace list` has no ponytail;
  `diff ~/.claude/settings.json.bak-2026-11-13 ~/.claude/settings.json` shows only Step 3's changes.
- [ ] **Step 7: End to end.** In a scratch git repo (scratchpad), run
  `claude -p "Invoke the superpowers:brainstorming skill, then quote verbatim any 'History first' reminder you received after it loaded, or say there was none."`
  Expected: the reminder quoted. If not, switch the hook to PreToolUse (same output with `"hookEventName": "PreToolUse"`,
  Task 1's tests updated) and rerun.

### Task 4: project_n's part

review: light

**Files:**
- Modify: `.claude/settings.json` (drop `"ponytail@ponytail": false`), `AGENTS.md` (Map line; Working with superpowers)

- [ ] **Step 1:** Remove the ponytail line from `.claude/settings.json`; Map line reads "`.claude/settings.json` turns
  superpowers on and swaps the global clangd plugin …" (rest unchanged).
- [ ] **Step 2:** Working with superpowers, after the "Continue where we left off" line, add:
  "- History sources for `recall-history` (global skill, run at each superpowers step): `docs/design/`, the specs'
  Outcome sections, and the project memory's `sweep-up-backlog.md` and `in-flight.md`."
- [ ] **Step 3:** `pixi run project_n doctor --docs` and `pixi run project_n lint --range <the commit before this task>..HEAD` (run as from "docs(design): audit the global context for token use"). Expected: both
  clean.
- [ ] **Step 4:** Commit: `chore(agents): drop ponytail and name the history sources`.

### Task 5: Outcome, gate, PR

review: light (then the final whole-branch review)

- [ ] **Step 1:** Spec: an **Outcome** section (what each global file holds, Task 2's baseline vs GREEN findings,
  Task 3's end-to-end result and the hook event used); status "implemented". Commit
  `docs(design): record history-first's outcome`.
- [ ] **Step 2:** `pixi run project_n check`. Expected: pass.
- [ ] **Step 3:** Final review (requesting-code-review, `main..HEAD` plus the global files), fix every finding,
  `check` again, then the root AGENTS.md's Finishing a branch.
