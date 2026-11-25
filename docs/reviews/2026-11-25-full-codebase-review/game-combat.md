# Review: game-combat

**Scope:** src/game/combat.{hpp,cpp}, components.{hpp,cpp}, random_purposes.hpp. Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 17 standing (0 critical, 0 high, 1 medium, 12 low, 4 info), 0 raised but refuted on verification.

**Parts:** game-combat.md, game-combat-part2.md

## Assessment

## game-combat (src/game/combat.{hpp,cpp}, components.{hpp,cpp}, random_purposes.hpp)

**Strengths**
- The code is careful and well documented. Every public contract (ordering inside `Strike`, when a use ends, what a save holds) is written down, and the code mostly does what the docs say.
- Arithmetic is defensive. `Damage` clamps without i64 overflow for any amount. `FlightTicks` and `CommitTicksFor` saturate. Easing runs in f64. `OrbitPoint` reduces the angle with `fmod` in f64 so long runs stay precise. The transcendental functions go through `core::math`.
- Determinism within one binary looks sound. Tagging is sorted by StableId. Every random draw is keyed on the seed, the state counter, the caster and the slot. Volleys are saved as their cast record and the arrival order is rebuilt on load, so a save holds no derived state.
- Save/Load validation is thorough: counts are bounded by the bytes left, enums and finiteness are checked, and the host's `CombatConsistent`, `AimsAtCreatures`, `FlashesStartBy` and `SealsStartBy` check how components and records depend on each other. I could not find a crafted save that reaches a missing-component `get<>`.
- No view is changed while it is being iterated: structural changes are collected first and applied after the loop.
- The tests are wide and specific: about 60 cases on cooldowns, Qi, dash easing, sword retargeting, burn and poison refresh, the UseEnded "exactly once" promise, scene changes, save round-trips, seals and data reloads.

**Weaknesses**
- **UseEnded promise has a gap.** `EndUses` doesn't see lance strikes cast this tick (`m_strikes`) or zero-count volleys. On the portal tick such a use never ends. Worse, `m_strikes` stays non-empty until `Build`, so the replay hash taken right after that tick fires the `PN_ASSERT` in `Combat::Save` (debug and releasedbg builds, while recording).
- **The element is only a label, not a behaviour.** An ability with `element: healing` damages enemies while its events say Healing. The `burn:`/`poison:` key in abilities.yaml is ignored: the element alone decides which slot the affliction takes and what its ticks are. A `seal:` on a bolt is accepted but never shows, because its last departure is the cast tick. Data validation looks at YAML blocks (`swords`, `speed`), while the runtime dispatches on `AbilityKind`, so the two can disagree silently.
- **Small inconsistencies in "hits on the death tick".** Swords and a creature's second affliction still emit Hit events after the creature is dying (no damage is applied, so numbers overstate the damage), but an affliction on a creature killed earlier in the tick is skipped. Data with `cooldown: 0` lets two casts in one tick share a use ID and a volley seed.
- **Architecture will not scale to the target genre (already known and deferred).** The closed `AbilityKind` if-chain, foxes hard-coded as the only enemy and the only kind that respawns, two fixed affliction slots that replace rather than stack, and `HitAmount` as a stub all mean every new skill is C++ work. That is fine for a demo arena, but a Tale-of-Immortal-like skill and technique system needs data-driven effects (targeting, delivery, on-hit effects, statuses), factions, and enemies that hit back. `Combat` is also a grab-bag (volleys, lance strikes, afflictions, flashes, seals, respawns) whose correctness depends on its call order inside `Host::Tick`.
- **Tests miss some edge cases:** a lance or zero-count volley on the portal tick, healing-element abilities, `Damage` at i64 extremes (the overflow-safety comment is unverified), `DissolveAt` with `dyingTicks` 1, a creature carrying both burn and poison, and a double cast with no cooldown. One test name claims to cover the lance's burst but only tests the aura, and another asserts `>= 1` where an exact count is known.

**Overall health:** good for its current scope. There are no crashes or desyncs in normal shipped-data play. The concrete bug worth fixing now is the lance/portal path. The data-model mismatches (element vs. sign of the amount, burn/poison keys, seals) are latent traps for tuning and modding and should be closed before content grows.

