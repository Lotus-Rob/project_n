# Review: gd-roadmap-scope (part 2)

### GD-ROADMAP-SCOPE-11: Minimum-spec GPU cost never measured while fragment-heavy world-map passes keep growing

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** performance-risk · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-design.md:72`

The phase bar is a steady 60 fps on a GTX 960 / RX 470. It is judged by frame time on an RTX 4090 'with headroom', with GPU time read from SubmitFrame's wait (which measure misattributes; audit item). The world map now layers procedural paper grain and washes, a cloud fog shader, river and road ribbons, additive glows and mist. 0002 §8.1 still says 'GPU cost... wait for a minimum-spec GPU'.

**Evidence:** Phase 6 decision 9; 0002 line 349 'Still unmeasured: GPU cost'. The 5b-look terrain spec: paper grain and wash noise are procedural in the shader.

**Impact:** A 4090 is roughly 8-10x a GTX 960 in fill rate and shader throughput, so 'well under budget' on the dev machine says little. Fill-rate and overdraw problems would surface only at release.

**Recommendation:** Reopen decision 9 cheaply: a used GTX 960 or RX 470 costs very little, and an iGPU laptop is a useful lower bound. Add one measure run on it per renderer branch, and add Tracy GPU zones or RenderDoc timings.

**Verification:** Phase 6 decision 9 deliberately judges frame time on the dev machine with headroom. The backlog GPU estimates show the min-spec gap was considered. The finding that no min-spec hardware has measured the world-map passes is plausible and unrecorded. It is a hardware-purchase suggestion, not a defect. I did not verify the 'misattributes SubmitFrame wait' claim.

### GD-ROADMAP-SCOPE-12: Delta saves need an identical base world across binaries, but nothing checks it

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** determinism-saves · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:288`

A save stores only the difference from the world its seed makes. A load regenerates that base and applies the delta. The base's identity is (seed, GENERATOR_VERSION, GameData::BaseHash()), which covers data, not the generated world. Generation already draws f32 power per NPC (StartingPower), and procedural maps will add float noise. The recorded determinism goal is 'within one binary', but saves must load across patches, compilers and platforms (cloud saves between Windows and Linux/Deck).

**Evidence:** host.cpp ~164: archive(seed, GENERATOR_VERSION, baseHash, next); ~276-293 compares only the data baseHash. world_state.hpp:30 GENERATOR_VERSION = 1. npcs.hpp:73 'f32 power'. 0002 §7.4: 'Across compilers and platforms results may differ'.

**Impact:** After 1.0, a patch built with another compiler or other FMA contraction, or a save moved between OSes, could rebuild a slightly different base. The delta would then apply to the wrong values: a silent world change or an unexplained refusal. Before 1.0 it's hidden by the 'saves break freely' rule.

**Recommendation:** Store a hash of the generated base (e.g. WorldHash of the base) in the save and refuse with a clear message on mismatch. Make world generation integer or fixed-point only, and lint that. Add a CI check that all presets generate the same base hash for a few seeds. Write down that base generation needs a stronger guarantee than the simulation.

**Verification:** host.cpp lines 276-293 compare only data.BaseHash() against the stored baseHash, as the finding says. The save stores seed, GENERATOR_VERSION, baseHash and the id counter, not a hash of the generated world. 0002 limits determinism to one binary, and the 'no compatibility before 1.0' memory means saves may break freely until then. The cross-compiler float risk is real only after 1.0 or with cross-platform cloud saves. The finding's 'contradicts-recorded-decision' label is off, because the pre-1.0 rule covers it. A base-world hash in the save is a sound cheap guard.

### GD-ROADMAP-SCOPE-13: Phase 6 master spec's chunk table is stale

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** docs-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-design.md:110`

The table that serves as the roadmap marks 5b3 'To do' (merged, PR #47), 5b-look 'To do' (two parts merged) and 5c 'To do' (5c1 merged). It has no rows for roads, river heads, player walk, travellers, shared-routes, relationships bounded, long-run determinism, input latency or the text layout budget. The status line was last amended 2026-11-10.

**Evidence:** Lines 108-110: '| 5b3 | Fog and sight ... | To do |', '| 5b-look | ... | To do | (to be designed)', '| 5c | ... | To do |'.

**Impact:** Anyone using the roadmap (a fresh session, recall-history, the user) gets a wrong picture of progress and of how much chunk 5 has grown.

**Recommendation:** Update the table to what was built, with the inserted branches as rows. Consider requiring the table update in the finishing-a-branch checklist.

**Verification:** Checked the table: 5b3, 5b-look and 5c still read 'To do' (lines 110-112, not 108), while 5b2 and earlier say Done. The GD row also still says 'To do'. Recent branches (roads, river heads, travellers) have no rows. Real drift, but the table is a status index and the specs' Outcomes hold the real record, so low.

### GD-ROADMAP-SCOPE-14: Polish and engineering exceed proven fun (river heads, seals, scale)

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** over-engineering · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-24-river-heads-design.md:1`

