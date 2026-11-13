# History first: every superpowers step starts from what came before

Date: 2026-11-13 · Status: implemented 2026-11-13 (approved the same day; the user: "Including these changes")

## Intent

The user, 2026-11-13: "I just want to ensure that we always look at history and consider refactoring, abstractions,
cleanup and also don't recover old ground as much." Two goals:

1. **No old ground.** Before designing, planning, debugging or reviewing, find what earlier sessions tried, decided or
   rejected, and build on it instead of rediscovering it.
2. **Cleanup in view.** Use that history (code touched again and again, near-duplicates, deferred cleanups) so each
   design and plan weighs a refactor or a shared abstraction against adding one more special case.

Today nothing makes this happen. The only rule is the global CLAUDE.md's "search the palace before answering about past
work", and no superpowers step triggers it. The palace holds 15,094 `project_n` drawers of raw transcript chunks, nearly
all in the catch-all `technical` room, so a careless query returns noise (a test search came back at "low" confidence,
mostly `Launching skill:` lines).

"History" means all of it: the palace (what was said: dead ends, rejected ideas, the user's words), git history, the
specs and their Outcome sections, the design docs, and the project memory's sweep-up backlog and `in-flight.md`.

## Goals, non-goals, anti-goals

- Goals: one recall procedure that every step uses the same way; a trigger that fires without anyone remembering;
  recall's results written where the next step reads them (spec, plan tasks, review).
- Non-goals: writing to the palace (the global rule "don't write unless asked" stays); moving to the full 47-tool
  MemPalace server (the user asked, 2026-11-13: it is the same engine and data, and adds no trigger); reorganising the
  palace (`/mempalace:audit` is a later option if recall stays noisy); recall inside each implementer subagent.
- Anti-goals: dumping drawers into the context; recall that reopens settled decisions instead of respecting them;
  a step made much slower for little found (the procedure scales to the task: a typo-sized change gets a quick look).

## Approaches

- **A. Rules only:** a "history first" line in AGENTS.md per step. Cheap, but it relies on being remembered, which is
  exactly how the palace gets missed today.
- **B. Rules, a skill and a trigger (picked by the user, 2026-11-13):** a `recall-history` skill holds the procedure, a
  hook fires when a superpowers step starts and points at it, and the rules say where the results go.
- **C. B plus filing decisions into the palace's `decisions` room** at each branch's end. Cleaner hits, but it drops
  the no-write rule and partly repeats what the specs already record. Later, if B's searches stay noisy.

## The design

**Global, not per project** (the user, 2026-11-13: "make sure this setup is global so any project using superpowers and
mempalace gets the same"). The skill, the hook and the rules live in the user's `~/.claude/`, so every project gets
them; a project adds only the history sources it names itself. `~/.claude/` is not under version control: this spec is
the record of what the global files hold and why, kept in project_n because the design started here and project_n's
AGENTS.md changes with it.

0. **Low token use** (the user, 2026-11-13: "Ensure the script and skill are optimised for low token usage"). The
   expensive part of recall is the raw material: palace results are JSON with ~15 metadata fields per drawer, and
   `git log` and grep output pile up; whatever enters the main session is paid for again on every later turn. So:
   - the search runs in a **`history-recall` subagent** (`~/.claude/agents/history-recall.md`, on Sonnet, its tools
     limited to Bash, Read, Grep, Glob, `LSP` and `mcp__mempalace-light__palace_query`), and only its report (≤ 30
     lines) comes back to the main session (my call: Sonnet, not Haiku, because sorting "decided" from "tried and
     rejected" and judging cleanup is judgment, and a wrong report costs more than it saves);
   - the `recall-history` skill is a thin dispatcher (≤ ~30 lines, a description of one short sentence, since every
     session lists it);
   - recall runs once per topic: a later step reuses history already in context (the spec's **History** section, an
     earlier report) instead of searching again;
   - the hook adds one short sentence, only for the four triggering skills, and nothing otherwise;
   - inside the agent: `LIMIT 5` per palace query, at most three queries before widening, `DRAWER` only for hits worth
     quoting, `git log` capped with `-n`, and a one-query, one-`git log` look for a small change.
1. **`recall-history` skill and `history-recall` agent** (`~/.claude/skills/recall-history/SKILL.md`,
   `~/.claude/agents/history-recall.md`; written with superpowers writing-skills). Project-agnostic. The skill hands
   the agent a topic, the files and symbols involved, or an error; the agent does:
   - Palace (when the MemPalace tools are connected; otherwise say so and carry on with the rest): a few short keyword
     queries in the project's wing (the repository's main folder name, also from a worktree), widened only if empty;
     skip the noise (skill launches, tool echoes); open the promising drawers; quote what matters verbatim.
   - Repo: `git log` for the paths involved (recent churn: what keeps changing), superpowers' specs and plans on the
     topic (`docs/superpowers/`) with any Outcome sections, and the history sources the project's CLAUDE.md or
     AGENTS.md names (design docs, a backlog, handover notes).
   - Code: existing code that already does part of the job (the `LSP` tool where a language server is set up), the
     candidates to reuse or extend.
   - Report, short: **Prior work** · **Decided** (not to reopen without the user) · **Tried and rejected** ·
     **Cleanup candidates** (refactor, abstraction or cleanup, each with a recommendation: in this branch, its own task,
     or the backlog) · **Searched** (the queries, so an empty result says what was looked for).
2. **The trigger:** a hook in `~/.claude/settings.json` on the Skill tool that, when the skill is superpowers
   brainstorming, writing-plans, systematic-debugging or requesting-code-review, adds a reminder to run
   `recall-history` first. A small standard-library Python script, `~/.claude/hooks/recall_history_reminder.py`, run by
   the host's `python3` (global config is per machine, so no project toolchain is involved).
3. **The rules:** `~/.claude/CLAUDE.md`'s MemPalace section gains a "history first" paragraph: the steps above run
   `recall-history`, and where its results go (below).
4. **project_n's part:** one line in AGENTS.md's Working with superpowers naming its own history sources for the skill:
   `docs/design/`, the specs' Outcome sections, and the project memory's sweep-up backlog and `in-flight.md`.
5. **Where the results go** (the global rules, item 3):
   - brainstorming: the report shapes the questions and approaches (a cleanup candidate can be one of the
     approaches), and the spec gets a **History** section;
   - writing-plans: each task carries the history it needs in its text, so implementers don't search again, and a
     cleanup worth doing first becomes its own task (refactor before feature);
   - systematic-debugging: past fixes and dead ends on the same symptom, before the first hypothesis;
   - code review: the reviewer also checks the diff for code duplicating an existing abstraction, and for anything a
     spec's Outcome ruled out.

## Superpowers and MemPalace as global defaults, ponytail gone

The user, 2026-11-13: "migrate my current superpowers/mempalace setup here to just both be my global defaults for all
claude projects and globally remove Ponytail". Found 2026-11-13:

| What | Now | After |
|---|---|---|
| superpowers plugin | installed at project scope; enabled by project_n's (and another project's) `.claude/settings.json` | installed at user scope, enabled in `~/.claude/settings.json` for every project |
| MemPalace MCP (`mempalace-light`) | user scope already | unchanged |
| MemPalace hooks | Stop and PreCompact in `~/.claude/settings.json` | plus SessionEnd (`mempal_session_end_hook.sh`, the hooks README's install), so short sessions are saved too (my call) |
| MemPalace rules | `~/.claude/CLAUDE.md` | plus "history first" (above) |
| ponytail | installed at user scope, enabled in `~/.claude/settings.json` and turned off per project in project_n; its marketplace and `enabledPlugins` entry in `~/.claude/settings.json`; leftovers `~/.claude/ponytail-statusline.sh`, `.ponytail-active`, `.ponytail-statusline-nudged`, `ponytail-modes/`; project_n's `"ponytail@ponytail": false` | uninstalled, marketplace removed, entries and leftovers deleted, project_n's line removed |

My calls:
- project_n keeps `"superpowers@claude-plugins-official": true` in its `.claude/settings.json`: the repo is public and
  its workflow depends on superpowers, so a fresh clone on another machine still gets it. AGENTS.md's Map line drops
  "and ponytail off". Other projects' identical lines are redundant but harmless and are left alone (not this repo).
- `~/.claude/settings.json` and `~/.claude/CLAUDE.md` are copied to `*.bak-2026-11-13` before editing (not versioned).
- Older specs and plans that mention ponytail stay as written: they are history.
- Moving project_n's generic superpowers rules (fix every finding, PRs only, Outcome sections) into the global
  CLAUDE.md is not part of this: offered to the user as a follow-up.

## Lean global context

The user, 2026-11-13: "ensure all the global injections/prompts/mds/agents/setup is token efficient, without loss of
functionality". What every session in every project loads from the user's setup, found 2026-11-13:

| Source | Cost per session | Change |
|---|---|---|
| `~/.claude/CLAUDE.md` | 1,289 bytes (~320 tokens) | rewritten tight, the "history first" rules folded into the MemPalace section; target ≤ 1.9 KB with them |
| superpowers' SessionStart (its `using-superpowers` skill, ~1.5k tokens) and 15 skill descriptions | the largest | none: trimming it means forking the plugin and losing its updates |
| `skill-creator@claude-plugins-official` | one listing | disabled: the same skill, same description, is already listed as `anthropic-skills:skill-creator` (synced from claude.ai) |
| `code-review`, `code-simplifier`, `claude-md-management`, `rust-analyzer-lsp` plugins | a few listings; the LSP none | none: each does something the rest don't (the built-in `code-review`/`simplify` overlap but don't match them) |
| `mempalace-light` MCP | 3 deferred tools | none: it is already the lean server |
| MemPalace Stop/PreCompact hooks | none (`silent_save` is on: they mine without blocking the model) | none |
| ponytail | none while disabled | removed (above) |
| new: hook reminder, skill and agent descriptions | one sentence per triggering step; two listings | kept to one short sentence each (design item 0) |
| claude.ai's Claude Docs connector instructions (~400 tokens) | every session | not ours to trim; the user can turn the connector off in claude.ai's settings if unused (offered, not done) |

Project-level context (project_n's AGENTS.md, its memory index) is not global and not part of this; offered as a
follow-up.

## Left to the builder

The hook's event (before or after the skill loads) and its output format, checked by triggering it in a real session;
the skill's exact query patterns, from baseline runs against real past topics in two projects with palace wings
(project_n and another project) so the skill is shown to be project-agnostic (writing-skills' test-first
method); a small self-test for the hook script (feeding it sample hook input).

## Outcome (2026-11-13)

What the global files hold now (not versioned; backups `~/.claude/settings.json.bak-2026-11-13`,
`~/.claude/CLAUDE.md.bak-2026-11-13`):

| File | Holds |
|---|---|
| `~/.claude/skills/recall-history/SKILL.md` | the dispatcher: skip if the topic's history is in context; dispatch `history-recall` with repo, topic, paths, error, size; where the report goes; Decided / Tried and rejected reopened only with the user |
| `~/.claude/agents/history-recall.md` | Sonnet; tools Bash, Read, Grep, Glob, LSP, `palace_query`; read-only; gathers in two turns of parallel calls (project rules, docs, git and churn, palace ≤ 3 × `LIMIT 5` in the wing slugged as MemPalace files it, widened hits labelled as other projects', code; git skipped outside a repo); a five-heading report of ≤ 25 lines |
| `~/.claude/hooks/recall_history_reminder.py` (+ `test_recall_history_reminder.py`, 6 tests) | PostToolUse on Skill and UserPromptSubmit (a typed `/superpowers:<step>`): one sentence when brainstorming, writing-plans, systematic-debugging or requesting-code-review starts; nothing otherwise, never blocks |
| `~/.claude/settings.json` | superpowers enabled at user scope; skill-creator disabled; ponytail and its marketplace gone; hooks: Stop, PreCompact (as before), PostToolUse and UserPromptSubmit (the reminder), SessionEnd (MemPalace) |
| `~/.claude/CLAUDE.md` | 1,533 bytes (1,289 before, without the history rules): packages and MemPalace rules unchanged in substance, the wing rule made exact (MemPalace's slug, worktrees), plus "History first" |

Rulings and their evidence:

- **The skill is an output contract, not prohibitions.** Baselines (a fresh Sonnet subagent asked "before we design
  it, what do we already know?") found the history well in both projects: project_n's found that the lint change had
  already shipped, the dev-loop-speedups spec and a stale backlog entry; the other project's found an architecture
  record's explicit rejection of the approach asked about. Their failure was the report: 45–60 lines, no decided/rejected split, no cleanup
  judgement. So the agent fixes the report's shape and size; the hook fixes the trigger (both baselines only searched
  because they were asked to).
- **Measured cost**, headless `claude -p` for both arms (baselines on the session default, Opus; the agent on Sonnet):

  | Topic | Baseline | `history-recall` |
  |---|---|---|
  | project_n: lint only what a header reaches | 136k tokens, $0.39, 31 lines | 74k–96k tokens, $0.12–0.14, 25 lines |
  | another project: a feature its records once rejected | 110k tokens, $0.32, 36 lines | 62k tokens, $0.105, 29 lines |

  The first agent draft took 10 turns (200k tokens) and 31–32 lines; "two turns of parallel calls" and a 25-line
  target with a count-and-cut rule brought it to the numbers above. Only the report reaches the main session.
- **The hook runs after the skill loads (PostToolUse).** Checked end to end: a fresh `claude -p` session in an empty
  repo with no project settings invoked superpowers:brainstorming (so superpowers loads from the user scope) and quoted
  the reminder verbatim.
- **A new agent type loads only at session start**, so the GREEN runs used `claude -p --agent history-recall`; from
  the next session the Agent tool can dispatch it.
- **ponytail's cached copy** (`~/.claude/plugins/cache/ponytail`) survived `claude plugin uninstall` and was deleted
  with the other leftovers.
- **Final review's fixes** (Opus, whole branch plus the global files; no Critical):
  - A typed `/superpowers:brainstorming` loads the skill without a Skill tool call, so PostToolUse never fired (checked
    end to end: "None reached me"). The script now also answers UserPromptSubmit when the prompt's first word is one
    of the four commands; rechecked end to end, the reminder arrives.
  - The wing was the raw folder name, but MemPalace files sessions under a slug (lower case, `-` and spaces to `_`:
    a hyphenated folder name's `IN` query finds nothing, its underscored wing holds the drawers) and folds only `.claude/worktrees/` and
    `.worktrees/` into the project. The agent and the global CLAUDE.md now derive the wing that way; a worktree
    elsewhere queries its own wing and the main repository's.
  - Widened palace hits come from other projects: the agent labels them as cross-project hints and writes "no hits in
    <wing>". Run on a repository with no wing: "no hits", one query for a small change, $0.04.
  - The global CLAUDE.md regained "before answering" (not only when asked) and "never guess"; the skill's line limit
    matches the agent's 25; the agent skips git outside a repository.
  - Left for the user: another project still pins superpowers at project scope on 7.0.0 (every other project gets 7.0.2
    from the user scope; `claude plugin uninstall superpowers@claude-plugins-official --scope project` there
    would end that); the headless test runs were filed into the palace (a wing of end-to-end test noise, and the
    recall reports in project_n's wing and another's), and removing palace entries is the user's call.
- **main's unity build was broken** by the glows: `src/core/glow.cpp` and `src/core/volley.cpp` each defined an
  anonymous `HALF` (`f32` and `f64`), which collide in one unity batch, so `check` failed on any branch. Renamed
  glow's to `GLOW_HALF` ("fix(core): keep glow's half constant unique across unity batches"); `test --preset
  linux-clang --unity` failed before and passes after.
