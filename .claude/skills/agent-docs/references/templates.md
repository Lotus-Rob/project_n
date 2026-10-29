# Templates

## AGENTS.md (nested)

```markdown
# <dir>/: <one-line purpose>

## Scope
What lives here, in one or two sentences.

## Rules
- One line per rule. Longer explanations live in a skill: `<rule> → <skill>`.

## Commands
- `pixi run project_n <command>` the commands relevant to this area

## Skills
- <skill>: when to use it

## Gotchas
- <symptom>: <fix>

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|
```

Its sibling `CLAUDE.md` contains exactly one line: `@AGENTS.md`.

## SKILL.md

```markdown
---
name: <kebab-case-name>
description: <What it does>. Use when <situations and trigger words a task would contain>.
---

# <Title>

Owned by: <path>/AGENTS.md.

## Steps
1. Run `pixi run project_n <cmd>` ...
2. Decide ...: if X then ..., otherwise ...

## Checks before finishing
- `pixi run project_n check` passes (or `check --container` for build/env changes)

## Gotchas
```
