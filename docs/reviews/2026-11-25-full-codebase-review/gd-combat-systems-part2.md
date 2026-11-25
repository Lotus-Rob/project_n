# Review: gd-combat-systems (part 2)

### GD-COMBAT-SYSTEMS-11: Flying swords dominate the shipped balance by an order of magnitude; most of a cast is overkill

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** game-design/balance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `assets/data/abilities.yaml:36`

1,000 swords × 20 damage = 20,000 per cast (~3,333 DPS over its 6 s cooldown), against ~560 DPS for the bolt (140 per 0.25 s, burn negated when spammed) and ~720 for the lance. Foxes have 1,200 health, so one cast kills up to 16. With 12 foxes in a ring of radius 300 and a 260-radius tag area, a cast catches ~3-4 foxes. ~60 swords kill each one, and the rest plunge or hit a corpse.

**Evidence:** abilities.yaml lines 29-40, creatures.yaml fox health 1200, arena_layout.cpp FOXES = 12. Chunk 4's 'Overkill swords within the commit window plunge and are wasted' is noted but not balanced.

**Impact:** The other abilities are irrelevant whenever the swords are ready, and the 'every sword is a hit' fantasy is mostly wasted numbers. Chunk 7's pace targets tuned 'with the demo loadout' will be set by one ability.

**Recommendation:** Rebalance against a time-to-kill target per ability. Consider damage that scales with tagged targets (split a pool rather than per sword), or retargeting overkill to untagged enemies in range. Add a data sanity test that computes each ability's DPS and overkill share.

**Verification:** The DPS arithmetic and the 3-4 foxes caught per cast depend on the YAML values and arena geometry, and I did not re-derive them. Overkill waste is acknowledged in chunk4's Outcome. A balance opinion like this is decided by measurement or playtest. No data sanity test for DPS is known to exist.

### GD-COMBAT-SYSTEMS-12: `element: healing` on an attack damages enemies while reading as healing

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** data-validation · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/data/abilities.cpp:338`

The parser accepts any of damage, healing, poison or burn as an ability's element. Every delivery path (sword Arrive, lance DealStrike, afflictions) calls Damage(state, target, +amount), which always subtracts, and tags only enemies (foxes). The Hit event carries HitKind::Healing, so the main thread shows a healing-coloured number over an enemy losing health. An affliction with element healing or damage goes to the burn slot and ticks as Burn hits.

**Evidence:** abilities.yaml header: 'element (damage, healing, poison or burn)'; combat.cpp:580 `Damage(state, target.entity, flight.damage, tick)` whatever flight.element is; AfflictionOf maps non-Poison to burn.

**Impact:** A data author (or modder) writing a healing ability gets one that harms enemies and is mislabelled. Colour and number semantics, an accessibility signal, lie.

**Recommendation:** Refuse `healing` (and an affliction whose element is neither burn nor poison) until healing abilities with a friendly-target rule exist. Or give abilities an explicit target filter and sign.

**Verification:** The parser's ELEMENT_NAMES accepts healing, and the error text at abilities.cpp:~338 says 'damage, healing, poison or burn'. Damage() is applied to targets regardless of element, so an attack with element healing would deal damage under a healing label. The claim that the player-facing label would lie is plausible. It is a validation gap for authored data only, since no shipped ability uses it. The finding's text on tagging only enemies was not fully re-checked.

### GD-COMBAT-SYSTEMS-13: The YAML `burn:` / `poison:` key is discarded; the affliction's kind comes from `element`

- **Severity:** low · **Verdict:** confirmed · **Category:** data-validation · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/data/abilities.cpp:376`

AfflictionAt reads ticks and damage only, and data.affliction does not record which key it came from. At run time the affliction's kind is AfflictionOf(element). `element: poison, burn: {...}` therefore makes a poison, and `element: damage, poison: {...}` makes a burn.

**Evidence:** `if (const YAML::Node over = burn ? burn : poison) { ... data.affliction = *affliction; }`; combat.cpp:100-102.

**Impact:** A silent mismatch between what the data says and what plays. This gets worse once more statuses exist.

**Recommendation:** Refuse a burn or poison block whose key disagrees with the element. Better, store the status id from the key and drop the coupling.

**Verification:** abilities.cpp ~370-380 reads `burn ? burn : poison` for the affliction block but only passes the key name into the error message. data.affliction stores ticks and damage only, and the kind comes from AfflictionOf(element) at run time, so a mismatched key and element silently plays as the element says. Only a hand-authored data mismatch can trigger it. Shipped data is consistent.

### GD-COMBAT-SYSTEMS-14: Kind-specific ability fields are accepted and silently ignored