**Risks:** the UseEnded contract is relied on by damage numbers (they wait for it) and any future consumer (stats, lifesteal, XP) will inherit the over-reporting of overkill Hit events. Purpose numbers for random draws are coordinated only by comments across modules, and the game and gfx share seed 0x5eed, so a careless new purpose could correlate draws silently.

**Files read in full:** src/game/combat.hpp, src/game/combat.cpp, src/game/components.hpp, src/game/components.cpp, src/game/random_purposes.hpp, tests/unit/game/combat_tests.cpp, src/core/volley.hpp, src/core/abilities.hpp, src/core/combat_rules.hpp, src/core/hit.hpp

## Findings

### GAME-COMBAT-01: Lance cast on the portal tick: no UseEnded, and a leftover strike trips PN_ASSERT in the replay hash

- **Severity:** medium · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:676`

`Combat::Lance` (combat.cpp:255) queues the strike in `m_strikes`, which only `Strike()` drains. `Host::Tick` runs `TakeActions` (the cast) and then, when the foot crosses the portal, calls `Leave()` and returns before `m_combat.Strike` (host.cpp:870-875). `Leave()` calls `Combat::EndUses` (host.cpp:1118). `EndUses` collects only pending volleys and afflictions, never `m_strikes`, so the lance's use gets no UseEnded. A zero-count volley cast that tick is missed the same way: `Pending()` is false, and only `Strike` ends it through the `startTick == tick` branch. `m_strikes` then stays non-empty until `EnterScene`/`Build` resets `m_combat`. Meanwhile `Logic::RunBurst` calls `CheckReplay()` right after the tick that started the change (logic.cpp:34). While recording, at `wall % 60 == 0`, that computes `Hash()` → `Host::StateHash` → `WriteScene` → `Combat::Save`, which starts with `PN_ASSERT(m_strikes.empty(), ...)` (combat.hpp:283).

**Evidence:** combat.hpp:283 `PN_ASSERT(m_strikes.empty(), "a lance's strike outlived its tick's Strike()")`; combat.cpp:676-699 EndUses loops m_flights (Pending only) and Afflictions; host.cpp:870-875 `if (OnPortal()) { ... Leave(next, true, events); return; }` before Strike at host.cpp:916. host_tests.cpp:1248 tests only a bolt cast on the stepping tick.

**Impact:** Debug/releasedbg builds abort when a player right-clicks the lance on the tick they step onto the arena portal while a replay records and the wall tick is a multiple of 60. In every build, the core::EventKind::UseEnded promise ('every use emits exactly one') is broken for that use.

**Recommendation:** In `EndUses`, also emit UseEnded for each `m_strikes` use (and for volleys with `startTick == tick` that are not pending), then clear `m_strikes`. Alternatively make `Leave` drop the strikes. Add a host test: a lance (and a zero-count volley) cast on the step onto the portal emits exactly one UseEnded, and `StateHash()` is callable after the tick.

**Verification:** Checked: Combat::Lance (combat.cpp:255) only pushes to m_strikes, and Strike (combat.cpp:607-610) is the only place that drains it. Host::Tick returns right after Leave() when OnPortal() (host.cpp:870-873), so m_combat.Strike at host.cpp:916 never runs on that tick. EndUses (combat.cpp:676-699) handles only pending flights and afflictions. m_combat is reset only in Build (host.cpp:803), which runs after the tick. Logic::RunBurst calls CheckReplay() right after RunTick (logic.cpp:34), before the rebuild, and CheckReplay hashes at wall%60==0 while recording. That reaches Combat::Save's PN_ASSERT(m_strikes.empty()) (combat.hpp:283). The scenario is rare but reachable, and the missing UseEnded for the lance is certain. No test covers it. I did not verify the zero-count volley sub-claim.

### GAME-COMBAT-02: An ability with element `healing` damages its targets while its events say Healing

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:580`

The element (`AbilityData::hit`) is only copied into events. Every combat path calls `Damage(state, target, +amount, tick)`: `Arrive` (580), `DealStrike` (292) and `TickAfflictions` (165). The crystal's aura negates for Healing (arena.cpp:90 `hit.hit == core::HitKind::Healing ? -hit.value : hit.value`). abilities.yaml accepts `element: healing` for any ability (data/abilities.cpp:338-346, `AbilityData::Valid` only checks `hit < HIT_KINDS`).

