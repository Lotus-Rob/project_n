# Review: agent-docs (part 2)

### AGENT-DOCS-11: README and doctor hints use pacman, which conflicts with the user's shelly rule for this machine

- **Severity:** info (reviewer said low) · **Verdict:** unverifiable · **Category:** rule-conflict · **Status:** new · **Reviewer confidence:** medium
- **Where:** `README.md:12`

README (lines 12-14) and `INSTALL_HINTS` in tools/project_n.py give `sudo pacman -S git|pixi|podman`. The user's global instructions say package commands given to them use `shelly`. The root AGENTS.md says to relay doctor's install instructions to the user.

**Impact:** An agent following 'give the user doctor's instructions' hands a CachyOS user pacman commands they have said they do not want. The README is public, so the hints should stay generic.

**Recommendation:** Have the agent translate the hint for this machine (the global rule already says so), or keep `pacman` in the README for Arch and add a note.

**Verification:** I did not read README or INSTALL_HINTS. The global rule says agents translate commands for this machine, and the README is public, so this is a policy choice.

### AGENT-DOCS-12: Skill ownership and the 'owner lists it under Skills' rule is not followed consistently

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** docs-consistency · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.claude/skills/git-commit/SKILL.md:6`

The root says a skill names the AGENTS.md that owns it, which lists it under Skills. git-commit says it is owned by the root AGENTS.md, but `tools/AGENTS.md` also lists it under Skills ('for SCOPES changes'). `handover`, `release` and `add-dependency` are owned by the root AGENTS.md, which has its own Skills section (so that is fine). `bump-toolchain` is owned by containers/AGENTS.md and is listed there. `fix-xmake-package` is owned by the root AGENTS.md, which is not stated anywhere else.

**Impact:** Minor. Two homes for one skill is how entries go stale.

**Recommendation:** Pick one owner per skill and list it once.

**Verification:** git-commit skill says 'Owned by: root AGENTS.md'; root lists it (line 66) and tools/AGENTS.md:27 also lists it 'for SCOPES changes'. Two listings exist, but the second is a pointer from the file that holds SCOPES, so it is a cross-reference rather than a competing owner. The claim about fix-xmake-package being 'not stated elsewhere' is true but not a defect (root lists it, line 69). Trivial.

### AGENT-DOCS-13: Docs name tools the rules forbid calling in docs, and run generators outside project_n.py

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** rule-conflict · **Status:** new · **Reviewer confidence:** low
- **Where:** `AGENTS.md:38`

The root says tools/project_n.py is 'the only entry point' and not to call podman or xmake directly in docs. The root documents `pixi run python art/world_map.py --roads`. `fix-xmake-package` mentions `xmake f -v` and `podman unshare`. README's Podman row describes `podman machine init`.

**Impact:** Mostly harmless, but the rule is stated as absolute and is not followed, which weakens trust in the other absolute rules.

**Recommendation:** Add a `project_n world-roads` wrapper, or soften the rule to 'workflow commands'.

**Verification:** Root documents `pixi run python art/world_map.py --roads` (a direct generator call), and fix-xmake-package SKILL.md lines 19 and 49 mention `xmake f -v` and `podman unshare`. But the rule is 'Don't call podman, xmake or git-cliff directly in docs or scripts; go through project_n.py'. The skill's `xmake f -v` is described as what project_n runs, and `podman unshare` is a debugging hint, so those are borderline. The world_map.py case is a real exception to 'only entry point' wording. Impact is cosmetic.

### AGENT-DOCS-14: The count 'nine checks' is hardcoded in two AGENTS.md files

- **Severity:** info · **Verdict:** confirmed · **Category:** docs-drift · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.github/AGENTS.md:4`

'All nine checks green' appears in `.github/AGENTS.md` (Scope) and in the root's Finishing a branch step 4. The ruleset JSON is the actual source. The job matrix (windows, macos entries, one container per preset) changes the count whenever a job is added.

**Impact:** Adding or removing a job makes two prose counts wrong, and nothing checks them.

**Recommendation:** Drop the number or add a self-test comparing it with `.github/rulesets/main.json`.

**Verification:** 'nine checks' appears in .github/AGENTS.md:4 and root AGENTS.md:47, with the authority in .github/rulesets/main.json. A count is hardcoded in two places; whether anything checks it I did not find. Minor drift risk.

### AGENT-DOCS-15: The workflow stacks many review layers for single-developer tasks

- **Severity:** info · **Verdict:** deliberate · **Category:** process-overhead · **Status:** new · **Reviewer confidence:** low
- **Where:** `AGENTS.md:53`

Per task: an implementer, a full Opus review (or light Sonnet review), one fix pass, a re-review, plus lint and test. Per wave: warm release builds for two presets by the controller. Per branch: `check`, a final Opus whole-branch review, four cross-model reviews (global rule), another `check`, then a PR with nine CI jobs that each run on every push. There are also parallel worktrees and in-wave rebasing.

**Impact:** Large wall-clock and token cost per change. The docs and the review-history record show that none of these layers has been measured against the bugs it caught. There is no recorded rule for when a small change may skip layers.

**Recommendation:** Record which review layer catches what, drop the least effective, and write a lightweight path (for example docs-only or mechanical changes) into the root AGENTS.md.

**Verification:** The layering is recorded in root AGENTS.md 'Reviews sized to the task': light vs full review, one fix pass, Sonnet re-reviews, and a final review run once. Light tasks (docs, data, mechanical moves) are already the lightweight path. The user's memory rules (fix every finding, verify per task) back this. The missing measurement of review effectiveness is an opinion, not checkable by reading.

### AGENT-DOCS-16: Memory index and AGENTS.md duplicate and can contradict each other

- **Severity:** info · **Verdict:** deliberate · **Category:** docs-drift · **Status:** new · **Reviewer confidence:** low
- **Where:** `AGENTS.md`

Rules such as small unit tests, no design references in code comments, lint fix-first, pre-1.0 compatibility, handover and the context budget exist in both the memory index and AGENTS.md or the skills. The memory index is outside the repo, is not reviewed and is not covered by `doctor --docs`.

**Impact:** The two sources can diverge: an agent reads both on every session, and a collaborator sees only AGENTS.md. Which one wins is unwritten.

**Recommendation:** Leave the rule in the repo and keep only a pointer in memory, or state that AGENTS.md wins.

**Verification:** The memory index is the user's private global store, outside the repo by design (user CLAUDE.md). Root AGENTS.md itself cites the project memory for in-flight.md and sweep-up-backlog.md. Duplication of rules is real, but which source wins is unrecorded. A style opinion with no concrete failure.
