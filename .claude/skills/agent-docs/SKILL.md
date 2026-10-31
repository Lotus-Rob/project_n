---
name: agent-docs
description: Maintain project_n's agent knowledge. Use when creating or editing any AGENTS.md or CLAUDE.md, adding a Gotchas entry, writing or changing a skill under .claude/skills or an agent under .claude/agents, proposing a new skill or agent, or when an AGENTS.md has grown past its size limit or contradicts the code.
---

# Maintaining AGENTS.md files and skills

Owned by: root AGENTS.md. Rationale: docs/design/0001 §10.

## Three tiers; each fact lives in exactly one

| Tier | Holds | Loaded | Limit |
|---|---|---|---|
| `AGENTS.md` | Facts needed for *any* task in that directory: map, commands, one-line rules, skill pointers, gotchas | Always | ~100 lines, one line per rule |
| Skills (`.claude/skills/<name>/SKILL.md`) | *How* to do one job: steps, decisions, examples | When the task matches the `description` | ~200 lines; longer material in `references/` |
| Design docs (`docs/design/`) | *Why*; alternatives rejected | When changing the design | none |
| Agent briefs (`.claude/agents/<name>.md`) | A subagent's role, what it may touch, what it reports | When spawned, in a fresh context | ~60 lines |

Other tiers link to a fact instead of repeating it. When an AGENTS.md rule needs more than one line, move the explanation to a skill and leave `<rule> → <skill>`.

## The contract

1. A change that makes any AGENTS.md or skill statement false updates it **in the same commit**.
2. After resolving a non-obvious failure, add one line under **Gotchas** in the nearest AGENTS.md (or the skill whose procedure it affects): symptom, then fix.
3. Respect the size limits; over the limit, move procedure into a skill or `docs/` and leave a pointer.
4. Add a nested AGENTS.md only when an area's rules differ from its parent's; never duplicate parent content. Each AGENTS.md has a sibling `CLAUDE.md` containing only `@AGENTS.md`; the root's is `.claude/CLAUDE.md` with `@../AGENTS.md`.
5. Commands quoted anywhere must be real `project_n` commands; `check` enforces it (its `selftest` runs the `doctor --docs` check). When renaming one, grep every AGENTS.md and skill.

## Skills

Write a skill so it loads exactly when needed:

1. **The description is the trigger.** Name the situations and the words a task would contain ("commit", "release", "bump gcc"). A vague description means the skill never loads.
2. **Procedures call commands.** Steps say "run `pixi run project_n <cmd>`, then check X". Mechanical sequences belong in `project_n.py`, not in prose.
3. **Progressive disclosure.** SKILL.md has the steps and the decisions; long tables, templates and examples go in `references/*.md`, linked from SKILL.md.
4. **One owner.** The skill names its owning AGENTS.md, and that AGENTS.md lists it under Skills. Add or remove both in the same commit.
5. **Skills are kept true too**: fix a misleading step in the same commit as the work, and log it under the skill's Gotchas.
6. No secrets, no machine-specific paths.

Templates for AGENTS.md and SKILL.md: [references/templates.md](references/templates.md).

## Agents

An agent brief gives a subagent what it needs in a fresh context and nothing it can read elsewhere:

1. **Role, limits, report.** Who it is, what it may and may not change, and the exact shape of its report.
2. **Point, don't copy.** For a procedure or a definition that a skill holds, name the skill section to read; a copy drifts.
3. **One owner.** Root AGENTS.md lists every agent under Agents, and the skill that spawns it names it. Add or remove them in the same commit; the contract above applies to briefs too.
4. **Proposed like a skill:** created only after the user agrees (below).

## Proposing a skill (needs user approval)

Propose when a multi-step workflow has been done by hand **twice**, or an area's Gotchas reach **5+ entries** about one workflow:

1. Add a row to the owning AGENTS.md's **Skill candidates** table: workflow, evidence, proposed name.
2. Tell the user what it would cover and why now. Create it only after they agree; then remove the candidate row and add the skill to the Skills list.

## Gotchas