**Evidence:** combat.cpp:570-580: event `.hit = flight.element` then `Damage(state, target.entity, flight.damage, tick)`; arena.cpp:90 negates for Healing.

**Impact:** A tuner or modder who makes a healing ability gets green healing numbers over foxes whose health actually drops. Nothing refuses the data. The same mismatch will bite any future support or heal skill.

**Recommendation:** Either refuse `element: healing` for abilities in data::abilities until healing abilities exist, or route every combat hit through one function that maps (element, amount) to a signed health change, as the aura does. Add a test.

**Verification:** Checked: abilities.cpp:11 and 343 accept element 'healing' for any ability, and the abilities.yaml header lists it. Combat paths pass the amount positively to Damage, and only the aura negates for Healing. No shipped ability uses healing (the grep of abilities.yaml and tests found none), so there is no live bug yet. It is a latent data/behaviour mismatch, so I rate it low rather than medium.

### GAME-COMBAT-03: The `burn:`/`poison:` key is ignored; the element alone decides the affliction slot and its tick kind

- **Severity:** low · **Verdict:** confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:100`

`AfflictionOf` picks the poison slot for `HitKind::Poison` and the burn slot for anything else. `TickAfflictions` then emits each slot's ticks as Poison or Burn hits. The YAML key that held the affliction (`burn` or `poison`, data/abilities.cpp:371-382) is parsed and dropped. So `element: poison` with `burn: {...}` poisons, `element: damage` with `poison: {...}` burns, and `element: healing` with an affliction deals Burn damage over time.

**Evidence:** combat.cpp:100 `return element == core::HitKind::Poison ? afflictions.poison : afflictions.burn;`; data/abilities.cpp:376 `if (const YAML::Node over = burn ? burn : poison)` stores only ticks/damage.

**Impact:** The data says one thing and the game does another, and nothing reports it. An ability's direct hit and its ticks can show different elements.

**Recommendation:** Store the affliction's element in `core::Affliction` from the key, or refuse a key that disagrees with the ability's element (and refuse afflictions on Damage/Healing elements).

**Verification:** Checked: AfflictionOf (combat.cpp:100) picks the slot from the element alone. The shipped data is consistent (fire_bolt is burn with a burn: block, poison_lance is poison with a poison: block), so only crafted data hits it. It is a validation gap, not a live bug.

### GAME-COMBAT-04: A seal on the fire bolt is accepted by the data but can never show

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:368`

`Launch` sets the seal's `endTick = LastDepartureTick(volley)`. The bolt's shape has stagger, out, hold and depart jitter forced to 0, so its last departure is the cast tick. `Strike` erases seals with `tick >= seal.endTick` in the same tick, before any publish. data/abilities.cpp:387-390 accepts a seal on any ability with a `speed` ('Only a volley raises its seal: one anywhere else would never show, so it is refused'), and the abilities.yaml header says 'seal (an ability that flies swords or a bolt)'. Data validation also keys on YAML blocks (`swords`, `speed`) while the runtime dispatches on `AbilityKind`. The lance or dash with a `speed:` and a seal is accepted too, and `Lance`/`StartDash` ignore it.

**Evidence:** combat.cpp:318-336 zeroes outTicks/holdTicks/departJitterTicks/staggerTicks for FireBolt; combat.cpp:633 `std::erase_if(m_seals, [tick](const core::Seal& seal) { return tick >= seal.endTick; })`.

**Impact:** Data that the validation promises is meaningful does nothing. Tuners get no error.

**Recommendation:** Either give the bolt's seal a lifetime (until arrival, say) or refuse seals on abilities whose kind can't raise one. Validate by `AbilityKind`, not by YAML block presence.

**Verification:** Checked: data/abilities.cpp:387-390 accepts a seal whenever node['swords'] or speed>0 holds, so the validation keys on YAML blocks as stated. The finding rests on the seal's endTick equalling the bolt's last departure tick (the cast tick). I did not read Launch (combat.cpp:368) or the Strike erase at line 633. The abilities.yaml header (lines 18-19) says a seal runs 'until its last sword leaves its ring', which does not fit a bolt. The shipped fire_bolt has no seal, so there is no live bug. Validating on AbilityKind is a reasonable fix.

### GAME-COMBAT-05: Hits on a creature that is already dying: emitted inconsistently, and their values overstate damage dealt

