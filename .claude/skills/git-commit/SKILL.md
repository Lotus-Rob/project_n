---
name: git-commit
description: Write commit messages and shape commits for project_n. Use whenever committing, writing or fixing a commit message, choosing a commit type or scope, marking a breaking change, splitting a mixed change into commits, squashing fixups, or when the commit-msg hook or `project_n commit-check` rejects a message.
---

# Committing in project_n

Owned by: root AGENTS.md. Enforced by `.githooks/commit-msg` and `pixi run project_n commit-check` (CI checks every commit in a PR).

## Format

```
<type>[(<scope>)][!]: <description>

[body: what and why, wrapped at 72]

[footers, e.g. BREAKING CHANGE: ...]
```

- Header at most 72 characters; description in the imperative ("add", not "added"), starting lowercase, no trailing period.
- A blank line separates header and body.

## Choosing the type

| Type | Use for |
|---|---|
| `feat` / `fix` | Game or engine behaviour in `src/` only, so the changelog lists what players get |
| `perf` | A performance change with no behaviour change |
| `refactor` | Code change that is neither a fix nor a feature |
| `test` | Tests only |
| `build` | `xmake.lua`, `xmake/`, `pixi.*`, `containers/`, `tools/`, dependencies, including tooling features and fixes |
| `ci` | CI configuration |
| `docs` | Docs, design docs, AGENTS.md, skills |
| `style` | Formatting only |
| `chore` | Anything else (repo configs); `chore(release)` is written by `project_n release` |
| `revert` | Reverts; the body names the reverted commit's hash |

A tooling feature is still `build`, e.g. `build(tools): add artifact command`.

## Choosing the scope

Optional. When used it must be in `SCOPES` in `tools/project_n.py` (see the list there; don't copy it here). A change that needs a new scope adds it to `SCOPES` in the same commit. Pick the area the change is *about*; omit the scope when a change spans several areas equally.

## Breaking changes

Mark with `!` after the type/scope and explain in a `BREAKING CHANGE:` footer. "Breaking" means: incompatible save files, config or mod/content formats, or a changed build interface (`project_n` commands, presets). Before 1.0 it bumps the minor version.

## Shaping commits

- One logical change per commit. AGENTS.md and skill updates go in the same commit as the change that makes them necessary.
- Split a mixed working tree with `git add -p` or by path; commit the enabling change (e.g. a new dependency) before the change that uses it.
- History is linear: never merge. Tidy a branch with `git commit --fixup=<sha>`, then squash non-interactively: `GIT_SEQUENCE_EDITOR=: git rebase -i --autosquash main`.
- `fixup!`/`squash!` commits pass the hook locally but fail `check`: squash them before merging.

## Examples

```
feat(render): recreate swapchain on window resize
fix(audio): stop crackle when the device sample rate changes
build(env): pin llvm 22.1.8 for all llvm packages
build(tools)!: rename `dist` command to `artifact`

BREAKING CHANGE: scripts calling the old `dist` command must use `project_n artifact`.
docs(agents): add gotcha for wine prefix flushing
```

## When the hook rejects a message

Read its message (it names the rule), fix the header, and commit again. Don't use `--no-verify`: CI runs the same check.

## Gotchas