- **Severity:** low · **Verdict:** confirmed · **Category:** data-validation · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:318`

Validation is shape-based ('has swords or a speed'), not by what the ability does. A `swords:` block on fire_bolt passes and is then overwritten by Launch's FireBolt branch. poison_lance with `speed` (or `swords`) and `seal` passes but Lance() never raises a seal or flies anything. A `seal:` on the bolt is raised with endTick = LastDepartureTick = startTick and erased the same tick, so it is never drawn. flying_swords `count: 0` spends 60 Qi for nothing. `damage` and `radius` on dash are ignored.

**Evidence:** data/abilities.cpp:283-285 and 389-391 check `node["swords"] || speed > 0`; combat.cpp:244-252, 318-338, 368-380.

**Impact:** Tuning edits silently do nothing. This will confuse designers and modders, especially in the planned tuning UI or editors.

**Recommendation:** Validate fields against the ability's behaviour (or archetype once data-driven), warning or refusing on fields the behaviour never reads.

**Verification:** abilities.cpp:283-285 and 389-391 check only 'swords or speed'. combat.cpp Launch's FireBolt branch overwrites count, waves, scatter and the other swords fields (lines 318-338), so a swords block on the bolt is dead data. Impact is limited to authoring clarity.

### GD-COMBAT-SYSTEMS-15: Per-frame sword retargeting is O(targets²) per sword, a scaling risk for chunk 7's hundreds of trash

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** performance/scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/volley.cpp:227`

WalkTargets runs for every sword on every drawn frame (gfx/volley_draw.cpp:97 SwordAim, and :99 SwordTarget again). Each move does a count_if over all targets and then a linear scan for the skip-th living one. The chain can run up to `count` moves, so the worst case per sword is O(T²). SwordAt is also re-evaluated per trail segment, recomputing Plan (a PlaceInWave binary search over float WaveStart) each time.

**Evidence:** volley.cpp WalkTargets: `for (u32 move = 0; move < count; ++move) { ... std::ranges::count_if(targets, ...) ... for (; next < count; ++next)`; volley_draw.cpp:55 SwordAt per trail point.

**Impact:** 1,000 swords × 200 tagged trash dying through the volley could be ~10^7 to 10^8 operations per frame on the main thread. DrawVolleys is already the frame's largest CPU zone with 3-4 foxes.

