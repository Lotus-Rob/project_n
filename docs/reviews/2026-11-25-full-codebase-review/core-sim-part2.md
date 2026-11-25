# Review: core-sim (part 2)

### CORE-SIM-11: GlowDrive::Valid requires a pulse's period for every drive kind

- **Severity:** low · **Verdict:** confirmed · **Category:** validation · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/glow.hpp:109`

Valid() requires `periodSeconds >= MIN_PULSE_SECONDS` and `fadeSeconds > 0` whatever `kind` is. Only Pulse reads the period and only Charge reads the fade. The enum fields (kind, curve, fadeCurve, results) aren't range-checked.

**Evidence:** glow.hpp:108-113.

**Impact:** A Steady or Progress glow in effects.yaml that sets `period: 0` (meaningless for it) is refused, rejecting a whole hot reload. Out-of-range enum values would pass if a loader ever produced them.

**Recommendation:** Check each field only for the kinds that read it, and add enum range checks like those in AbilityData::Valid.

**Verification:** glow.hpp Valid() checks periodSeconds >= MIN_PULSE_SECONDS and fadeSeconds > 0 for every kind. The enums are not range-checked. glow.cpp reads periodSeconds only in the pulse path. Whether a Steady glow with period 0 can be written in effects.yaml depends on loader defaults, which I did not check. Impact is low.

### CORE-SIM-12: State's domain access throws despite the no-exceptions rule

- **Severity:** low (reviewer said info) · **Verdict:** confirmed · **Category:** rules · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/state.hpp:72`

`m_domains.at(...)` throws std::out_of_range for a bad DomainIndex. src/AGENTS.md says "Our code throws nothing"; a broken invariant should be PN_ASSERT.

**Evidence:** state.hpp:71-77; abilities.hpp:198-205.

**Impact:** A bad index ends the program with an uncaught exception instead of an assertion with a message, and costs a check in release where an assert would compile out. (The same `.at()` pattern is used widely in core: Abilities::Of and In, ShippedGlowTable.)

**Recommendation:** Use `PN_ASSERT(index < size); return m_domains[index];`, or record that `.at()` is the accepted bounds check.

**Verification:** state.hpp:71-77 uses m_domains.at(), and abilities.hpp:199 and 204 use .at() too. src/AGENTS.md:20 says 'Our code throws nothing' and that a broken invariant is PN_ASSERT. .at() is an unasserted throwing bounds check, so it breaks the stated rule, though the effect is only a crash on an already-broken invariant. I did not look for a recorded exception to the rule.

### CORE-SIM-13: ToBelow and ToBelowWide return 0 for a bound of 0, outside their stated range

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** api · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/random.hpp:54`

Both are documented as giving a value in [0, bound), but a bound of 0 gives 0 rather than an empty range, and nothing asserts the bound is above 0.

**Evidence:** random.hpp:52-57 and 59-73.

**Impact:** Any caller whose bound can reach 0 (an empty weight sum, an empty candidate list, the aura range above) quietly picks element 0 instead of failing an assertion: wrong picks or out-of-range indexing downstream.

**Recommendation:** Document the precondition `bound > 0`. Because these are constexpr, use a constexpr-friendly assertion, or have callers PN_ASSERT before drawing.

**Verification:** random.hpp:54-73 returns 0 for bound 0, as stated. [0,0) is empty, so no valid result exists, and returning 0 is natural for the multiply-shift. This is a missing documented precondition, not a defect. Whether any caller can pass 0 beyond the aura case (finding 3) was not checked.

### CORE-SIM-14: RunBurst would publish a bogus snapshot for a burst of zero ticks

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/logic.cpp:23`

RunBurst publishes unconditionally. TickClock::Advance returns `{}` (ticks 0, scheduledNs 0) when called early. Game::Run calls Step after WaitUntilNs(NextNs()), so this shouldn't happen, but nothing in Logic guards it. Such a publish carries `scheduledNs = 0` with `advanced` left from the last tick (possibly true).

