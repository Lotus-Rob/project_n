---
name: handover
description: Write a handover so a fresh session can continue project_n work. Use when a plan task completes (its review is clean), when stopping mid-branch, when the context is getting long, when blocked on the user's decision, at a large milestone, when the user asks for a handover, a summary for the next session or where things stand, and when a branch merges (its handover is removed).
---

# Handing work over

Owned by: root AGENTS.md.

## When
- **A plan task completes** (the user, 2026-11-13): its review is clean (after its fix pass and re-review) and it is on the feature branch. Dispatch no further task, the rest of its wave included. Tasks of the same wave that are already running finish their implement-and-review loop; then write the handover and stop, ending with a short summary for the user. One task per session, however much context is left. After the last task, the handover's Next is finishing the branch (root AGENTS.md, Finishing a branch; its step 3 is the final whole-branch review), which the next session runs.
- Also: blocked on a decision that is the user's, the context getting long, or a branch merged (delete its section).
- Work without a plan (a fix, a spike) still runs several commits per session and hands over only at those other points.

## Where
`in-flight.md` in the project's Claude Code memory folder (`~/.claude/projects/<project>/memory/`; worktrees share it, and the memory index loads it in every session). It holds one section per branch in flight, headed `## <branch>`. A handover replaces only its own branch's section, or adds it if missing; every other section stays exactly as it was. When the branch merges, delete its section.

Nothing in the section may be the only record of a decision: decisions and rationale go into the spec (`docs/superpowers/specs/`, its Outcome section), gotchas into AGENTS.md or a skill, in the commits that make them true.

## The section
```
## <branch> — <in progress | paused | waiting for review | waiting for merge> (updated YYYY-MM-DD)
- Where: <worktree path, or the main checkout>; <pushed or not>; <PR link or "no PR">.
- Spec / plan / ledger: <their paths, or "none">.
- Decided: <the user's decisions so far, one clause each>.
- State: <what is done (for a plan: the tasks complete, by number); the last gate that passed and at which commit; anything uncommitted and why>.
- Next: <the next step, one a fresh session can start without asking; for a plan: the next task(s) by number, starting from the ledger as superpowers' subagent-driven-development `references/resuming.md` says>.
- Open for the user: <unanswered questions, or "none">.
```
Six bullets, each one or two lines: git, the plan's ledger and AGENTS.md hold the rest.

## Checks before finishing
- Every other section of `in-flight.md` is unchanged (compare it with the file before your edit).
- Every hash and command in the section is real (`git log --oneline`; commands go through `project_n`).
- Dates are absolute (2026-11-05, not "today").
- `git status` is clean, or the State bullet says what is uncommitted and why.
