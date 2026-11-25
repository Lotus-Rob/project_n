# Review: gd-combat-systems (part 3)

### GD-COMBAT-SYSTEMS-21: A zero cooldown (allowed by data) lets two same-slot casts in one tick share a use ID and seed

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/abilities.cpp:348`

cooldown is RequiredTicks with a least of 0, so readyTick = tick + 0 lets a second Cast action in the same tick (two clicks inside one 16.7 ms tick) go off. CastUse(tick, slot) and the volley seed Random(seed, counters.state, caster, PURPOSE_VOLLEY, slot) are then identical. Two flights share one use, Holds and EndReleased conflate them, and the swords overlap exactly.

**Evidence:** data/abilities.cpp:348 `RequiredTicks("abilities", node, name, "cooldown", 0)`; combat.cpp:231 and 340-348.

**Impact:** A minor edge case with tuned data, but it breaks the use-identity invariant that combined damage numbers rely on.

**Recommendation:** Require cooldown ≥ 1 tick, or add a per-tick cast counter to the use ID.

**Verification:** The core holds. abilities.cpp:348 calls RequiredTicks with a least of 0, so cooldown 0 loads. The gate at combat.cpp:219 is `tick < readyTick`, and readyTick is set to tick+cooldown, so with cooldown 0 a second cast in the same tick passes. I did not verify that the use ID and volley seed are identical in that case, or that a same-tick duplicate Cast action can occur. No shipped or test data uses cooldown 0. It is a theoretical edge case, so low severity. The cited line is combat.cpp:219 and abilities.cpp:348, not combat.hpp:34.

### GD-COMBAT-SYSTEMS-22: No balance or time-to-kill tests or tooling for the shipped loadout

- **Severity:** low · **Verdict:** unverifiable · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/game/combat_tests.cpp:1`

The combat tests check mechanics thoroughly but nothing checks outcomes at the game-design level: DPS per ability, overkill share, Qi sustainability, or time to clear N foxes with the shipped data. Chunk 7 makes pace targets ('an arena under a minute') a goal with no automated measure.

**Evidence:** combat_tests.cpp TEST_CASE list (mechanics only); the backlog's progression-balance tool is unplanned.

**Impact:** Balance regressions from data edits go unnoticed, and degenerate patterns such as the refresh-negated DoT or Qi-from-air are discovered only by play.

**Recommendation:** Add a headless 'balance probe' (a scripted optimal rotation in the arena for N seconds, reporting damage by ability, overkill and Qi) as a measure or report command, not a gate.

**Verification:** I did not read the combat tests, so I cannot confirm that no balance or time-to-kill coverage exists. The backlog does list the progression-balance tool as unplanned. The proposal is a feature suggestion, not a defect.

### GD-COMBAT-SYSTEMS-23: The showcase special takes ~2 s from press to first impact, against 'fast combat'

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** feel · **Status:** contradicts-recorded-decision · **Reviewer confidence:** low
- **Where:** `assets/data/abilities.yaml:37`

First arrival = out (0.12 s) + hold (0.3 s) + depart jitter (up to 0.25 s) + flight (1.7 s) ≈ 2.1 s; the last ≈ 3.7 s. Targets are tagged at the cast, so enemies entering the area afterwards are ignored, and tagged ones are chased anywhere at whatever speed the fixed flight time demands.

**Evidence:** abilities.yaml flying_swords swords block; volley.hpp: the flight is fixed ticks, the end follows the aim wherever it goes.

**Impact:** This is fine for a showcase. As a combat tool against moving or hostile enemies, the windup is long and the homing is unconditional, so there is no counterplay and no aiming skill.

**Recommendation:** When enemies gain verbs, revisit: an early first wave, shorter hold, or a range cap beyond which swords plunge.

**Verification:** I did not recompute the 2.1 s and 3.7 s timings. The finding itself calls the long windup fine for a showcase, and its recommendation is to revisit once enemies gain verbs. That is a forward-looking note, not a defect. The 'contradicts-recorded-decision' status is not supported by anything I checked.

### GD-COMBAT-SYSTEMS-24: Stale docs: the affliction cadence is 'once a second', and chunk 4 notes name effect keys that no longer exist

- **Severity:** info · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/abilities.hpp:105`

Affliction::ticks says 'How many times it deals its damage, once a second', and abilities.yaml's header says '(damage over time, once a second)'. The cadence is actually combat.affliction_period. The chunk 4 Outcome's 'Notes for later chunks' tells chunk 11 to scale `halo_intensity` and `core_opacity` after reloads; both keys were removed when glows replaced the halos and cores (effects.yaml has neither).

**Evidence:** abilities.hpp:105; abilities.yaml:5; chunk 4 spec lines 170 and 686; effects.yaml has only sword_scale, trail* and glows.

**Impact:** Chunk 11's reduce-flashing work would follow dead instructions, and data authors misread the cadence.

**Recommendation:** Say 'once every affliction_period'. Add a superseded note in the chunk 4 spec pointing at the glow spec's levers.

**Verification:** abilities.hpp:105 says 'once a second' and the abilities.yaml header line 5 says the same. The glow spec (phase6-glow-design.md:208-209) removed halo_intensity and core_opacity from effects.yaml. The chunk4 spec still names them at lines 170, 379 and 686, so those notes are stale. A grep of src and assets found no live use of either key. I did not check whether affliction_period is the actual cadence.
