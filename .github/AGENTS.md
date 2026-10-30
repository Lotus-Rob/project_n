# .github/: CI workflows

## Scope
`workflows/ci.yml`: GitHub Actions for every push, tag and pull request.

## Rules
- Workflows only run `pixi run project_n <command>`; any logic beyond setup belongs in tools/project_n.py, so CI and local runs can't drift.
- Pin every action by commit SHA with the version in a comment; Pixi by exact version (`PIXI_VERSION`).
- Jobs split `check` so they run in parallel: `lint` (commit-check, `doctor --docs`, `fmt --check`, `lint --container`), `container` (one clean-container `artifact` job per preset: build, tests, dependency check, upload), `windows` (native MSVC build, tests, ASan), `macos` (native build, tests, ASan, artefact). Locally, `check` stays the gate.
- Never push or upload the msvc image or anything from `/opt/msvc`; only `dist/` is uploaded. `IMAGES[...]["push"]` guards the registry.
- Caches: Pixi envs (setup-pixi), XMake packages per job (key: pixi files, `xmake-requires.lock`, `xmake/repo/`), the linux image in GHCR by content tag, and the native MSVC 14.50 toolset (key: `buildtools.vsconfig`).

## Commands
- `gh run list --branch <branch>` · `gh run watch <id>` · `gh run view <id> --log-failed`

## Gotchas

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|
