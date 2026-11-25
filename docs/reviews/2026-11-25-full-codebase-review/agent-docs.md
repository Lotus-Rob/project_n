# Review: agent-docs

**Scope:** README.md, every AGENTS.md / CLAUDE.md in the repo, .claude/** (settings.json, skills, agents, marketplace): accuracy versus the repo (commands that do not exist, stale paths, contradictions between files), rules that conflict, process overhead and risks of the agent workflow itself.

**Findings:** 16 standing (0 critical, 0 high, 0 medium, 9 low, 7 info), 0 raised but refuted on verification.

**Parts:** agent-docs.md, agent-docs-part2.md

## Assessment

## Assessment: agent docs and .claude/**

I read README.md, every AGENTS.md and CLAUDE.md, all seven skills, the `log-triage` agent, `.claude/settings.json`, and the marketplace and LSP files in full. I checked them against `tools/project_n.py`, `.gitignore` and the repo tree. I confirmed that every `.md` and `.py` path quoted in the docs exists, apart from memory-folder files (`in-flight.md`, `sweep-up-backlog.md`) and a few generic file names.

**Strengths**
- Ownership is clear: each skill names the AGENTS.md that owns it, and the root AGENTS.md maps each directory to its guide.
- Commands are mostly accurate. `doctor --docs` checks that quoted commands exist.
- Gotchas are concrete and carry evidence, which makes them useful to debug from.
- The `.claude` setup (a local clangd marketplace through Pixi, the Artifact tools denied, skill overrides) is small and sensible.
- Rules are mostly enforced by `lint` or `commit-check`, not by prose alone.
- Rationale is recorded in specs and design docs, and `.superpowers/sdd/` is hidden by its own inner `.gitignore`, as the root claims.

**Weaknesses**
- The rule "an AGENTS.md is ~100 lines, one line per rule" is broken by the files that need it most.
  - `src/AGENTS.md` is 45 KB. One line is 15,404 characters and two more are over 7,000.
  - `tools/AGENTS.md` is 21 KB, `tests/AGENTS.md` 19 KB, the root 16 KB, and `.github/AGENTS.md` 8.5 KB (about 3 KB of that is one line).
  - Everything under `src/` loads in full on any task there, which costs tokens every turn.
  - The prose is hard to scan and easy to contradict.
  - Much of it is changelog-style narrative (dated spike results, "2026-11-12" measurements, a `ViewReport`/`WorldLook` walkthrough) that belongs in specs or design docs.
- Sources of truth are duplicated and can drift. Rules are repeated across the root AGENTS.md, the memory index and the skills.
- The process is heavy for a single-developer repo, and depends on things outside the repo.
  - Per task: implementer, full Opus review, one fix pass, re-review, lint, test, then two release builds.
  - Per branch: `check`, final Opus review, the cross-model review on four more models, `check` again, PR, nine CI checks.
  - The context-budget rule depends on a user hook, `~/.claude/hooks/context_budget.py`, that is not in the repo.
  - The handover state lives in memory outside the repo.
  - The rules also depend on the unpinned superpowers plugin continuing to behave as described.

**Overall health:** good on accuracy for a doc set this size. I found one hard error, in the git-commit examples. The main risk is size and density. Second is rule conflicts between the repo and the user's global instructions.

**Files read in full:** (not reported)

## Findings

### AGENT-DOCS-01: git-commit skill examples use scopes that commit-check rejects

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** docs-accuracy · **Status:** new · **Reviewer confidence:** high
- **Where:** `.claude/skills/git-commit/SKILL.md`

The Examples block shows `feat(render): ...` and `fix(audio): ...`. `SCOPES` in tools/project_n.py (line 117) is repo, env, containers, tools, xmake, deps, tests, design, agents, release, core, data, platform, gfx, game, app. `render` and `audio` are not in it. `commit-check` (line ~3028) reports `unknown scope`. The skill says not to copy the list, yet its examples are not drawn from it.

**Impact:** An agent that copies the examples verbatim, the way agents do, writes a message the commit-msg hook rejects. This wastes a turn, and it teaches a wrong scope vocabulary.

**Recommendation:** Use real scopes in the examples (`feat(gfx): ...`, `fix(game): ...`). Consider a self-test that parses the skill's examples through `commit-check`.

**Verification:** git-commit SKILL.md lines 58-59 use feat(render) and fix(audio). SCOPES (tools/project_n.py:117) has neither. Examples that would be rejected; low impact since agents check the scope list.

### AGENT-DOCS-02: AGENTS.md files far exceed the stated size and format rule

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** agent-workflow · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/AGENTS.md:4`

The root says an AGENTS.md holds ~100 lines, one line per rule. src/AGENTS.md is 49 lines but 45 KB. Line 27 (the scene/host paragraph) is 15,404 characters, line 4 (Scope) 7,276 and line 24 (Rendering) 7,254. tools/AGENTS.md is 21 KB, with gotchas of ~3 KB each. tests/AGENTS.md is 19 KB, with line 20 at 5.8 KB. The root is 16 KB, and the `.github/AGENTS.md` cache bullet is ~3 KB. Much of the text is a data-flow narrative of how each YAML file is loaded, or dated measurement logs, not rules.

**Impact:** Every session touching `src/` loads about 11k tokens of dense prose. Contradictions and stale facts hide inside paragraphs that nobody can scan. Updating them in the same commit as the code (as the rules demand) is error-prone. `doctor --docs` cannot catch drift in prose.

**Recommendation:** Split the narrative into design docs or specs and leave AGENTS.md with one-sentence rules and pointers. Enforce the budget in `doctor --docs`, for example a maximum line length and a maximum file size.

**Verification:** src/AGENTS.md is 45264 bytes and tools/AGENTS.md is 21137, far past the '~100 lines, one line per rule' budget. Size is real. Impact is token cost, not a correctness bug.

### AGENT-DOCS-03: Global rule 'low = Minor, note it' conflicts with the project's 'fix every finding, Minor included'

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** rule-conflict · **Status:** new · **Reviewer confidence:** medium
- **Where:** `AGENTS.md:79`

Finishing a branch step 3 says to fix every finding, Minor included, and that this overrides superpowers' 'Minor to ledger'. The user's global CLAUDE.md says the cross-model review's low findings are Minor and to 'note it'. Neither file says which wins for cross-model lows.

**Impact:** Cross-model lows are the least reliable findings. An agent could burn fix passes on speculative low-confidence items, or skip them and break the project rule, and either reading can be defended.

**Recommendation:** State in the root AGENTS.md that cross-model lows are verified first and then fixed or ruled wrong with a reason, or say explicitly that they are noted only.

**Verification:** The two rules do differ (the global one says low is Minor, note it; the project one says fix Minor too). The project rule is explicitly project-scoped and overriding, so the likely reading is clear. Only the user can say whether it extends to cross-model lows.

### AGENT-DOCS-04: src/AGENTS.md says 'phases 1 to 5 of §16 implemented', which no longer matches the work

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** stale-doc · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `src/AGENTS.md:4`

The scope line says design doc 0002 §16 phases 1 to 5 are implemented. §16 phase 6 is 'UI and text' (RmlUi, SDL3_ttf), and the repo has phase-6 specs through 'chunk5b-look-travellers' and 'river-heads'. The review history already notes that 0002.4 uses stale chunk numbers and that 0002 predates superpowers. The docs also mix 'phase 6 chunk N' with plain feature names.

**Impact:** A reader cannot tell what is built. The 'phase' vocabulary is also what the lint rule forbids in code comments, so the docs are the only place it survives.

**Recommendation:** Replace the phase claim with a pointer to the current list of implemented subsystems, or update it and drop the claim from AGENTS.md.

**Verification:** src/AGENTS.md line 4 does say 'phases 1 to 5 of §16 implemented'. I did not check §16 or the specs to confirm phase 6 work exists. Likely stale wording; status known-deferred per the reviewer.

### AGENT-DOCS-05: look-at-the-game fetches an unlocked tool at runtime, against the pinned-environment rule

- **Severity:** low · **Verdict:** confirmed · **Category:** rule-conflict · **Status:** new · **Reviewer confidence:** medium
- **Where:** `.claude/skills/look-at-the-game/SKILL.md`

The skill runs `pixi exec --spec pillow==13.0.0 python -I <script>` to compare pictures. The root says every tool comes from the Pixi environment (`pixi.toml` / `pixi.lock`) and that pins live in the lock. `pixi exec` fetches from conda at run time, outside the lock.

**Impact:** The tool is pinned in the command but missing from the lockfile. It can break offline, and the repo's host-leak guarantees do not cover it.

**Recommendation:** Add pillow to a Pixi feature, or move the diff into a `project_n` command.

**Verification:** look-at-the-game SKILL.md line 22 uses 'pixi exec --spec pillow==13.0.0'. pillow does not appear in pixi.toml, so it is outside the lock. It is pinned in the command and used only for ad-hoc diffs, so the impact is small.

### AGENT-DOCS-06: The log-triage agent's AGENTS.md Gotchas grep skips .github, and its read-only rule is not enforced

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** agent-workflow · **Status:** new · **Reviewer confidence:** high
- **Where:** `.claude/agents/log-triage.md:12`

Step 4 runs `grep -rn Gotchas -A20 AGENTS.md */AGENTS.md .claude/skills`. The shell glob `*/AGENTS.md` does not match the hidden `.github/` directory, so the CI Gotchas (Dependabot, caches) are never searched. `-A20` also truncates longer Gotchas sections. The agent is told 'read only: never edit, rebuild or rerun', but `tools: Read, Grep, Glob, Bash` gives it a full shell.

**Impact:** CI-log triage misses known CI gotchas. Nothing technical stops the agent from running builds.

**Recommendation:** Use `grep -rn 'Gotchas' --include=AGENTS.md --include=SKILL.md -A40 .` and consider dropping Bash in favour of Read and Grep.

**Verification:** log-triage.md line 12 uses 'grep -rn Gotchas -A20 AGENTS.md */AGENTS.md .claude/skills'. The shell glob '*/AGENTS.md' skips hidden .github/, which does have an AGENTS.md. -A20 truncation is plausible. I did not check the tools list for the read-only point.

### AGENT-DOCS-07: Context-budget process rests on a user-local hook and on memory outside the repo

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** process-risk · **Status:** new · **Reviewer confidence:** medium
- **Where:** `AGENTS.md:57`

The 150k/250k budget rule, the `in-flight.md` handover and the 'continue where we left off' routine all depend on `~/.claude/hooks/context_budget.py` and on the user's project memory folder. Neither is in the repo. The fallback is one task per session. The handover skill also has agents rewrite one shared `in-flight.md` with the instruction 'every other section unchanged'. Parallel waves and worktrees share that file, so it has no locking.

**Impact:** On another machine, in CI or a fresh clone, the rules silently degrade. Two parallel sessions that both write a handover can overwrite each other's section. The decisions the skill says must not live only there are enforced by convention.

**Recommendation:** Commit a template or a hook script (or a note on it) under `tools/`, or keep per-branch handover files in the repo (gitignored) and cite them. Make sure handover writes are atomic, or write one file per branch.

**Verification:** Root AGENTS.md says the hook and in-flight.md live in the user's own ~/.claude and project memory, and states the fallback (one task per session without the hook). So the degradation is documented and deliberate, and it is user-specific by design. The shared-file concurrency concern is plausible but I did not verify the handover skill's text, and handover is written by one session at a time at stops. Low.

### AGENT-DOCS-08: The committed settings.json hardcodes one user's plugin set and does not pin the plugin

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** config · **Status:** new · **Reviewer confidence:** low
- **Where:** `.claude/settings.json:22`

`skillOverrides` lists `anthropic-skills:*` and other skill names from the user's own installation. `superpowers@claude-plugins-official` is enabled with no pinned version, and the root AGENTS.md states that its lines 'win over its skills'. `permissions` only denies the Artifact tools; no allow list is committed, and there are no hooks to enforce 'never use host compilers' or the commit-before-lint rules.

**Impact:** The repo is public. A collaborator or another machine gets irrelevant overrides, and a superpowers update can change skill behaviour so that the documented overrides no longer match. Prompts for routine `pixi run project_n` calls are unavoidable.

**Recommendation:** Move the user-specific overrides to the user's settings, state the superpowers version these notes were written against, and consider a committed allow list for read-only project_n commands.

**Verification:** settings.json does list anthropic-skills:* and other user-installed skill overrides, and superpowers is enabled with no version pin; permissions only deny the Artifact tools. Root AGENTS.md states the overrides are deliberate (hides unused skills). Overrides for skills a collaborator lacks are harmless. Pinning is a fair point but the repo is single-developer. Low.

### AGENT-DOCS-09: The clangd plugin covers only .cpp and .hpp and assumes a Pixi env in every checkout

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** config · **Status:** new · **Reviewer confidence:** low
- **Where:** `.claude/marketplace/plugins/clangd/.lsp.json:5`

`extensionToLanguage` maps only `.cpp` and `.hpp`. The command runs `pixi run --manifest-path ${CLAUDE_PROJECT_DIR}/pixi.toml --frozen --quiet clangd`. The root AGENTS.md says the LSP tool reads `build/compile_commands.json`, which only native builds refresh.

**Impact:** Headers named `.h` and sources named `.cc` or `.mm` (macOS) are not served. In a fresh worktree, `.pixi` is absent and `compile_commands.json` is missing or stale, so `findReferences` returns empty or partial results. The root rule 'use LSP, not grep, for callers' then fails silently.

**Recommendation:** Add `.h`, `.cc` and `.mm` if the repo uses them. Document that a worktree needs a native build first, or point clangd at the main checkout's compile database. Have the reviewer fall back to grep when LSP returns nothing.

**Verification:** .lsp.json maps only .cpp and .hpp, as stated. A git ls-files search for .h, .cc and .mm files returned nothing, so the repo has no such sources and the extension gap has no effect today. The dependence on build/compile_commands.json is already documented in root AGENTS.md. The fresh-worktree concern is plausible, but I did not confirm it.

### AGENT-DOCS-10: Skill 'Gotchas' headings are empty in release and look-at-the-game

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** docs-hygiene · **Status:** new · **Reviewer confidence:** high
- **Where:** `.claude/skills/release/SKILL.md:43`

Both skills end with a bare `## Gotchas` heading and no content. In look-at-the-game the only gotcha-like line, 'Keep the window focused', sits under a different heading.

**Impact:** Minor noise. The heading suggests a convention nobody fills in.

**Recommendation:** Remove the empty headings or fill them in.

**Verification:** '## Gotchas' exists at release/SKILL.md:43 and look-at-the-game/SKILL.md:28. I did not confirm whether either section is empty (grep -A3 printed no following lines, which suggests they are). Trivial either way.