- **Severity:** low · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/combat.cpp:141`

`TickAfflictions` skips a creature that is Dying when the loop reaches it (killed earlier this tick by the aura or the burst). But within one creature, if the burn tick kills it, the poison tick right after still emits a Hit event, and `Damage` then does nothing. Swords arriving on the death tick likewise emit Hit events with full value against a dying target (by design: 'a target dying on that tick still takes the hit'). So which hits on a dead creature show depends on the source and on element order. Any consumer summing Hit values (damage numbers today; stats, lifesteal or XP later) over-counts overkill.

**Evidence:** combat.cpp:141 entity-level `if (entities.all_of<Dying>(entity)) continue;` but the element loop at 144-166 does not re-check; Arrive at 564-580 emits and then calls Damage, which ignores dying targets.

**Impact:** Damage-number totals don't match the health lost. The rule for which death-tick hits show is not uniform.

**Recommendation:** Settle on one rule (e.g. hits resolving after the death in the same tick still show, but carry the dealt amount or an overkill flag) and apply it to afflictions too. Re-check Dying inside the element loop or document the order.

**Verification:** Checked: combat.cpp:141 re-checks Dying only per entity. The element loop (144-166) does not, so a poison tick still emits a Hit after the burn tick kills the creature. The Hit event carries the full value even though Damage ignores a dying target. Today the only consumer is the damage numbers, so the impact is cosmetic. The arriving-on-death-tick case is a documented design choice, but the afflictions case follows no stated rule.

### GAME-COMBAT-06: DissolveAt contradicts its doc when dyingTicks is 1

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:443`

The doc says the value goes from 0 at the death's tick to 1 at 'its last tick drawn, the one before its removal', and the comment says 'the last of them fully dissolved, so nothing is left to vanish at its removal'. With dyingTicks = 1 (allowed: `CombatRules::Valid` requires >= 1), the only tick drawn is the death tick, and `DissolveAt` returns 0 there (`tick <= dying.startTick`). The creature is drawn whole and then vanishes.

**Evidence:** combat.cpp:446-450; combat.hpp:189-192; combat_rules.hpp:91 `dyingTicks >= 1`.

**Impact:** A popping death with `dying:` set to one tick. The doc misleads.

**Recommendation:** Return 1 when dyingTicks <= 1, or fix the doc and the inline comment. Add a direct unit test of DissolveAt at dyingTicks 1, 2 and the shipped value.

**Verification:** Checked: DissolveAt (combat.cpp:443-451) returns 0 at tick<=startTick. Removal happens at tick>=start+dyingTicks (combat.cpp:511), so with dyingTicks=1 only the death tick is drawn, at 0. The creature is drawn whole and then vanishes, and the header doc (combat.hpp:189-192) contradicts that. The inline comment 'A dissolve of one tick is drawn in one step' shows the author considered the case, so the code is arguably deliberate and the doc is what needs fixing. The shipped dying value is not 1, so there is no live impact.

### GAME-COMBAT-07: Combat::Load checks loaded volleys less strictly than flashes and seals

- **Severity:** low · **Verdict:** confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/combat.cpp:822`

Load refuses only `AbilityKind::None` and out-of-range kinds, so a volley of `Dash` or `PoisonLance` loads. Nothing checks a volley's `startTick` against the save's tick, although flashes and seals are checked (`FlashesStartBy`, `SealsStartBy`). A target with a nonzero `diedTick` later than the save tick also passes: `AimsAtCreatures` skips it as dead, yet `Struck()` treats it as hittable until then, and `Arrive` would hit the entity if it exists.

**Evidence:** combat.cpp:822-835; combat.hpp:306-318.

**Impact:** Only crafted or corrupt saves reach these states. The renderer may get a volley of an ability it has no look for. Behaviour stays bounded, but it doesn't match the stated rule that 'a load refuses what can't be used'.

**Recommendation:** Refuse volleys whose ability isn't FireBolt or FlyingSwords, and volleys or target death ticks later than the save's tick (a `VolleysStartBy` beside `FlashesStartBy`).

**Verification:** Checked: the Load validation (combat.cpp:822-835) rejects only None and out-of-range kinds. A Dash or PoisonLance volley passes, and there is no startTick or diedTick check against the save tick, unlike flashes and seals. Only crafted or corrupt saves reach this. There is no compatibility concern before 1.0, so the stricter check is cheap.

### GAME-COMBAT-08: Random purposes are coordinated across modules only by comments, and the game and gfx share seed 0x5eed

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/random_purposes.hpp:36`

