---
name: review-branch
description: Get a branch reviewed before it merges into main, and act on the review. Use when finishing a branch, when asked to review a branch or run the pre-merge review, or when fixing review findings.
---

# Reviewing a branch before merge

Owned by: root AGENTS.md. Every branch gets a clean review from a fresh reviewer before it merges. The reviewer is the `branch-reviewer` agent (`.claude/agents/branch-reviewer.md`), which holds what to check and how to report.

## Steps

1. Finish the branch: `pixi run project_n check` passes and everything is committed.
2. Spawn a new `branch-reviewer` subagent with the Agent tool: "Review branch `<branch>` (HEAD `<sha>`) against main." It starts with none of the author's context, which is the point.
3. Fix every finding; there is no non-blocking one. A bug in `src/` is fixed test-first (fix-bug skill). A fix belongs in the commit it corrects: `git commit --fixup=<sha>`, then `GIT_SEQUENCE_EDITOR=: git rebase -i --autosquash main` (git-commit skill), then `check` again.
4. Review again with another **new** subagent, never by continuing the previous one: each round reviews the whole branch with fresh eyes. At most 5 rounds per branch: fresh reviewers keep finding smaller things, so the loop doesn't end by itself.
5. Merge after a round ends `REVIEW: CLEAN` on the branch's last commit, through a pull request with green CI (root AGENTS.md, Finishing a branch, step 4). If round 5 still has findings, fix them all and pass `check`, then give the user round 5's findings and the fixes: the user decides whether to merge or to allow more rounds. Tell the user the result of each round.

## Gotchas

- An agent added during a session isn't available straight away ("Agent type 'branch-reviewer' not found"). Until it is, spawn a `general-purpose` subagent and tell it to read `.claude/agents/branch-reviewer.md` as its full brief.
