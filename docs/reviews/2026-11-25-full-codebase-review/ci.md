# Review: ci

**Scope:** .github/** (workflows, actions, rulesets, dependabot, actionlint, AGENTS.md): correctness, security (permissions, pinning, untrusted input), caching, speed, gaps between CI and the local `check` gate.

**Findings:** 13 standing (0 critical, 0 high, 0 medium, 6 low, 7 info), 1 raised but refuted on verification.

**Parts:** ci.md, ci-part2.md

## Assessment

The `.github/` scope is mature and deliberate. Actions are SHA-pinned and Pixi's version is exact. Job permissions are least-privilege: `contents: read` globally, `packages: write` only where GHCR images are pushed, `actions: write` only in `caches`, and `contents: write` only in `release`. There is no `pull_request` trigger, so no untrusted-code path can reach the secrets or write tokens. Logic is pushed into `tools/project_n.py`, so CI and local runs mostly can't drift. The cache design is carefully reasoned: the save key is the restore step's `cache-primary-key`, saves happen even after a failure, tag refs stay cold, and a per-ref prune keeps the 10 GB budget. The ruleset is tight: no bypass, strict status checks, linear history, rebase only.

Weaknesses are mostly second-order.
- Several `run:` steps interpolate `${{ }}` expressions straight into shell text (low risk here).
- The `caches` job uses `always()`, which also fires on a cancelled run, and `cancel-in-progress` is on.
- Tags have no protection, and the `release` job doesn't check that the tagged commit passed the main gate.
- Nothing is attested, checksummed or SBOM'd for the public release archives.
- CI misses a few things local `check` or `check --sanitizers` cover or that a gate could cover: Linux sanitizers, docs, fuzz smoke runs and any scheduled job.
- Runner images and Visual Studio paths are floating or hard-coded.
- The same LFS cache is uploaded redundantly by parallel jobs.
- Some comments and docs are stale: the fork-PR comment, "tags write no cache", and the claim that every job fetches LFS.

Overall health is good. I found no critical issue in scope.

**Files read in full:** (not reported)

## Findings

### CI-01: Release job does not verify the tag points at a main commit that passed the gate

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** security · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.github/workflows/ci.yml:433`

`release` runs for any `refs/tags/v*`. The ruleset (rulesets/main.json) targets only the default branch, and no tag ruleset exists. Anyone with write access can push a `v*` tag on any branch or an unreviewed commit. The full CI then builds that commit and `publish` drafts a release with it. Nothing checks that the commit is reachable from `origin/main`.

**Impact:** A mistaken or malicious tag on a feature branch produces a draft release with unreviewed binaries. The draft step limits the damage, but the artefacts are public-repo release material. Tags can also be deleted or moved, since no tag rule blocks it.

**Recommendation:** Add a tag ruleset (`target: tag`, `~ALL` or `refs/tags/v*`, with `deletion`, `non_fast_forward` and `update` rules). In `release` or `lint`, fail unless `git merge-base --is-ancestor $GITHUB_SHA origin/main`. Do the check inside project_n (`publish`) to follow the repo's rule.

**Verification:** Checked ci.yml:433-455 and rulesets/main.json. The release job only checks startsWith(github.ref,'refs/tags/v'). The ruleset targets only ~DEFAULT_BRANCH and has no tag rule, and nothing checks ancestry (no merge-base --is-ancestor in tools/project_n.py or ci.yml). So the gap exists. Impact is limited: the release is a draft that the user checks, the tag needs write access, and the full gate runs on the tagged commit. The release skill may name a procedure, which I did not check. Low severity.

### CI-02: `caches` job uses `always()`, so it still runs after the workflow is cancelled, and cancel-in-progress is on

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.github/workflows/ci.yml:414`

`if: always() && github.ref_type != 'tag'` (comment: to run after failed jobs). `always()` also runs the job when the run is cancelled. With `concurrency: cancel-in-progress: true`, every superseding push cancels the old run, and that old run's `caches` job still starts. It prunes the same ref's cache entries (`prune-ci-caches`) while the new run's jobs are restoring or saving.

**Impact:** Racy deletion of the entries the new run is about to restore. Possible cold builds and transient restore misses on branches with frequent pushes. A cancelled run also burns a runner and its `needs` wait.

**Recommendation:** Use `if: ${{ !cancelled() && github.ref_type != 'tag' }}`. This runs after failures but not after cancellation.

**Verification:** The `if: always() && github.ref_type != 'tag'` at line 414 is as quoted, and cancel-in-progress is true (line 14). always() does run a job after a cancellation, so !cancelled() is the more correct guard. The claimed race is speculative. Pruning deletes this ref's superseded entries, and the new run saves under new keys. Whether it can delete entries the new run is restoring depends on prune-ci-caches logic, which I did not trace. The likely harm is a wasted runner and an occasional cold build. Low severity.

### CI-03: Untrusted-ish context values interpolated directly into shell scripts

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** security · **Status:** new · **Reviewer confidence:** high
- **Where:** `.github/workflows/ci.yml:47`

`${{ github.ref }}` and `${{ github.event.before }}` are pasted into the `run:` script (lines 47-48). `${{ secrets.GITHUB_TOKEN }}` and `${{ github.actor }}` are pasted into the `podman login` line (lines 56, 119, 200). A branch name may legally contain `"`, `$()` or backticks. The expression is expanded before the shell parses it.

**Impact:** A user with push access could create a branch named to inject shell into `lint` (the job has `packages: write`). Risk is low because only writers can push and the repo has no `pull_request` trigger. It is still the classic script-injection pattern, and it becomes serious if a PR trigger is ever added.

**Recommendation:** Pass them via `env:` (for example `REF: ${{ github.ref }}`, `GH_TOKEN`, `ACTOR`) and use `"$REF"` in the script. Better, move the range computation into project_n.

**Verification:** Lines 47-48 interpolate github.ref and event.before directly. github.ref can be a branch name that contains shell metacharacters, and the token and actor are interpolated at line 56. This is the standard injection pattern. It is exploitable only by someone with push access, who could already change the workflow. The workflow has no pull_request trigger, and ci.yml:12 of AGENTS.md says never to add one carelessly. Hardening via env: is cheap. Low severity.

### CI-04: LFS composite action saves caches on tag refs and from every parallel job; the docs say tags write no cache

- **Severity:** low · **Verdict:** confirmed · **Category:** caching · **Status:** new · **Reviewer confidence:** high
- **Where:** `.github/actions/lfs/action.yml:27`

`Save LFS objects` has `if: cache-hit != 'true'` only. It has no `github.ref_type != 'tag'` guard, and it does not use `always()`. AGENTS.md says tags write no cache and the `caches` job (which skips tags) never prunes them. On a new branch's first push, eight or more jobs (lint, 3 container, releasedbg, 2 windows, 2 macOS) all miss the same key and each uploads the same `.git/lfs/objects`. The extras fail with 'Unable to reserve cache', after the upload work.

**Impact:** Wasted bandwidth and time per branch. On a tag whose LFS set changed, an unpruned tag-scoped cache is written. The doc claim is inaccurate.

**Recommendation:** Guard the save with `github.ref_type != 'tag'`. Optionally restrict saving to one job (for example lint). Correct the AGENTS.md sentence.

**Verification:** The Save LFS objects step has only `if: steps.restore.outputs.cache-hit != 'true'`. It has no tag guard. AGENTS.md line 13 says tags write no cache for the packages and objects layers. The LFS layer is not given the same exemption in the text. Parallel jobs saving the same key is a real but small waste.

### CI-05: CI lacks gates for things local `check --sanitizers`, docs and fuzzing cover; no scheduled job

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** ci-gap · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.github/workflows/ci.yml:1`

Only Windows and macOS run ASan, and only in debug-less `--sanitizer address` runs. Linux has no ASan, UBSan or TSan job (`check --sanitizers` is on request only), although the engine is multi-threaded (workers). There is no `docs` (Doxygen) build, no fuzz smoke run of `tests/fuzz/corpus` (seeds get added but never replayed in CI unless in tests), no coverage job, and no `schedule:` trigger. A cron would catch toolchain, runner-image and dependency drift, run nightly sanitizers and fuzzing, and warm main's caches.

**Impact:** Data races and UB can reach main unnoticed. A broken Doxygen or docs command is found only locally. Drift in floating runner images is found only on someone's next push.

**Recommendation:** Add a nightly or weekly scheduled workflow (outside the nine required checks): Linux ASan/UBSan/TSan via container, a `fuzz --time` smoke run, and `docs`. Alternatively, record in AGENTS.md that these are deliberately local-only.

**Verification:** There is no schedule trigger, and the Linux gate has no sanitizers. AGENTS.md line 12 and the project instructions say `check --sanitizers` is on request only, and Windows and macOS run address. `fuzz`, `docs` and `coverage` are local commands. Local-only sanitizers are therefore a stated policy, even if not explained as a trade-off in .github/AGENTS.md. The finding's 'docs build is only found locally' is partly offset by `doctor --docs`, which runs in lint. The nightly job is a reasonable suggestion, not a bug.

### CI-06: `cancel-in-progress` on main cancels the previous main run before its cold caches are saved

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** caching · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.github/workflows/ci.yml:12`

The concurrency group is per ref with cancel-in-progress true. Two PRs merged back-to-back cancel the first main run, which is the only one that saves main's cold object caches (branches fall back to main's caches). The first run also never validates the intermediate main commit.

**Impact:** Main's object caches are saved less often. Branch first-push runs may go cold. A broken intermediate commit goes unreported. This is minor, since the PR gate already ran.

**Recommendation:** Use `cancel-in-progress: ${{ github.ref != 'refs/heads/main' }}`, so main runs complete.

**Verification:** ci.yml:12-14 uses cancel-in-progress: true for every ref, including main. A second merge therefore cancels the first main run, which is the one that saves caches (lookup-only is false on main, lines 73, 135, 217). The cache loss is real but small, because the next main run saves them. Skipping validation of the intermediate commit is harmless, since the ruleset requires green PR CI. The suggested fix is sound.

### CI-07: Stale comment about fork PRs; the repo has no pull_request trigger, so fork PRs get no CI

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `.github/workflows/ci.yml:55`

The comment reads 'Fork PRs have no write token; project_n then builds the image instead of pulling it.' The workflow runs only on `push` and `workflow_dispatch`. Fork PRs trigger nothing, so the required checks never appear and a fork PR can never satisfy the ruleset. The `|| true` actually serves Dependabot's read-only token.

**Impact:** The comment misdocuments the threat model. It hides that outside contributions are structurally impossible to merge (maybe intended, but not stated).

**Recommendation:** Reword the comment to Dependabot's case. State in AGENTS.md that external fork PRs are unsupported by design, or add a `pull_request` strategy that excludes secrets and MSVC caches.

**Verification:** The comment says fork PRs, but the workflow has no pull_request trigger. AGENTS.md line 12 says explicitly that no workflow runs on pull_request, and line 24 says the `|| true` and PROJECT_N_PUSH_IMAGES handling exists for Dependabot's read-only token. So the comment is stale. The no-fork-PR design is already recorded in AGENTS.md, which is not stated as a contributor policy.

### CI-08: No supply-chain attestation, checksums or SBOM for public release archives

- **Severity:** info (reviewer said low) · **Verdict:** unverifiable · **Category:** security · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.github/workflows/ci.yml:433`

`release` downloads the archives and runs `project_n publish`. It has no `id-token` or `attestations` permission, no `actions/attest-build-provenance`, and no SHA256SUMS or SBOM step. The code-signing item is already parked.

**Impact:** Users cannot verify that the zips came from this repo's CI.

**Recommendation:** Add build provenance attestation (`attestations: write`, `id-token: write`) in the release job and publish a checksum file via `publish`.

**Verification:** The release job has contents: write only, with no attestation permissions, as the finding says. Whether checksums or an attestation are emitted inside `publish` I did not check. The finding admits that code signing is parked. This is a feature request, not a defect.

### CI-09: Dependabot covers only github-actions; pins elsewhere are manual

- **Severity:** info (reviewer said low) · **Verdict:** deliberate · **Category:** supply-chain · **Status:** new · **Reviewer confidence:** low
- **Where:** `.github/dependabot.yml:3`

Only `github-actions` is configured. The container base image, the apt snapshot, the Pixi release in the image, `PIXI_VERSION` and the XMake macOS bundle SHA are bumped by hand (bump-toolchain skill). `PIXI_VERSION` in ci.yml is not a Dependabot-visible pin. `setup-pixi` downloads the Pixi binary pinned by version only, with no checksum.

**Impact:** There are no automated security-update signals for the base image or Pixi. Versions stay frozen until someone remembers (by design, since pins are exact). A CVE in a pinned tool is invisible.

**Recommendation:** Enable the `docker` ecosystem for `containers/` (Dependabot supports Containerfile naming via a directory entry). Consider a periodic `pixi update --dry-run` report job.

**Verification:** AGENTS.md line 4 says dependabot.yml covers weekly updates of the pinned actions only. The project rule 'Pins are exact' and the bump-toolchain skill make the other pins manual and deliberate. A docker ecosystem entry could be added, but it is a suggestion, and Dependabot would conflict with the apt snapshot and Pixi pins.

### CI-10: Floating runner images and hard-coded Visual Studio path

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** reproducibility · **Status:** new · **Reviewer confidence:** low
- **Where:** `.github/workflows/ci.yml:253`

`ubuntu-26.04`, `macos-26` and `windows-2025-vs2026` are image labels whose contents update weekly. The MSVC cache and the cache restore/save path hard-code `C:\Program Files\Microsoft Visual Studio\18\Enterprise`. The install step finds the VS path through vswhere, and the cache step does not.

**Impact:** If the image changes edition or major version, the restore targets a path that is not the install. The job would restore nothing useful or fail in confusing ways. `actionlint.yaml` already needs a workaround for the unknown `ubuntu-26.04` label.

**Recommendation:** Add a guard step that asserts the vswhere path equals the hard-coded one (fail loudly). Record the runner image version (`ImageVersion`) in the log. Consider including `${{ runner.os }}` and the image version in the cache key.

**Verification:** ci.yml:265-266 and 284-285 hard-code VS\18\Enterprise while the install step (272-273) uses vswhere, so the mismatch is real. The failure is not silent, though. A wrong path means the cache saves nothing and the build fails at configure. Labels float by design. Low value.
