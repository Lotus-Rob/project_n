# Review: game-combat (part 2)

### GAME-COMBAT-11: `Holds` costs a scan of every flight and every afflicted entity per ending use

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/combat.cpp:586`

`EndReleased` calls `Holds` for each unique ending use. Each call walks all `m_flights` and every entity with `Afflictions`. Separately, `TickAfflictions` copies every afflicted entity into a new vector every tick, even when none is due. The comment justifying the copy ('adding to the pools the view is built on') is inaccurate: emplacing `Dying` does not touch the `Afflictions` pool a single-component view iterates.

**Evidence:** combat.cpp:136-139, 586-599, 636-646.

**Impact:** Cheap today. In a mass fight with many afflicted creatures and many uses ending at once, it grows as uses × (flights + afflicted), plus an allocation per tick.

**Recommendation:** Keep a per-use hold count, raised on launch and affliction and lowered on resolve, refresh and death, instead of rescanning. Drop the copy, or fix its comment.

**Verification:** Holds (combat.cpp:586-599) does scan m_flights and all Afflictions entities per unique ending use, as stated. TickAfflictions (128-150) copies the view into a vector every tick. The comment 'adding to the pools the view is built on' is dubious, since Dying is a different pool, though emplacing during iteration can still matter for entt views of other pools. The cost is small and ending uses are rare, so low severity.

### GAME-COMBAT-12: Test name claims the lance's burst but only checks the aura; another lance-rule test asserts only `>= 1`

- **Severity:** low · **Verdict:** confirmed · **Category:** test-quality · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/game/combat_tests.cpp:1646`

"the data's combat rules set the aura's period and the lance's burst" checks only the aura's period and damage. "the data's combat rules set the lance's burst and its poison's period" (1662) checks `ticked >= 1`, although with radius 5,000 every fox (12) is tagged and should tick exactly once at CAST + PERIOD.

**Evidence:** combat_tests.cpp:1646-1660, 1688-1692.

**Impact:** Misleading names, and a weak assertion that would pass if most poisons failed to tick.

**Recommendation:** Rename the first case. Assert `ticked == FOXES` in the second.

**Verification:** combat_tests.cpp:1646-1660 checks only the aura's period and damage, so its name over-claims. The second test (1662-1692) ends with `CHECK(ticked >= 1)` although radius 5000 tags every fox. Whether every fox ticks exactly once at CAST+PERIOD I did not check, so the proposed `== FOXES` is plausible but unproven.

### GAME-COMBAT-13: Missing tests for combat edge cases

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/game/combat_tests.cpp:393`

None of these is tested: (1) a lance or zero-count volley cast on the portal tick (the first finding); (2) an ability with element healing (the second finding); (3) `Combat::Damage` at i64 extremes, where the comment 'Each branch stays within i64 whatever the amount' is unverified; (4) `DissolveAt` directly, including dyingTicks 1; (5) one creature carrying both burn and poison, especially the tick one kills it; (6) two casts of one slot in a tick with cooldown 0; (7) a seal on the bolt; (8) the `burn:`/`poison:` key disagreeing with the element. Several of the defects above would be caught by these.

**Evidence:** grep over tests/ finds no DissolveAt, no i64 extremes for Damage, and no healing-element ability cases.

**Impact:** These defects stay unnoticed and could return after a fix.

**Recommendation:** Add small, explicit cases for each, per tests/AGENTS.md.

**Verification:** Grep confirms DissolveAt has no direct test, and there are no i64-extreme Damage cases and no healing-element ability cases. DissolveAt is only exercised indirectly (a dying fox's dissolve through the snapshot at combat_tests.cpp:280 and the replay and save-exactness tests). Burn and poison are tested separately, the tick-order case is covered at 1391, and a seal on the swords at 1481. So items (3), (4) and (2) are accurate gaps. Items (5), (6), (7) and (8) I did not check individually. Some existing tests also cover neighbouring cases, e.g. a burn ending with its creature's death at 1082, and a lance with no enemy at 1359. The finding's value depends on the defects it refers to. I did not verify those, and the cited line 393 is only loosely placed. This is a coverage gap of low severity.

### GAME-COMBAT-14: `cooldown: 0` lets two casts of one slot in one tick share a use ID and a volley seed

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/combat.cpp:340`

