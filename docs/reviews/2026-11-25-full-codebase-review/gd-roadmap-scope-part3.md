# Review: gd-roadmap-scope (part 3)

### GD-ROADMAP-SCOPE-21: Demo-arena rule and four dev scenes add per-feature upkeep that won't scale with content

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** architecture-process · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/AGENTS.md:27`

Every new feature must show up in the arena in the same branch and be added to arena.hpp's header list (already ~40 lines). Four dev scenes (arena, stress, painted, cells) with scratch worlds, portals and saves are kept alongside the game world.

**Evidence:** src/AGENTS.md:27 'a new feature of the game or engine shows up in it, in the same branch, and is added to the header's list'; arena.hpp header doc.

**Impact:** Each gameplay system pays a showcase tax, and dev scenes accumulate save and scratch-world complexity (BuildScratch, devScene saves).

**Recommendation:** Once the game world hosts features, retire the arena-showcase rule in favour of per-system test scenes or reference cases. Keep only stress for benchmarks.

**Verification:** src/AGENTS.md:27 does contain the rule that features show up in the arena and are added to the header's list, and the dev scenes exist. Whether this is a burden is opinion. The arena is described as the growing demo scene, which suggests it is deliberate, and the only evidence of upkeep cost is the claimed ~40-line header.

### GD-ROADMAP-SCOPE-22: No build has reached a player since v0.1.0

- **Severity:** low · **Verdict:** confirmed · **Category:** process · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `CHANGELOG.md:1`

The only tag is v0.1.0 (2026-10-30, day 2). No playtest builds and no playtesting process exist, though release tooling (artefacts for every preset) is in place.

**Evidence:** git tag: v0.1.0 only. game-design-gaps.md 'a playtesting and QA process' under Shipping.

**Impact:** All feedback comes from one developer and agents; problems with fun, controls and accessibility surface late.

**Recommendation:** Once the slice exists, cut regular internal playtest builds (the release skill already makes artefacts) and gather feedback on the loop.

**Verification:** git tag lists only v0.1.0. game-design-gaps line 55 lists a playtesting and QA process as an unplanned gap. The facts hold, and the lack of builds in players' hands is a known deferral.

### GD-ROADMAP-SCOPE-23: Audio absent until the last phase; audio settings ship before any sound

- **Severity:** info (reviewer said low) · **Verdict:** deliberate · **Category:** ordering-risk · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk11-settings-design.md:71`

There is no src/audio module. Sound arrives only in phase 7, the last phase planned anywhere, while chunk 11 ships audio sliders labelled 'sound arrives in a later update'. Combat feel and world atmosphere are being judged in silence.

**Evidence:** ls src: app core data game gfx platform (no audio). 0002 §16 phase 7. Chunk 11 lines 68-71, 205.

**Impact:** Feel and fun judgements without sound are skewed, and the audio content plan (music, SFX style) is missing (known gap).

**Recommendation:** For the slice, bring in a minimal SDL3_mixer playback path with a handful of placeholder SFX early (hits, casts, UI). Low technical risk, high feel value.

**Verification:** src has no audio module. game-design-gaps.md line 50 records 'Audio content: phase 7 plays sounds; no one has planned music or sound design', so audio's late placement and the planning gap are known. Whether to bring audio forward is a priority opinion. The claim that settings sliders ship early rests on the chunk 11 spec, which I did not open.
