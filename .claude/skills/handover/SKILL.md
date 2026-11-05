---
name: handover
description: Write a handover so a fresh session can continue project_n work. Use when stopping mid-branch, when the context is getting long, when blocked on the user's decision, at a large milestone, or when the user asks for a handover, a summary for the next session, or where things stand.
---

# Writing a handover

Owned by: root AGENTS.md.

## When
Work through several chunks in one session, committing each. Write a handover and stop only when blocked on a decision that is the user's, when the context is getting long, or at a large milestone (a phase done, a branch merged). Not after every task.

## Where
`<scratchpad>/handover-<branch or topic>-<what was last done>.md`, and give the user its full absolute path. It is for the next session, not the repository: nothing in it may be the only record of a decision. Decisions and rationale go into the design doc, gotchas into AGENTS.md or a skill, in the commits that make them true.

## Contents, in this order
1. **One paragraph:** branch, whether it is pushed, what to read first (AGENTS.md files, design doc sections), the previous handover's path if it still exists.
2. **The user's standing rules** that aren't in AGENTS.md (from memory or this session), in one line each.
3. **Commits this session:** a table of short hash, Conventional Commits header and one line of what it means; then the last gate that passed (`check`, `check --container`, `artifact`) and at which commit.
4. **Next, in order:** numbered, each a chunk the next session can start without asking.
5. **Open, for the user:** questions asked and not answered, and `shortcut:`s that are the user's call.
6. **Known limits:** what is untested or unverified (other platforms, CI not yet run, hardware not available) and why.
7. **Gotchas learned** that haven't reached an AGENTS.md or skill yet; better: add them there before writing the handover.

## Checks before finishing
- `git status` is clean, or the handover says what is uncommitted and why.
- Every hash and command in it is real (`git log --oneline`; commands go through `project_n`).
- Dates are absolute (2026-11-05, not "today").

## Gotchas