`CastUse(tick, slot)` and the volley seed `Random(seed, counters.state, caster, PURPOSE_VOLLEY, slot)` depend only on the tick and the slot. data/abilities.cpp:348 allows a cooldown of 0 (`RequiredTicks(..., "cooldown", 0)`). Two Cast actions for one slot in one tick both go off, which happens when two clicks land within one 16 ms tick. The result is two flights with the same use and the same seed (identical, overlapping swords), and two lance strikes with one use.

**Evidence:** combat.cpp:231 `caster->readyTick.at(slot) = tick + ability.cooldownTicks;` (no change at 0); combat.hpp:34 CastUse; combat_tests.cpp:971 itself sets cooldownTicks = 0.

**Impact:** Duplicate visuals, and damage numbers merge two casts into one use. The refresh logic in `Afflict` (`state.use != use`) can't tell the casts apart.

**Recommendation:** Require a cooldown of at least 1 tick in the data, or add a per-tick cast ordinal to the use ID and the seed draw.

**Verification:** Checked: combat.cpp:231 shows no cooldown change at 0, and abilities.cpp:348 allows 0. The use ID and seed depend only on tick and slot, so two same-tick casts at cooldown 0 would share both. No shipped ability has cooldown 0 (the lowest is fire_bolt at 0.25 s). The test at combat_tests.cpp:971 sets 0 deliberately. Whether two same-slot casts can arrive in one tick depends on input handling, which I did not check. It is only a latent edge case.

### GAME-COMBAT-15: Lance tags at end-of-tick positions; the swords and the bolt tag at positions before the tick's moves

- **Severity:** info · **Verdict:** unverifiable · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/combat.cpp:314`

`Launch` runs inside `Cast` during `TakeActions`, before the orbits move, so it tags chests at last tick's `current`. `DealStrike` runs in `Strike`, after every move, so it tags chests at this tick's positions. The two area abilities therefore test their radius against positions one tick apart. This is documented, but it may read as aim inconsistency on fast-moving targets.

**Evidence:** host.cpp:860-916 order; combat.cpp:279 vs 314.

**Impact:** Edge-of-radius targets may be hit by one ability and missed by the other for the same click point.

**Recommendation:** Pick one time base for tagging (ideally the positions the player saw) and use it for every area ability.

**Verification:** I did not read host.cpp's tick order or combat.cpp:279/314, so I cannot confirm the one-tick difference in tagging time. The finding itself says this is documented, so it is a design choice rather than a defect.

### GAME-COMBAT-16: HeightOf in combat.cpp duplicates HeadOf in components.cpp

- **Severity:** info · **Verdict:** confirmed · **Category:** duplication · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:43`

`HeightOf` computes `footY * look.scale` from the manifest, which `HeadOf` (components.cpp:28-33) also computes. The test file has a third copy (combat_tests.cpp:699-706).

**Evidence:** combat.cpp:43-50 vs components.cpp:28-33.

**Impact:** If the head/chest rule changes (e.g. a per-sprite anchor), it must be changed in several places.

**Recommendation:** Expose `HeightOf` (or `ChestOf`) from components.hpp next to `HeadOf` and use it everywhere.

**Verification:** combat.cpp:43-47 HeightOf computes footY*scale, and components.cpp:28-33 HeadOf computes the same. This is real minor duplication. HeadOf also reads Body::current, which HeightOf does not.

### GAME-COMBAT-17: combat.hpp uses std::to_underlying without including <utility>

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** agents-rule · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/combat.hpp:35`

`CastUse` calls `std::to_underlying`, but the header includes only <array> and <vector> from the standard library. src/AGENTS.md: 'Headers still include everything they use.'

**Evidence:** combat.hpp:24-25, 35.

**Impact:** It compiles only through transitive includes and can break when another header's includes change.

**Recommendation:** Add `#include <utility>`.

**Verification:** combat.hpp includes <array> and <vector> only, and no <utility>, so the finding is right about the includes. It is only an include-hygiene gap, and I did not re-verify the to_underlying call at line 35.
