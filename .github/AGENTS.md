# .github/: CI workflows

## Scope
`workflows/ci.yml`: GitHub Actions for every push and tag, and manual runs. `rulesets/main.json`: the rule on `main` (pull requests only, all six jobs green on an up-to-date branch, rebase merge, linear history, no bypass). `dependabot.yml`: weekly updates of the pinned actions. `actionlint.yaml`: runner labels actionlint doesn't know yet.

## Rules
- Workflows only run `pixi run project_n <command>`; any logic beyond setup belongs in tools/project_n.py, so CI and local runs can't drift.
- Pin every action by commit SHA with the version in a comment; Pixi by exact version (`PIXI_VERSION`).
- Jobs split `check` so they run in parallel: `lint` (`selftest` with the `doctor --docs` check, commit-check, and `lint --container` over a branch's commits since main, or main's pushed ones; plus `fmt --check` over the same, which also catches commits made without the pre-commit hook), `container` (one clean-container `artifact` job per preset: build, tests, dependency check, upload), `windows` (`selftest`, native MSVC build, tests, ASan), `macos` (native build, tests, ASan, artefact), and on `v*` tags `release` (after all the others: `publish` drafts the GitHub release from every preset's archives). Locally, `check` stays the gate.
- Every push runs every job (the public repository's minutes are free), as does `pixi run gh workflow run ci --ref <branch>`. Never put an `if:` on a required job: GitHub counts a skipped job as passed, so the ruleset's check would be hollow.
- Never push or upload Microsoft's files (the MSVC install, anything from `/opt/msvc`): no image contains them, and only `dist/`'s zip archives are uploaded, named by `artifact` (`project_n-<version>-<preset>`, plus the commit unless a tag is built). Caching them in the Actions cache is accepted only because no workflow runs on `pull_request`: never add that trigger without keeping the MSVC caches out of it (the repository is public).
- Two cache layers. Setup, saved as soon as its step finishes: Pixi envs (setup-pixi), the linux and msvc images in GHCR by content tag, the MSVC install from `project_n image` (key: `msvc.Containerfile`), the native MSVC 14.50 toolset (key: `buildtools.vsconfig`), the macOS XMake bundle (key: `XMAKE_MACOS_SHA256`). Packages, saved at job end even on failure: XMake packages per job (key: pixi files, `xmake-requires.lock`, `xmake/repo/`).

## Commands
- Apply a changed ruleset (the user's say-so; it binds everyone): `pixi run gh api -X PUT repos/Lotus-Rob/project_n/rulesets/<id> --input .github/rulesets/main.json` (`pixi run gh api repos/Lotus-Rob/project_n/rulesets` lists the id)
- `pixi run gh run list --branch <branch>` · `pixi run gh run watch <id>` · `pixi run gh run view <id> --log-failed`

## Gotchas
- `actions/cache` saves only when the job succeeds, so a failing job rebuilt everything every run. XMake package caches use separate `cache/restore` and `cache/save` steps, saving even after a failure.
- No disk cleanup step: ubuntu-26.04 runners have ~90 GB free, and the images plus packages need ~25 GB. Deleting the preinstalled SDKs cost ~2 minutes per job.

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|
