# .github/: CI workflows

## Scope
- `workflows/ci.yml`: GitHub Actions for every push and tag, and manual runs. `actions/lfs/`: the Git LFS step every job but `release` and `caches` uses.
- `rulesets/main.json`: the rule on `main` (pull requests only, all nine checks green on an up-to-date branch, rebase merge, linear history, no bypass).
- `dependabot.yml`: weekly updates of the pinned actions. `actionlint.yaml`: runner labels actionlint doesn't know yet.

## Rules
- Workflows only run `pixi run project_n <command>`; any logic beyond setup belongs in tools/project_n.py, so CI and local runs can't drift.
- Pin every action by commit SHA with the version in a comment; Pixi by exact version (`PIXI_VERSION`).
- Jobs split `check` so they run in parallel: `lint`, `container` (per preset), `releasedbg`, `windows`, `macos`, `caches`, and on `v*` tags `release`; each job's first comment in `ci.yml` says what it runs. Locally, `check` stays the gate.
- One build per job on branch pushes (spec 2026-11-13 §5): a pull request waits for the slowest job, so a job that builds twice there is split. On main and tags `macos (release)` builds again for its clean-room artefact: the cold rule, not a job to split.
- Every push runs every job (the public repository's minutes are free), as does `pixi run gh workflow run ci --ref <branch>`. Never put an `if:` on a required job: GitHub counts a skipped job as passed, so the ruleset's check would be hollow.
- Never push or upload Microsoft's files (the MSVC install, anything from `/opt/msvc`): no image contains them, and only `dist/`'s zip archives are uploaded, named by `artifact`.
- Caching Microsoft's files in the Actions cache is accepted only because no workflow runs on `pull_request` (the repository is public): never add that trigger without keeping the MSVC caches out of it.
- Three cache layers (setup, packages, objects): see the comments at their steps in `ci.yml`. A save uses the key its restore step computed, never a fresh `hashFiles`; restore and save are separate steps, since `actions/cache` saves only after success.
- Main and tags compile cold (the cold rule: `cold_ref()`, tools/AGENTS.md). Git LFS comes through `actions/lfs` and Pixi's git-lfs, never `actions/checkout`'s `lfs`.
- The 10 GB budget: over it GitHub evicts the least recently used entries, main's first, and every first push goes cold. `caches` keeps one entry per family and version on each ref (`cache_family`): ~1.5 GB per ref plus ~3 GB of setup caches.
- A new cache needs a key `cache_family` parses, or it is never pruned.

## Commands
- Apply a changed ruleset (the user's say-so; it binds everyone): `pixi run gh api -X PUT repos/Lotus-Rob/project_n/rulesets/<id> --input .github/rulesets/main.json` (`pixi run gh api repos/Lotus-Rob/project_n/rulesets` lists the id)
- `pixi run gh run list --branch <branch>` · `pixi run gh run watch <id>` · `pixi run gh run view <id> --log-failed`

## Gotchas
- MSVC's objects stay out of the object cache (a cached `.obj` loses `/Zi`'s debug types and no longer matches an uncached build; under Wine it is no faster): see the Outcome of `docs/superpowers/specs/2026-11-13-ci-and-local-speed-design.md`.
- Dependabot's push runs get a read-only token: pushing a missing image to GHCR failed them, and with every check required their pull requests could never merge. Keep the `PROJECT_N_PUSH_IMAGES` condition (ci.yml) when touching the expression.
- No disk cleanup step: ubuntu-26.04 runners have ~90 GB free, and the images plus packages need ~25 GB; deleting the preinstalled SDKs cost ~2 minutes per job.
