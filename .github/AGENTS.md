# .github/: CI workflows

## Scope
`workflows/ci.yml`: GitHub Actions for every push, tag and pull request.

## Rules
- Workflows only run `pixi run project_n <command>`; any logic beyond setup belongs in tools/project_n.py, so CI and local runs can't drift.
- Pin every action by commit SHA with the version in a comment; Pixi by exact version (`PIXI_VERSION`).
- Jobs: `linux` (`check --container`, then all artefacts), `windows` (native MSVC build, tests and ASan), `macos` (native macos-clang build, tests, ASan and artefact).
- Never upload the msvc image or anything from `/opt/msvc`; only `dist/` is uploaded.

## Commands
- `gh run list --branch <branch>` · `gh run watch <id>` · `gh run view <id> --log-failed`

## Gotchas

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|
