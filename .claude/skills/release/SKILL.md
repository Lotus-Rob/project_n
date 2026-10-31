---
name: release
description: Cut a project_n release. Use when the user asks to release, tag a version, bump the version, write or regenerate the changelog, publish or attach build artefacts, or asks what the next version number would be.
---

# Releasing project_n

Owned by: root AGENTS.md. Rationale: docs/design/0001 §11.3.

Release only when the user asks; never on your own initiative.

## Steps

1. Be on `main`, up to date, with a clean working tree (`release` refuses otherwise).
2. `pixi run project_n check` and `pixi run project_n check --container` both pass.
3. `pixi run project_n release --dry-run` prints the changelog section and the version. Review it:
   - Every commit sits in the right group; a `feat`/`fix` outside `src/` means a mistyped commit (git-commit skill). History on `main` is never rewritten, so tell the user rather than fixing it.
   - The version follows the bump rules below. If it surprises you, explain why to the user before releasing.
4. `pixi run project_n release` creates the branch `release/vX.Y.Z`, sets `set_version()` in xmake.lua, regenerates `CHANGELOG.md` and commits `chore(release): vX.Y.Z` there. Merge it like any branch: review round and a pull request with green CI (root AGENTS.md, Finishing a branch).
5. After the merge, on an up-to-date `main`: `pixi run project_n release --tag` tags main's release commit (GitHub's rebase merge rewrote the branch's commit, so the tag can only come now). It refuses if `CHANGELOG.md` no longer matches the commits (dates aside), when the pull request was rebased onto newer work: don't tag this commit; delete the merged local `release/vX.Y.Z` branch (GitHub deleted the remote one at the merge) and go back to step 3: the newer commits may change the version, so review the dry run again before `release`. Push the tag only when the user says so: `git push origin vX.Y.Z`. CI runs every job on the tag, the native `windows` and `macos` ones included; when all pass, its `release` job runs `pixi run project_n publish`, which drafts the GitHub release `vX.Y.Z` with each preset's game zip and `-symbols` zip (the macOS one holds `project_n.app`) and the changelog section, plus a note that the builds aren't code-signed yet (design doc O6).
6. Check the draft (`pixi run gh release view vX.Y.Z`): eight archives, the right version in each name, the notes. Publish it only when the user says so: `pixi run gh release edit vX.Y.Z --draft=false`, or the Publish button. A failed build job means no draft: fix it on `main` and cut the next version; a tag is never moved. A `release` job that failed while uploading can leave a partial draft: `pixi run gh release delete vX.Y.Z --yes` (the tag stays), then re-run the job.
7. Locally, `pixi run project_n artifact --preset <p>` builds the same archives (not macOS: it needs a Mac) to compare against CI's (S2).

## Bump rules

| Commits since the last tag | Before 1.0 | From 1.0 |
|---|---|---|
| Breaking (`!` or `BREAKING CHANGE:`) | MINOR | MAJOR |
| `feat` | MINOR | MINOR |
| anything else | PATCH | PATCH |

From 1.0, "breaking" means incompatible save files, config or mod/content formats, or a changed build interface (`project_n.py` commands, presets). git-cliff applies these rules from `tools/cliff.toml`; the first release is `v0.1.0` (`initial_tag`).

## Rules

- `CHANGELOG.md` is generated; never edit it by hand. Fix a wrong entry by fixing the next commit message, not the file.
- The tag `vX.Y.Z` and `set_version()` always match; only `release` changes either.
- The game is AGPL-3.0-or-later: whoever receives a build must be able to get its source. Publish the tagged source with every build that leaves the machine.
- `dist/<preset>-symbols/` (`.sym`, `.pdb`, `.dSYM`) is for decoding crash reports: keep it with the release, never ship it to players.
- Never publish Microsoft's files (the MSVC install, anything from `/opt/msvc`) alongside artefacts (licence).
- GitHub attaches the tagged source to every release, which covers the AGPL's source offer.

## Gotchas
