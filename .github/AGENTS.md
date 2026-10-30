# .github/: CI workflows

## Scope
`workflows/ci.yml`: GitHub Actions for every push, tag and pull request.

## Rules
- Workflows only run `pixi run project_n <command>`; any logic beyond setup belongs in tools/project_n.py, so CI and local runs can't drift.
- Pin every action by commit SHA with the version in a comment; Pixi by exact version (`PIXI_VERSION`).
- Jobs split `check` so they run in parallel: `lint` (commit-check, `doctor --docs`, `fmt --check`, `lint --container`), `container` (one clean-container `artifact` job per preset: build, tests, dependency check, upload), `windows` (native MSVC build, tests, ASan), `macos` (native build, tests, ASan, artefact). Locally, `check` stays the gate.
- Never push or upload the msvc image or anything from `/opt/msvc`; only `dist/` is uploaded. Caching Microsoft's downloads in the Actions cache is accepted (the repo goes private once setup is done). `IMAGES[...]["push"]` guards the registry.
- Two cache layers. Setup, saved as soon as its step finishes: Pixi envs (setup-pixi), the linux and wine images in GHCR by content tag, the native MSVC 14.50 toolset (key: `buildtools.vsconfig`), the macOS XMake build (key: `XMAKE_MACOS_SHA256`), Microsoft's MSVC download for the msvc image (key: `msvc.Containerfile`). Packages, saved at job end even on failure: XMake packages per job (key: pixi files, `xmake-requires.lock`, `xmake/repo/`).

## Commands
- `gh run list --branch <branch>` · `gh run watch <id>` · `gh run view <id> --log-failed`

## Gotchas
- `actions/cache` saves only when the job succeeds, so a failing job rebuilt everything every run. XMake package caches use separate `cache/restore` and `cache/save` steps, saving even after a failure.
- No disk cleanup step: ubuntu-26.04 runners have ~90 GB free, and the images plus packages need ~25 GB. Deleting the preinstalled SDKs cost ~2 minutes per job.

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|
