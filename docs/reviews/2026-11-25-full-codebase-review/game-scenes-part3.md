# Review: game-scenes (part 3)

### GAME-SCENES-21: Aura emits the full heal amount even when the player is already at full health

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** presentation · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/game/scenes/arena.cpp:78`

The aura pushes a Healing hit event with the rolled amount, then applies `context.Damage(target, -value)`, which caps health at its most. The damage number shows '+N' even when nothing was healed.

**Evidence:** arena.cpp:76-91; combat_tests 'heals the player up to its most'.

**Impact:** Misleading feedback; the aura is a demo, but the same pattern will mislead in real healing abilities.

**Recommendation:** Decide whether a heal number shows the amount actually healed (common in the genre) or the amount rolled; if the former, return the applied amount from Damage and emit that.

**Verification:** arena.cpp:76-91 pushes the hit event with the rolled value, then calls context.Damage(target, -value). The event carries the rolled amount whatever the target's health. I did not check that Damage caps at max health, but the finding says it does. Whether the number should show the rolled or the applied amount is a design choice, and I found no record of that decision.