The header promises 'one value per kind of use, so no two uses share numbers', but it lists only the game's purposes. gfx/tile_hash.hpp uses purpose 9 with `TERRAIN_SEED = 0x5eed`, the same value as host.cpp's world `SEED`, which is why 9 is 'retired' here. core/names.cpp and core/journey.cpp use ASCII-tag purposes, and core/volley.cpp reuses 1-8 under the volley's own seed. Nothing checks for collisions at compile time. Purpose 8 is unused and not explained.

**Evidence:** random_purposes.hpp:36-37 comment; gfx/tile_hash.hpp:14-16; host.cpp:48 `constexpr u64 SEED = 0x5eed;`.

**Impact:** A new game purpose of 9, or a gfx purpose that matches a game one under stateCounter 0, would silently correlate presentation noise with simulation draws (e.g. starting power or home tile).

**Recommendation:** Keep one registry of purposes in core (an enum, or constants with a static_assert that the values are distinct), give gfx a seed distinct from the game's, and note why 8 is free.

**Verification:** Facts check out: random_purposes.hpp:36-37 retires 9 for gfx/tile_hash.hpp (TERRAIN_SEED 0x5eed, purpose 9). core/volley.cpp, names.cpp and journey.cpp use their own purposes, mostly under their own seeds (volley.seed, stay.seed), so those are not collisions. Only the 0x5eed vs host SEED overlap is a real latent risk, and the retirement comment is the deliberate guard. The claim that nothing checks is true. 8 unexplained is trivial.

### GAME-COMBAT-09: Enum Valid() checks rely on the last enumerator

- **Severity:** low · **Verdict:** confirmed · **Category:** maintainability · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/components.hpp:63`

`Look::Valid` checks `layer <= core::Layer::Effects` and `Creature::Valid` checks `kind <= CreatureKind::Fox`. There is no `LAYERS` or `CREATURE_KINDS` count, unlike `PIPELINES`/`HIT_KINDS`, which this code uses elsewhere.

**Evidence:** components.hpp:63 and :109.

**Impact:** Adding a layer after Effects, or a creature kind after Fox, silently makes saves holding it refused until someone remembers these checks.

**Recommendation:** Add count constants next to the enums, with a static_assert, and compare against them.

**Verification:** components.hpp:63 uses `layer <= core::Layer::Effects` and :109 uses `kind <= CreatureKind::Fox`, with no count constants, while PIPELINES is used on the adjacent line. The maintainability point holds, and the impact is minor.

### GAME-COMBAT-10: Combat's ability model is a closed if-chain with hard-coded enemies and two affliction slots

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** architecture · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:244`

`Combat::Start` dispatches on `AbilityKind` (Dash, FireBolt/FlyingSwords, PoisonLance). The bolt is the swords' shape with fields zeroed in code (318-337). `Tag` and the respawn queue hard-code `CreatureKind::Fox` as 'enemy'. `Afflictions` has exactly one burn and one poison, replacing rather than stacking. `HitAmount` is a stub. Creatures never hurt the player through Combat. Each new skill, status or faction needs C++ changes in several places, and correctness depends on the call order inside `Host::Tick` (Cast → Dash → orbits → Combat::Tick → scene Tick → Strike).

**Evidence:** combat.cpp:244-253, 76, 513; combat.hpp:163-179, 183-187.

**Impact:** This does not scale to a Tale-of-Immortal-like skill and technique roster (many skills, elements, statuses, sects, NPCs fighting each other). The more content arrives first, the larger the rewrite.

**Recommendation:** At the game-design brainstorm, design a data-driven effect model: delivery (strike, volley, dash), targeting by faction, a list of on-hit effects, and statuses with stacking rules. Then make Combat run it rather than branch on kinds. Split volleys, statuses and respawns into separate systems with explicit ordering.

**Verification:** A design opinion about a future roster. I did not re-check combat.cpp lines 244-337, and I did not find where it is recorded as deferred. Whether a data-driven effect model is warranted depends on the game-design brainstorm. 'Medium' severity is overstated for something that is not a defect today.
