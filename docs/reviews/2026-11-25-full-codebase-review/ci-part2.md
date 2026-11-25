# Review: ci (part 2)

### CI-11: Artifacts uploaded for every branch push without a retention limit

- **Severity:** info · **Verdict:** confirmed · **Category:** cost · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.github/workflows/ci.yml:161`

`upload-artifact` has no `retention-days` (default 90). It runs on every push for 3 container presets plus macOS release, and the zips contain game builds.

**Impact:** Wasted artifact storage. The public repo accumulates build zips that, for MSVC, are Wine-built binaries.

**Recommendation:** Set `retention-days` to about 7 for non-tag runs, and keep the default only for tags and the release path.

**Verification:** upload-artifact at ci.yml:161 and 384 has no retention-days, so it keeps the 90-day default on every branch push. The same artifacts feed the release job (download-artifact at ci.yml:445), so tag runs need a longer retention. A conditional retention is a valid cost tweak. Negligible impact.

### CI-12: Windows `doctor` step swallows all failures other than exit code 3 only through pwsh semantics; `address` run does not cover commit-check or lint on Windows

- **Severity:** info · **Verdict:** deliberate · **Category:** ci-gap · **Status:** new · **Reviewer confidence:** low
- **Where:** `.github/workflows/ci.yml:304`

`pixi run project_n doctor; if ($LASTEXITCODE -notin 0, 3) { exit 1 }; exit 0` makes doctor report-only. This is deliberate, but the semantics differ from the macOS line (`|| [ $? -eq 3 ]`). Native Windows never runs `lint`, `fmt` or `reference`. The Windows `release` build is the only native-MSVC exercise of `artifact` or packaging paths (artifact comes from the Wine container).

**Impact:** Windows-specific path bugs in `lint`, `fmt` or `artifact` are caught only on the dev machine.

**Recommendation:** Run `project_n selftest` in both Windows entries (not just address), or document why one entry suffices. Consider a native `artifact --reuse-build` smoke on Windows like macOS.

**Verification:** ci.yml:301-302 shows selftest runs on one Windows entry on purpose, with a comment: 'the only place it runs on Windows (paths, shims): the shorter build's entry'. The doctor handling at 303-304 and 371 is documented as report-only, with exit code 3 allowed. Lint and fmt run in the lint job, and the artifact is built in containers by design. The Windows artifact smoke test is a speculative suggestion.

### CI-13: Ruleset: no required review, no CODEOWNERS or SECURITY.md, no signed-commit or tag protection

- **Severity:** info · **Verdict:** deliberate · **Category:** governance · **Status:** new · **Reviewer confidence:** low
- **Where:** `.github/rulesets/main.json:14`

`required_approving_review_count` is 0 and no `CODEOWNERS` exists. `.github/workflows/**`, `.github/rulesets/**` and `tools/project_n.py` (which CI trusts) are changeable by any writer through a PR whose CI runs the changed workflow. There is no SECURITY policy for the public repo.

**Impact:** Fine for a single-owner repo. If collaborators are added, a PR can alter workflows to exfiltrate `GITHUB_TOKEN` (limited to read plus packages write).

**Recommendation:** Add a CODEOWNERS entry for `.github/` and `tools/`, and a `SECURITY.md`. Set `require_code_owner_review` when more writers join.

**Verification:** main.json sets required_approving_review_count 0 and require_code_owner_review false, and AGENTS.md says the user merges every PR as the single owner. No CODEOWNERS or SECURITY.md exists. The finding is governance advice that only matters if collaborators join, and it names no current defect.

### CI-14: `main` ref hard-coded, and a commit-range edge cases in lint

- **Severity:** info (reviewer said low) · **Verdict:** refuted · **Category:** correctness · **Status:** new · **Reviewer confidence:** low
- **Where:** `.github/workflows/ci.yml:47`

The range logic compares `github.ref` to the literal `refs/heads/main`. The ruleset uses `~DEFAULT_BRANCH`. For `workflow_dispatch` on main, or a tag push, `github.event.before` is empty, so the range falls to `origin/main..HEAD`. That range is empty on main or a tag on a main commit. It depends on `commit-check` and `fmt` accepting an empty range. On a main push, `before..HEAD` is used. A force push or a new-branch push gives `before` = 0000 and falls back to `origin/main..HEAD`, which is empty after the merge. Main would then get no commit or lint check on that push.

**Impact:** A force-pushed or manually dispatched main run lints nothing. The ruleset forbids force pushes, so this is unlikely. A comparison with the diff for the whole main push (the PR's already-checked commits) is redundant work on every merge.

**Recommendation:** Move the range selection into project_n (it already has `commit_range()`). Make main and tag runs lint `--all`, or document that an empty range passes. Use `github.event.repository.default_branch`.

**Verification:** ci.yml:47-51 uses before..HEAD on a main push, which is the pushed commits. The fallback to origin/main..HEAD applies only to new-branch or force-push cases. The comment at 44-45 says this is intended. The ruleset (main.json) forbids force pushes and requires PRs with strict up-to-date checks. Every main commit was already linted on its PR, so an empty range loses nothing. workflow_dispatch and tag runs are edge cases with no real impact.
