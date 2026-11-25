# Review: gd-world-sim (part 3)

### GD-WORLD-SIM-21: An NPC due to leave late in the month sits the whole month out before it leaves

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** simulation · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/travel.cpp:163`

`SetOut` sets `person.action = Action::Travel` at the month step, while departure is drawn uniformly over the month. Until its journey ends, the NPC skips `LiveMonth` (no rest, cultivation or socialising) and is removed from the partner pool (npcs.cpp:343), even though it is physically at home for up to 29 more days. About 6% of people a month are affected. A visit spanning a boundary costs a second month.

**Impact:** This is a small distortion now. It becomes visible once months matter (cultivation timing, events), because 'travelling' NPCs are absent from home life for up to two months per short visit.

**Recommendation:** When designing activities, model the month as time slices: the activity takes its days and home life the rest. Or let departures happen at the month step, with arrival spread instead.

**Verification:** travel.cpp:~160-165 confirms the mechanism. person.action = Action::Travel is set at the month step, while the departure tick is drawn uniformly over TICKS_PER_MONTH. The NPC is therefore marked travelling before it leaves. I did not check the 6% figure or npcs.cpp:343. The impact is speculative until activities exist. It is a design caveat, not a bug.

### GD-WORLD-SIM-22: Save checks accept whole journeys and player routes whose steps aren't adjacent or walkable

- **Severity:** low · **Verdict:** confirmed · **Category:** validation · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/travel.cpp:176`

`LegSound` checks the shape, profile, a finite origin, tiles on the map and strictly rising arrive ticks. It does not check that consecutive tiles are adjacent, or that the profile can step between them. `RouteFits` for the player's route relies on it too.

**Impact:** A crafted or corrupted save can make a traveller or the cultivator glide straight across the map. `ExploreRoute` then explores the arbitrary tiles listed in the route. There is no memory-safety problem, but the 'a save file is untrusted' checks are weaker here than elsewhere.

**Recommendation:** In `LegSound`, check `StepCost(tiles[i-1], tiles[i], profile) != IMPASSABLE_COST`. For paced player legs, check adjacency.

**Verification:** travel.cpp:176-185 LegSound checks shape, profile, finite origin, non-empty tiles, arrive size and monotonicity, tile < tiles, and the startTick overflow. It does not check adjacency or StepCost. The claim holds. I found no other guard in JourneySound (lines 76-85). It is a weak spot against corrupt saves, with no memory-safety impact.

### GD-WORLD-SIM-23: Weighted destination draw can saturate and favour low-index cities

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/game/travel.cpp:253`

`ChooseDestination` sums weights with `SaturatingSum`. A weight can reach about 1e11 (pull) x 1000 (reach) x 100 (draw) = 1e16 when a destination is very near (cost 1-10). With many such cities the total saturates at u64 max (1.8e19). Every later city's cumulative weight then equals the total, and the pick, being below the total, always stops at the first saturated city. Cities with higher place indices can never be drawn.

**Impact:** Unreachable on the test map. Possible on dense generated maps with many tightly packed settlements, where it would bias traffic towards low indices with no error.

**Recommendation:** Normalise weights (e.g. scale pull to u32 before multiplying) or use 128-bit accumulation. Assert the total never saturates.

**Verification:** travel.cpp:253-262 uses SaturatingSum as described, and the final return nullopt is unreachable only if the total did not saturate. The 1e16 per-weight figure is generous. Saturation would need about 1,800 cities each at that extreme, which no realistic map reaches. The bias is real in principle but practically unreachable. An assert on the total would be cheap.
