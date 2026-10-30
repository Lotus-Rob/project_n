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
4. `pixi run project_n release` sets `set_version()` in xmake.lua, regenerates `CHANGELOG.md`, commits `chore(release): vX.Y.Z` and creates an annotated tag.
5. `pixi run project_n artifact --preset <p>` for each of `linux-gcc`, `linux-clang`, `windows-msvc`. Each `dist/<preset>/manifest.json` must name the release commit (`artifact` refuses uncommitted changes and builds from an empty build directory).
6. Release gate: CI's native `windows` job passed on the release commit (`gh run list --branch main`), or the user waives it (while CI is unavailable). The container build runs Windows tests only under Wine.
7. Push only when the user says so: `git push --follow-tags`. Then, also only when the user says so, publish the GitHub release with the archives `artifact` wrote: `gh release create vX.Y.Z --verify-tag --notes "See CHANGELOG.md" dist/project_n-X.Y.Z-*.zip` (each preset's game and its `-symbols` zip; the repository is private, so only collaborators can download them).

## Bump rules

| Commits since the last tag | Before 1.0 | From 1.0 |
|---|---|---|
| Breaking (`!` or `BREAKING CHANGE:`) | MINOR | MAJOR |
| `feat` | MINOR | MINOR |
| anything else | PATCH | PATCH |

From 1.0, "breaking" means incompatible save files, config or mod/content formats, or a changed build interface (`project_n.py` commands, presets). git-cliff applies these rules from `cliff.toml`; the first release is `v0.1.0` (`initial_tag`).

## Rules

- `CHANGELOG.md` is generated; never edit it by hand. Fix a wrong entry by fixing the next commit message, not the file.
- The tag `vX.Y.Z` and `set_version()` always match; only `release` changes either.
- The game is AGPL-3.0-or-later: whoever receives a build must be able to get its source. Publish the tagged source with every build that leaves the machine.
- `dist/<preset>-symbols/` (`.sym`, `.pdb`, `.dSYM`) is for decoding crash reports: keep it with the release, never ship it to players.
- Never publish Microsoft's files (the MSVC install, anything from `/opt/msvc`) alongside artefacts (licence).

## Gotchas