Several recent branches invest full process (spec, plan, waves, final review, cross-model review) in presentation detail or scale for systems with no gameplay yet:
- river heads: where rivers start on a test map
- seals: 9 turning rings, satellites, pillars, a showcase
- shared-routes: hierarchical routing for very large procedural maps that don't exist
- per-frame text layout budgets and shimmer metrics

**Evidence:** River heads: 23 commits on 2026-11-24, a 219-line spec and a 266-line plan, three docs commits after the final review. Shared-routes stage 1: 10 tasks, 13 rulings, spikes S1-S8, with stage 2 to come.

**Impact:** Engineering hours go into things that may be redesigned or discarded once the core loop and generator exist, and the gap between engine maturity and game content keeps widening.

**Recommendation:** Adopt a 'fun first' rule for the GD roadmap: presentation polish and scale work for a system only after that system is proven in play in the slice. Keep a measured budget for perf work, as the user's 'measured spikes' rule intends, but tie it to the slice's real needs.

**Verification:** This is an opinion about proportion of effort, and the git history cannot decide it. Seals are a user-requested item (phase6 table row 4b 'added 2026-11-12, the user's'). Scale and perf work follows the recorded 'unknowns are measured spikes' and 'avoid wasted work' rules. The user chose all of it, so it is a priority call by the owner, not a defect. What would decide it is the user's own view on the fun-first ordering.

### GD-ROADMAP-SCOPE-15: Process overhead per change is very high

- **Severity:** low (reviewer said medium) · **Verdict:** deliberate · **Category:** process · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `AGENTS.md:30`

Each task runs:
- an implementer and an Opus full review
- one fix pass and a re-review
- test and lint
- two release builds in the main checkout

Each branch then runs:
- a spec and a plan, both approved by the user
- an Opus final review
- a review by four external models
- check
- nine CI checks (cold lint ~18 min, Windows container ~10 min)
- an Outcome section

Every Minor finding must be fixed. Sessions end on a context budget, each with a handover.

**Evidence:** 26% of commits are docs (253/975); since 2026-11-17 there are 91 docs commits against 38 feat. Specs and plans total ~21k lines against 42.7k lines of src. Process changes are branches of their own (superpowers-workflow, history-first, dev-loop, faster-check, ci-speed, build-folder-size, warnings, test-suite-names).

**Impact:** Throughput per feature is low and falling, and the user's time goes into approvals and rulings. A Tale of Immortal-scale game needs hundreds of content-sized changes that this ceremony can't carry.

**Recommendation:** Tier the process by risk:
- a fast path for presentation tweaks, data and test-map edits, and docs: no spec, an inline plan, one Sonnet review, Minor findings as notes
- the full path only for simulation, determinism, saves and renderer architecture
- the cross-model review only on those

Track throughput (feat commits per week, or slice features done) as a health metric.

**Verification:** The heavy process is the user's recorded decision: AGENTS.md 'Working with superpowers', plus memory notes on fixing every finding including Minor and on cross-model reviews at milestones. The AGENTS.md rules already tier review by task (review: full or light, Sonnet or Opus). The finding is itself marked contradicts-recorded-decision. The throughput concern is a fair opinion, but it is not a bug.

### GD-ROADMAP-SCOPE-16: Scale ambition isn't reconciled with population caps and memory budgets

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/world_map.hpp:57`

The user now asks for maps 2-50x larger, but MOST_WORLD_PEOPLE caps a map at 100,000 people, 0002 §13 budgets 50 MB of game state 'for a large world', and phase 4's gate is a 50,000-NPC month step. No document says whether larger maps mean the same people spread thinner, more people (millions at today's density), or aggregate tiers doing the work (0002 §7.7's aggregate tier exists only as Fold).

**Evidence:** world_map.hpp:55-57; 0002 §13 memory table; memory procedural-world-map.md 'maps vary per world and may be much larger (the user asked about 2x-50x)'.

**Impact:** Routing, saves, relationships and month-step design are being sized against an undefined target. Choices made for 'very large' maps may be unnecessary, or not enough.

**Recommendation:** In GD, fix target map sizes and populations for the slice and for 1.0, and the role of aggregate NPCs. Then size the routing and month-step work against those numbers.

**Verification:** I did not open world_map.hpp or 0002 §13 to re-check the 100,000 cap and the 50 MB budget. That no document ties target map sizes and populations to the scale ambition is plausible. The memory note procedural-world-map.md records the 2x-50x question as open. Setting target sizes and populations is a game-design decision for the GD brainstorm, which is still pending, so the gap is real but the decision isn't yet made.

### GD-ROADMAP-SCOPE-17: 0002 contradicts later decisions (modding non-goal, NPC actions as YAML)

- **Severity:** low · **Verdict:** confirmed · **Category:** docs-drift · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:29`

0002 §1 lists 'Networking, scripting and modding' as non-goals, but the user made mod support a high priority (2026-11-16). §7.7 says calendar steps use 'utility AI over actions defined in YAML data, so new behaviour is data, not code', but game::Action is a closed four-value enum scored in code. The doc is still 'Draft', and §16 ends at phase 7.

**Evidence:** 0002 line 29; line 309; src/game/npcs.hpp:56-61 enum class Action { Rest, Cultivate, Socialise, Travel }; npcs.cpp:73-74 scores by Action in code.

**Impact:** Readers and agents take the architecture doc as authority and get a wrong picture of modding and NPC behaviour.

**Recommendation:** In the planned 0002 refresh, update the non-goals and §7.7 to match code and decisions, mark it Accepted or Living, and point §16 to the GD roadmap.

**Verification:** 0002 line 29 lists modding as non-goal; line 309 says NPC actions are YAML data; game-design-gaps line 54 records the user's mod priority; npcs.hpp:56 has closed enum class Action. Doc status is Draft with a long Revised log. Drift is real, though the doc is a living draft and a refresh is planned.

### GD-ROADMAP-SCOPE-18: Skeletal animation runtime (DragonBones, A12) unverified while the art direction depends on it

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** technical-risk · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `docs/design/0002-engine-architecture.md:457`

A12 commits to DragonBones for heroes, dialogue characters and bosses. Spike S14 never ran, the core runtime hasn't changed since 2018, and the editor's status was unconfirmed ('dragonbones.com didn't answer'; perhaps succeeded by LoongBones). The backlog now says skeletal is 'its own future project later' and considers a rig of our own.

**Evidence:** 0002 §8.13 Status paragraph; §14 S14; sweep-up backlog 'Skeletal animation' item.

**Impact:** Character art production (the costliest asset class) can't start in a known format. A late switch would redo both art and runtime.

**Recommendation:** Run S14 (or an alternative's spike) before commissioning any character art. Record the decision in 0002 A12.

**Verification:** 0002 §8.13 status (line 457) and S14 (line 551) are as cited and the editor status is unconfirmed. But the backlog (line 103) records the user's decision that skeletal animation is a later separate project and that frames suffice for the initial slice, so no character art is blocked now. The residual risk is that A12 still names DragonBones, which should be reconciled in the doc. Medium is too high.

### GD-ROADMAP-SCOPE-19: Backlog and design gaps live in a 57 KB memory file outside the repo

- **Severity:** low · **Verdict:** confirmed · **Category:** process · **Status:** new · **Reviewer confidence:** medium
- **Where:** `/home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md:1`

The backlog has ~40 paragraph-length items. Its 'Priority order' item is mostly merged history, and it was last walked with the user on 2026-11-05. It and game-design-gaps.md sit in the user's Claude memory folder, not the public repo, and are recall-history's main inputs, so each recall reads a large file.

**Evidence:** wc: sweep-up-backlog.md 57,239 bytes, 109 lines. Header: 'Last walked together 2026-11-05'.

**Impact:** Context cost on every recall, items lost in long paragraphs, decisions not visible to anyone working from the repo, and a single point of loss if the memory folder goes.

**Recommendation:** Move durable items into the repo (GitHub issues, since the repo is public, or docs/backlog.md with one line per item and links to specs). Trim merged history, and walk it with the user at GD time.

**Verification:** sweep-up-backlog.md is 57,239 bytes and is in the user's memory dir, not the repo. AGENTS.md names it as a recall-history source, so the size and visibility facts hold. Where the backlog lives is the user's own setup, so this is a judgement call.

### GD-ROADMAP-SCOPE-20: AGPL licence vs store integration and art contributions undecided

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** licensing · **Status:** new · **Reviewer confidence:** medium
- **Where:** `README.md:61`

The game is AGPL-3.0-or-later. game-design-gaps lists store integration (achievements, cloud saves), but the Steamworks SDK is proprietary, so linking it with AGPL code needs an additional permission. Console ports are effectively excluded. Commissioned art would need a licence compatible with redistribution under the AGPL or a separate asset licence. 0001 says 'shipping on Steam isn't a requirement'. No document weighs these trade-offs.

**Evidence:** README.md:61; 0002 A12 and Appendix A cite the AGPL; 0001 line 779 on the Steam Runtime; game-design-gaps.md 'Shipping: store integration'.

**Impact:** Licence decisions get harder once outside contributions or commissioned art exist.

**Recommendation:** Record a licensing strategy before accepting contributions or commissioning art: an AGPL §7 linking exception for platform SDKs, a separate asset licence (e.g. all rights reserved or CC BY-NC), and a CLA if dual licensing is ever wanted.

**Verification:** game-design-gaps line 55 lists store integration and credits and licences as gaps, with no licensing strategy written down. I did not check README line 61 or 0001. The AGPL and Steamworks conflict is a real issue in principle, but it only matters once store integration is actually planned.