**Recommendation:** Precompute, per volley per snapshot, the sorted death ticks and a living-index prefix (or each target's successor chain) once, so each sword's walk is O(moves) with O(1) picks. Cache Plan per sword per frame across the trail samples. Add a bench with ~200 targets.

**Verification:** volley.cpp:227-260: the walk does a count_if plus a linear scan per move, as described. But a move happens only when the current link is dead and past the commit window, and the loop breaks at the first living target. The chain length is therefore the number of consecutive deaths, not the target count, so O(T^2) per sword is an upper bound that is rarely reached. The cost at chunk 7 scale is unmeasured, so a spike or benchmark would decide it. I did not verify the draw-side per-trail-segment re-evaluation.

### GD-COMBAT-SYSTEMS-16: The arena has no threat: enemies orbit the player at a fixed radius, so movement and dash carry no tactics

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** game-design/enemy-ai · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `assets/data/creatures.yaml:17`

Foxes orbit `around: player` at radius 300; the host re-centres every orbit on the player each tick. Enemy positions are a function of the player's, so walking or dashing never changes the engagement, and enemies never threaten. Only one enemy kind exists, and the only damage source besides the player is the crystal aura (scene content).

**Evidence:** host.cpp Tick: `if (DataOf(creature.kind).orbitsPlayer) { orbit.centre = player; }`; creatures.yaml fox orbit around: player.

**Impact:** Positioning, dodging, kiting and defence (ToI-style bullet-dodging combat) have no testbed. Abilities are judged only on spectacle.

**Recommendation:** Give the arena at least one hostile verb (contact damage or a telegraphed shot) and a chase or flee behaviour as a combat-depth testbed, ahead of chunk 7's dungeon AI.

**Verification:** arena_layout.cpp:147 and the orbitsPlayer flag in game_data.hpp:42 support the claim that foxes orbit the player. Enemy AI beyond that is chunk 7 scope, and the spec says behaviour differences come with combat AI, so the gap is a recorded deferral. I did not locate an explicit 'arena is a spectacle testbed' statement.

### GD-COMBAT-SYSTEMS-17: Creature kinds are two named GameData fields, and `Tag` hard-codes Fox as the only enemy

- **Severity:** low (reviewer said medium) · **Verdict:** deliberate · **Category:** architecture/extensibility · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:76`

GameData has `cultivator` and `fox` fields, CreatureKind {Cultivator, Fox}, and Tag() skips anything that is not CreatureKind::Fox. There is no faction or hostility, tier, hit radius, resistance or behaviour in the data. creatures.yaml can describe only these two.

**Evidence:** combat.cpp:76 `if (entities.get<Creature>(entity).kind != CreatureKind::Fox ...) continue;`; core/game_data.hpp:66-69; components.hpp CreatureKind.

**Impact:** Chunk 7's five themes × four tiers of rosters need a creature table keyed by id plus a faction check. Every hard-coded Fox site (Tag, the aura, respawns, publishing) must change.

**Recommendation:** Make creatures a keyed table with a faction or hostility field. Tag by faction, never by kind. Fold this into the data-driven sweep.

**Verification:** Facts hold: combat.cpp:76 filters on CreatureKind::Fox. The backlog's 'Data-driven sweep' entry records hard-coded content (creatures included) as a known item and says to raise it when chunk 7's combat grows. Treat as a tracked deferral, not a new finding.

### GD-COMBAT-SYSTEMS-18: Afflicted enemies show no status: DoT state is readable only through tick numbers

- **Severity:** low · **Verdict:** unverifiable · **Category:** readability · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1830`

Afflictions are never published. A burning or poisoned fox looks identical to a healthy one, and its health bar has no status marker. With the refresh rule, a player cannot tell whether a DoT is pending or being suppressed.

**Evidence:** There is no reference to Afflictions in gfx, app or the snapshot (grep found only host.cpp's save-version comment and CombatConsistent).

**Impact:** Weak combat readability. Element effects are invisible, especially with combined damage numbers that wait for the use's end.

**Recommendation:** Publish a status set per creature and draw an icon or tint (shape plus colour, never colour alone) under the health bar.

**Verification:** I did not re-grep for Afflictions in gfx, app or the snapshot, so the claim that they are never published is unchecked. It is plausible, because the finding itself says the only hits are in host.cpp. The readability gap is a design opinion, and playtesting would decide it.

### GD-COMBAT-SYSTEMS-19: Combat rules hold arena scene content and one ability's look-length as global hashed rules

- **Severity:** low · **Verdict:** unverifiable · **Category:** architecture/data-layering · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/combat_rules.hpp:56`

CombatRules (global, hashed) holds the arena crystal's aura (period, reach, damage and healing ranges), the fox respawn and arrive times (arena-only behaviour), and flashTicks, which is 'the poison lance's burst' (per ability). None of these are global combat rules.

**Evidence:** combat_rules.hpp fields auraPeriod..auraHealingMax, respawnTicks, arriveTicks, flashTicks; combat.cpp:297 uses m_rules.flashTicks for every lance strike.

**Impact:** Chunk 7's per-theme hazards and props, and new instant-area abilities, will either crowd this struct or share the lance's flash length. Editing an arena prop changes the global data hash.

**Recommendation:** Move the aura into scene or prop data, respawn and arrive into the scene's spawn rules, and the flash length into the ability's data.

**Verification:** I did not open combat_rules.hpp or check where flashTicks is used. The backlog's sweep-up entry says arena layout constants 'become scene data with chunk 7', which covers part of this (the arena props). Whether the fox respawn and arrive times belong to the same move is not recorded. This is a design opinion, not a bug.

### GD-COMBAT-SYSTEMS-20: Seal lifetime is tied to the volley, and the 'gameplay seals fit the same record' claim is optimistic

- **Severity:** low · **Verdict:** unverifiable · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/seal.hpp:41`

A seal's end is always LastDepartureTick of its volley (combat.cpp:369-377), so data cannot make a lingering formation or a seal on a non-volley ability. The 4b spec says 'Seals that heal, defend or attack later fit the same record without changing it'. The record has no owner or faction, no effect or period, and keys into an unhashed presentation table, so a gameplay seal needs at least an owner, an effect reference into hashed data and a tick schedule.

**Evidence:** seal.hpp Seal fields: key, point, size, startTick, endTick, element, hasElement, showcase; 4b spec line 19.

**Impact:** Formations, a ToI and xianxia staple, are pure decoration, and the record will need reworking when they gain play.

**Recommendation:** Plan gameplay formations as a separate zone component (owner, effect id, period, area) that references a seal look. Keep Seal as presentation. Make the seal's duration a data field.

**Verification:** I did not open seal.hpp or combat.cpp:369-377, and I did not check the 4b spec quote. It is a forward-looking design critique. Gameplay seals are not implemented, so nothing is broken today.