**Evidence:** logic.cpp:23-84 (no ticks check before the publish); tick_clock.cpp:7-10 `if (nowNs < NextNs()) return {};`; app/game.cpp:51-55.

**Impact:** If the clock ever woke early, the main thread would take a snapshot of the same tick scheduled at time 0. Display::Took would record a lateness of `publishedNs - 0` (the whole time since the clock's origin), so DisplayDelay's target would go to its ceiling (TICK_NS) for 10 s. The publish (the most costly part of a burst) would also run for nothing.

**Recommendation:** Return early from RunBurst when `burst.ticks == 0` (publishing nothing, returning false), or assert `burst.ticks > 0`. Add a logic test for a zero-tick burst.

**Verification:** RunBurst does publish for ticks==0, as stated. Game::Run (app/game.cpp:51-55) always waits until NextNs() before Step, so Advance never returns an empty burst there. The scenario is theoretical. Adding an assert is harmless.

### CORE-SIM-15: Event and Snapshot comments describe things that no longer exist

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** documentation · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/frame.hpp:471`

`Event::cosmetic`: "a sound may; a residency hint may not". Residency is now the SceneResident action, not an event, and no event kind is cosmetic (the struct comment says "Neither is cosmetic: none is dropped"). `Snapshot::epoch`: "it changes when a save is loaded, a new game or a replay starts". There is no new-game action; the epoch moves only at a load's or a replay's swap.

**Evidence:** frame.hpp:375-376, 464-472; logic.cpp:349-356 (only TakeRestored moves the epoch).

**Impact:** Misleading contract docs for the main-thread event path.

**Recommendation:** Reword both to what the code does today, or remove `cosmetic` until an event kind uses it.

**Verification:** The Event::cosmetic comment mentions a residency hint, which is stale, since residency is now an action. But `cosmetic` is still read by handover.hpp:96 to drop stale events, so it is not dead. The struct comment says no event is cosmetic today. Only the stale wording is valid. The recommendation to remove the field is wrong. I did not check the Snapshot::epoch claim.

### CORE-SIM-16: AbilityUse doesn't enforce its 8-bit source

- **Severity:** info · **Verdict:** confirmed · **Category:** api · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/frame.hpp:459`

`return (tick << SOURCE_BITS) | source;`. The source is documented as below 256 but not masked or asserted. Today's sources are 1, 2 and 16+slot (at most 23).

**Evidence:** frame.hpp:457-462; game/combat.hpp:31-35; game/host.cpp:46; game/scenes/arena.cpp:29.

**Impact:** A future source of 256 or more would OR into the tick bits, so two uses on different ticks could share an ID and their damage numbers would combine wrongly.

**Recommendation:** static_assert at the source constants, or PN_ASSERT / mask `source & 0xFF` in AbilityUse.

**Verification:** AbilityUse at frame.hpp:459 does `(tick << 8) | source` with no mask or assert. The 'below 256' limit is documented only in the comment. Current sources are small. This is a latent hazard only.

### CORE-SIM-17: ACTION_KINDS depends on WalkTo staying the last enumerator

- **Severity:** info · **Verdict:** confirmed · **Category:** maintainability · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/frame.hpp:511`

`ACTION_KINDS = WalkTo + 1` with the comment "WalkTo is the last". A new ActionKind added after WalkTo without updating this line would be refused by the replay reader (`kind >= ACTION_KINDS`) and miss the ActionSet static_assert.

**Evidence:** frame.hpp:488-511; logic.hpp:218-219; replay.cpp:98.

**Impact:** A new recorded action silently makes every replay containing it unreadable, until someone notices.

**Recommendation:** Add a test or static_assert tied to the last enumerator (for example a `constexpr` array of all kinds whose size is checked), or name the last kind in a constant next to the enum.

**Verification:** ACTION_KINDS is `to_underlying(ActionKind::WalkTo)+1`, with a comment saying WalkTo must stay last. replay.cpp rejects kinds at or above it. The coupling is real and nothing enforces it. I did not look for an existing test.

### CORE-SIM-18: Effects::LookOf gives the flying swords' look to every ability but the fire bolt

- **Severity:** info · **Verdict:** confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/effects.hpp:233`

`return kind == AbilityKind::FireBolt ? fireBolt : flyingSwords;`. A new projectile ability (or None, Dash or PoisonLance passed by mistake) silently draws with the swords' look.

**Evidence:** effects.hpp:232-235.

**Impact:** A new ability added to AbilityKind gets no compile error or log for its missing look.

**Recommendation:** Index looks by AbilityKind (an array per kind, with an explicit 'no projectile' entry) or assert on kinds that have no projectile.

**Verification:** Read effects.hpp:232-235. The ternary falls back to flyingSwords for every kind except FireBolt. Its doc comment states this ('else the swords'), so it is a simple design shortcut with no guard for new kinds. I did not trace callers. Low risk.

### CORE-SIM-19: Presentation-only tables and the main-thread frame code live in core, next to the simulation

- **Severity:** info · **Verdict:** deliberate · **Category:** architecture · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `src/core/seals.hpp:15`

core holds the glow looks (glow.hpp), the seal looks (seals.hpp, which includes the generated `core/generated/atlas.hpp`), the HUD layout (effects.hpp), DisplayDelay, and PresentFrame (a main-thread template in frame.hpp, which also pulls in display_delay.hpp and profile.hpp for every simulation unit that includes the frame contract). game links only core, so it can see and depend on the HUD and seal look types, which src/AGENTS.md says the simulation never reads.

**Evidence:** seals.hpp:15 `#include "core/generated/atlas.hpp"`; frame.hpp:10,16,566-611; src/AGENTS.md (effects, seals and glows are presentation only).

**Impact:** Weaker layering: nothing stops game code from reading presentation data, which would break the presentation-not-hashed rule. Every re-pack of the atlas recompiles anything that includes seals.hpp. The cost grows as presentation grows (a ui module is planned).

**Recommendation:** When the ui/presentation split lands (known plan: "presentation out of app when a ui module lands"), move glow, seals, effects, display_delay and PresentFrame into a presentation module above core that game doesn't link. Until then, keep frame.hpp's simulation contract separate from PresentFrame.

**Verification:** The finding itself marks this known-deferred (presentation out of app when a ui module lands). src/AGENTS.md describes effects, seals and glows as presentation-only, never hashed. Layering is by convention only; the modules lint checks includes by module, not by type. Nothing to act on now.

### CORE-SIM-20: A data reload during a replay's start change leaves the live game at the replay's start state

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/logic.cpp:349`

If a ReloadData arrives in Wait() while a replay's start is held, StopPlaying runs, but at the swap TakeRestored still adopts the replay's counters, calendar phase and the simulation's loaded state, and opens a new epoch. Play then goes on live from the replay's start rather than staying where it was. The test 'a replay stopped while its start is held leaves the window's pausing at the swap' asserts this.

**Evidence:** logic.cpp:166-171, 214-216, 349-358; logic_tests.cpp:1378-1394.

**Impact:** A debug hot reload during an F10 transition jumps the player's game to the replay's start without playing it. That is surprising, though debug-only.

**Recommendation:** Confirm it is intended. If not, drop m_restored and the held load when playback stops before the swap (the simulation would need a way to discard the state it holds), or document it in RunBurst's contract.

**Verification:** Read Wait() at logic.cpp:160-171 and TakeRestored at 349-358. A ReloadData during a replay calls StopPlaying and then ReloadData. TakeRestored still applies counters and calendar and opens a new epoch, and only the paused flag is gated on m_replay->Playing() (the comment 'a replay stopped since leaves the window's pausing'). The behaviour is intentional enough that a test asserts it, and it only happens on a debug hot reload. A comment or the RunBurst contract could state it. Low impact.
