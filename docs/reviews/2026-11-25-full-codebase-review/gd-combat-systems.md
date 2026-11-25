# Review: gd-combat-systems

**Scope:** GAME DESIGN of the combat stack as implemented and specified: abilities, volleys, seals, effects, glow, combat rules, creatures (src/core, src/game/combat.*, src/data, assets/data/{abilities,effects,seals,creatures}.yaml, specs chunk4/4b/chunk7): depth, readability, balance levers, data-drivenness, extensibility to hundreds of skills and build variety, enemy variety and AI, feel (input latency, feedback), difficulty model, what is missing for a combat loop that is fun.

**Findings:** 24 standing (0 critical, 0 high, 8 medium, 14 low, 2 info), 0 raised but refuted on verification.

**Parts:** gd-combat-systems.md, gd-combat-systems-part2.md, gd-combat-systems-part3.md

## Assessment

## Combat systems: game-design review (scope gd-combat-systems)

**Overall: good engineering, but not yet a game design.** What is built is a careful, deterministic, well-tested *showcase* of one ability (1,000 flying swords) with three supporting abilities. The rules underneath it (resource, statuses, enemies, threat, scaling) are placeholders. Some of them work against the fun loop the specs promise. The presentation stack is several times richer than the gameplay stack. Seals have a 1,024-entry, 9-ring data language and glows have four drives. Combat itself is 4 hard-coded `AbilityKind`s, 2 hard-coded statuses, 2 hard-coded creature kinds and enemies that cannot hurt you.

### Strengths
- **The volley model is excellent engineering.** One record per cast, each sword a pure function of (record, index, time, aim), scheduled arrivals, a stateless retarget chain and a commit window. The renderer and the logic share the maths. It is deterministic, cheap in the simulation, saved and loaded exactly, and heavily tested. This is the right base for the planned GPU projectiles.
- **Tuning values are data** (`abilities.yaml` `motion:`/`swords:`/`combat:`, `effects.yaml`), with refusals that name the line, shipped defaults mirrored in `core::shipped`, and hot reload. Game data and presentation are cleanly split (hashed vs not).
- **Amounts are i64 and go through a single `HitAmount` choke point.** Ability uses and `UseEnded` support combined damage numbers. Accessibility hooks are present: recolour by element, "Not enough Qi" in words, seconds on cooldowns, reduce-motion and reduce-flashing levers.
- **Save/load validation of combat state is thorough** (counts are bounded, enums are checked, targets are cross-checked against the domain).

### Weaknesses (design)
1. **No threat and no difficulty model.** The player cannot be damaged, foxes orbit at a fixed radius round the player (movement and dash change nothing tactically), and player death is undefined. Chunk 7 keeps "enemies attacking, AI, death" as a non-goal, yet it promises balance and elites that "feel different". With no enemy verbs those cannot be met. Time-to-kill is the only lever.
2. **The shipped DoT design defeats itself.** Re-application resets the affliction clock. The shipped generator (0.25 s) and spender (0.5 s) both re-apply faster than the 1 s period, so in normal play burn and poison never tick. This was a user decision, but it should be reopened.
3. **The resource is not a constraint.** Qi is gained on cast (even into empty air), and one or two bolts refill the swords' cost. The swords' cooldown is the only real limit.
4. **Balance is lopsided.** The swords deal ~20,000 damage per cast (~3,300 DPS). The bolt does ~560 and the lance ~720. One cast is 16 fox lifetimes, and most of it is overkill.
5. **Not extensible to hundreds of skills.** Abilities, glow roles, effect looks, statuses and creature kinds are closed enums or named fields. Behaviour dispatches on identity. There is no effect pipeline (no on-hit/on-kill triggers, buffs, debuffs, crowd control, resistances, crits, stacks), and damage has no source. `HitAmount` lacks the caster and the target, which stats will need. The data-driven sweep is recorded, but it should be scoped as an *effect system* design, not only an enum-to-table conversion.
6. **Data validation is shape-based, not semantic.** `element: healing` on an attack damages enemies with green numbers. The `burn:`/`poison:` key is discarded in favour of `element`. Per-kind fields (swords on the bolt, a seal or speed on the lance) are accepted and silently ignored.
7. **Feel.** No input buffer and no hold-to-cast: sustained generator fire means 4 clicks a second, a strain issue under accessibility-first. There is no gamepad casting. The showcase special takes ~2 s to its first impact. There is no hit flash, knockback or status visual on afflicted enemies.
8. **Spec and architecture mismatches ahead.** Chunk 7 wants path collision ("a bolt meeting a crate breaks it") and large bosses. The volley has no collision, and hits test a single chest point. The renderer's per-sword target walk is O(targets²), which will hurt with chunk 7's hundreds of trash.

### Risks
- Chunk 7 (dungeons, trash in the hundreds, elites, bosses, props) will hit every one of these limits at once: closed creature kinds, Fox-only `Tag`, point hit tests, no collision, O(T²) retargeting, no threat, global combat rules that hold arena-only content. The game-design brainstorm (scheduled) should settle the combat pillars (threat, enemy verbs, effect/status system, resource loop, build variety) **before** chunk 7's plan, or chunk 7 will harden a showcase into the game's combat.
- Balance work has no tooling (no time-to-kill or DPS tests, no simulator). The spec's pace targets ("an arena under a minute") will be tuned by HP sponges.

### Files and coverage
I read the core combat code, the data parser, the three YAML tables, the arena scene, app casting and the chunk 7 spec in full. I read most of the chunk 4 spec and parts of the glow header, seals.yaml, the seals spec and the combat tests. I did not read `data/seals.cpp`, `data/effects.cpp`, `core/seals.hpp` or `core/glow.cpp` in depth: they are presentation-only, and I judged them out of focus for a gameplay review.

**Files read in full:** assets/data/abilities.yaml, assets/data/effects.yaml, assets/data/creatures.yaml, src/core/combat_rules.hpp, src/core/hit.hpp, src/core/abilities.hpp, src/core/effects.hpp, src/core/volley.hpp, src/core/volley.cpp, src/core/seal.hpp, src/core/logic.hpp, src/core/game_data.hpp, src/game/combat.hpp, src/game/combat.cpp, src/game/scenes/arena.cpp, src/game/scenes/arena.hpp, src/data/abilities.cpp, src/data/abilities.hpp, src/app/cast.hpp, src/app/cast.cpp, docs/superpowers/specs/2026-11-10-phase6-chunk7-combat-scenes-design.md, docs/superpowers/specs/2026-11-10-phase6-chunk4-abilities-design.md (sections 1-460, 671-720, 845-865)

## Findings

### GD-COMBAT-SYSTEMS-01: Chunk 7's balance anti-goals cannot be met while enemies have no verbs

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** spec-flaw · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk7-combat-scenes-design.md:82`

Chunk 7 lists 'Enemies attacking, combat AI, player death and defeat' as non-goals (line 82). It also lists as anti-goals 'Badly balanced: fights that end before they start, or drag; elites and bosses that feel no different' (line 93) and 'Boring, a drudge' (line 94), and decides that 'Elites and bosses differ in data only (size, hit points, art, label, aura, orbs)'. With no enemy attacks, telegraphs, movement threats or player health at stake, an elite or boss differs from trash only in time-to-kill.

**Evidence:** Spec lines 82, 93-94, 133-134, 191. In code, nothing calls Combat::Damage on the player except the arena aura, which heals; chunk 4 Outcome 'Player death is undefined (ruling 12)'.

**Impact:** Chunk 7 will be built and tuned as HP-sponge clearing: the exact 'drudge' and 'feel no different' failure the user named. The pace targets (an arena in under a minute) become a pure DPS/HP ratio.

**Recommendation:** In the scheduled game-design brainstorm, decide on minimal enemy verbs (contact damage, one telegraphed attack per tier, a boss pattern) and a player failure state before chunk 7 is planned. Otherwise, amend chunk 7's anti-goals to match its non-goals.

**Verification:** The chunk7 spec lists 'Enemies attacking, combat AI, player death and defeat' as a non-goal, and lists 'elites and bosses that feel no different' and 'boring, a drudge' as anti-goals (lines 82-94). So the tension is real. The non-goal is the user's explicit scoping and is deferred, not overlooked. Whether elites and bosses can feel different with data-only differences is a fair design risk, not a defect. Judged as a valid risk to raise in the game-design brainstorm.

### GD-COMBAT-SYSTEMS-02: Abilities are a closed enum with per-identity code dispatch: no path to hundreds of skills or build variety

- **Severity:** medium (reviewer said high) · **Verdict:** deliberate · **Category:** architecture/extensibility · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/core/abilities.hpp:85`

AbilityKind {None, FireBolt, PoisonLance, Dash, FlyingSwords} with ABILITY_NAMES mirrors the YAML keys. Combat::Start dispatches on identity (`kind == Dash`, `FireBolt || FlyingSwords`, `PoisonLance`). Launch special-cases FireBolt by overwriting 14 shape fields. Effects::LookOf picks by `kind == FireBolt`. GlowRole has FlyingSwords and FireBolt roles. Afflictions has fixed burn and poison fields. The YAML can only tune the 4 shipped abilities, and there is no notion of ability behaviour archetypes (projectile volley, instant area, movement, aura, zone, buff).

**Evidence:** combat.cpp:244-252 (Start), 318-338 (FireBolt overrides); effects.hpp:475-477; glow.hpp GlowRole; data/abilities.cpp:470-473 refuses any name not in ABILITY_NAMES.

**Impact:** This blocks the genre's core (techniques, many skills, build variety, mods). Specials 2-4 and the ultimate are empty, so the loadout's 8 slots hold 4 abilities with no choice.

**Recommendation:** Design this as an effect system, not only enum-to-table: archetype by data (delivery: volley, instant area, dash, zone; payload: a list of effects such as damage, status, knockback, heal, Qi), so code branches on properties and never on identity. This fits the recorded data-driven sweep. Raise it before chunk 7.

**Verification:** AbilityKind is a closed enum with ABILITY_NAMES, and Combat::Start dispatches on identity (combat.cpp ~244-252), as stated. sweep-up-backlog.md 'Data-driven sweep' records this almost verbatim: AbilityKind is named as the main obstacle to mods, with 24 uses, LookOf picking by FireBolt, and the aim of tables where code branches on properties, not identity. It is raised 'when abilities or elements next grow (chunk 7's combat)'. Known and deferred, so the finding adds nothing new.

### GD-COMBAT-SYSTEMS-03: Qi is gained on cast, not on hit, so the resource loop is never a real constraint

- **Severity:** medium · **Verdict:** confirmed · **Category:** game-design/balance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:230`

Cast() credits qiGain unconditionally when the cast goes off, even when the bolt targets empty ground (no enemy within 120 of the cursor). At 4 bolts a second that is 40 Qi/s against a 100 maximum. The swords' 60 Qi is refilled by one or two bolts, well inside their 6 s cooldown.

**Evidence:** `caster->qi += std::min(ability.qiGain, room) - ability.qiCost;` runs before Start(). abilities.yaml: qi max 100, start 50; fire_bolt qi_gain 10, cooldown 0.25; flying_swords qi_cost 60, cooldown 6.

**Impact:** The generator/spender economy (a ToI- and ARPG-style decision loop) carries no decisions. Spamming the generator into the air is optimal, and Qi tuning has no effect on play.

**Recommendation:** Grant generator Qi on hit (per target, capped), or make spender and special costs bind against the cooldowns. Add a small simulation test of the shipped loadout's Qi over 30 s of optimal play.

**Verification:** The code at combat.cpp ~229-232 credits qiGain on cast before Start() and does not check for a target, so spamming the generator into empty air fills Qi. The cited numbers (qi_gain 10, cooldown 0.25) were not re-read from the YAML. I found no recorded decision that says gain on cast is intended. It is a valid balance and loop concern.

### GD-COMBAT-SYSTEMS-04: HitAmount and Damage carry no caster, target or source: stats, defence and attribution cannot plug in

- **Severity:** medium · **Verdict:** confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:435`

HitAmount(const AbilityData&, i64 base) is promised as 'the one place every amount passes through, so stats and levels plug in here later', but it receives neither the caster nor the target. It is evaluated at cast time (amounts 'as cast'), so the target's defence or resistance at impact cannot apply. Combat::Damage(state, target, amount, tick) takes no source, so kill credit, aggro, lifesteal, on-kill triggers and 'who killed whom' for NPC simulation are impossible without changing every call site.

**Evidence:** combat.cpp:262-263 and 354-355 compute HitAmount at cast; combat.hpp:205 Damage signature; arena.cpp:84 the aura calls the one-argument HitAmount, bypassing the ability variant.

**Impact:** The 'fast combat, slow progression' plan (stats, realms) needs attacker and defender stats at impact. Today's choke point would have to be redesigned, and saved volleys store pre-resolved amounts.

**Recommendation:** Split amount resolution into a cast-time attacker part and an impact-time defender part (with the source StableId and the target). Pass the source through Damage and the Hit events.

**Verification:** combat.cpp:435-441: HitAmount(const AbilityData&, i64) ignores the ability and returns base. Launch computes damage at cast (lines 262-263, 354-355). The finding is accurate that no caster or target reaches the amount. The seam is a documented placeholder, so this is a forward-looking design risk and not a bug. I did not re-check the Damage signature or arena.cpp:84 line by line.

### GD-COMBAT-SYSTEMS-05: Hits test a single chest point: no creature hit radius, so large enemies (chunk 7 bosses) are hit unreliably

- **Severity:** medium · **Verdict:** confirmed · **Category:** game-design/architecture · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:72`

Tag() includes an enemy only when its chest point (foot raised by half its sprite height) lies within the ability's radius of the cast point. CreatureData has no hit radius or body size. A boss whose body overlaps the area but whose chest is outside is untouched. Swords aim within aimScatter (a fixed 18 units, 'while foxes are the only enemies') of the chest, so they all converge on a boss's middle whatever its size.

**Evidence:** combat.cpp:79-81 `const f64 away = SquaredDistance(point, chest); if (away <= reach)`; core/game_data.hpp CreatureData has no size field; chunk 7 decision 8, 'Bosses are big'.

**Impact:** Area abilities feel like they miss big targets, and visuals do not match outcomes (readability). It also makes the 'bosses feel different' goal harder.

**Recommendation:** Add a per-kind hit radius (data) and test circle-circle overlap. Scale aimScatter by the target's radius.

**Verification:** combat.cpp:72-85: Tag() tests only the squared distance from the chest to the cast point against radius. It also filters on CreatureKind::Fox. CreatureData has no size or hit radius, and aimScatter is a fixed per-shape value (volley.cpp:446). Chunk 7's spec says tiers differ by size, with bosses big, so it will need this. It is a limitation for chunk 7, not a current defect.

### GD-COMBAT-SYSTEMS-06: Chunk 7 requires path collision that the scheduled-hit volley model cannot express

- **Severity:** medium · **Verdict:** confirmed · **Category:** spec-flaw · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk7-combat-scenes-design.md:131`

Chunk 7 says 'Anything an ability's path touches is still hit (a bolt meeting a crate breaks it).' The volley design is a pure function of time and the aim, with arrival ticks scheduled at cast and 'no collision' (chunk 4: 'Both are scheduled hits (no collision)'). The bolt passes through enemies on its way to the tagged one.

**Evidence:** chunk 4 spec 'Elements over time': 'Both are scheduled hits (no collision)'; volley.hpp has no path query; combat.cpp Arrive only resolves the scheduled target.

**Impact:** Either chunk 7 silently drops the rule or it adds per-projectile, per-tick sweep tests against creatures and props for up to 2,000 swords. That breaks the 'logic is near free' property and needs a spatial index. Skill-shot or blocking gameplay (core to 'bullet-heavy' ToI-style combat) is likewise impossible today.

**Recommendation:** Decide in the game-design brainstorm whether projectiles collide. If they do, design a sampled path-versus-spatial-grid test (projectiles flagged colliding or decorative) and measure it. If not, amend chunk 7.

**Verification:** Chunk 7 spec line 131 says 'Anything an ability's path touches is still hit (a bolt meeting a crate breaks it)'. Chunk 4 specifies scheduled hits with no collision, and combat.cpp resolves only the scheduled target. The two specs conflict. The cost estimate for a sweep test is reasonable but unmeasured.

### GD-COMBAT-SYSTEMS-07: No input buffer and no hold-to-cast: the generator needs 4 clicks a second and early presses are lost

- **Severity:** medium · **Verdict:** partially-confirmed · **Category:** feel/accessibility · **Status:** contradicts-recorded-decision · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:219`

A cast arriving before readyTick is dropped silently (`tick < caster->readyTick.at(slot)` returns, with no event). app/cast.hpp makes every press exactly one Cast, never repeated while held. Sustained generator fire at the shipped 0.25 s cooldown means clicking 4 times a second with frame-accurate timing, and a press a few ms early does nothing.

**Evidence:** cast.hpp: 'Every press is its own Cast action, never merged, repeated while held'; chunk 4 decision 10 'One cast per click'.

**Impact:** This is a repetitive-strain burden (accessibility is the top priority by the user's rule) and makes the game feel unresponsive. Combos planned later will suffer the same way.

**Recommendation:** Add a short cast buffer in the simulation (a press within N ticks before ready fires at ready, recorded as the same action). Add a hold-to-repeat option as an accessibility preference. It does not change difficulty because the cooldown is the limit.

**Verification:** combat.cpp:219 does drop an early press silently. Chunk 4 line 34 and decision 10 make 'one cast per click, a held button doesn't repeat' deliberate. That decision says nothing about a buffer for an early press. The buffer and hold-to-repeat are therefore new proposals, not a reversal of a recorded decision. The accessibility argument is sound. I did not find the idea recorded in the backlog or the game-design gaps file.

### GD-COMBAT-SYSTEMS-08: Casting has no gamepad bindings, and keyboard casts need the mouse cursor

- **Severity:** medium · **Verdict:** deliberate · **Category:** accessibility · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/app/bindings.cpp:78`

Primary, Secondary, Movement and Special1-4/Ultimate are bound only to the mouse buttons, Space and keys 1-5. Combat cannot be played on a gamepad. Keyboard casts aim at the mouse cursor (audit 4).

**Evidence:** bindings.cpp:78-85; chunk 4 spec Accessibility: 'casts have no gamepad bindings yet'; phase 6 non-goal 'full gamepad play'.

**Impact:** Combat cannot be played at all without a mouse, against the user's 'every option by mouse, keyboard and gamepad' rule.

**Recommendation:** Design aim-by-stick or soft-lock targeting together with chunk 7's combat input (the backlog item for audit 4).

**Verification:** Chunk 4 spec lines 176-177 state that casts have no gamepad bindings yet, because gamepad play is a phase non-goal. Line 485 defers gamepad aiming. bindings.cpp:78-84 confirms the mouse and 1-4 keys only. It conflicts with the user's every-option-on-gamepad rule, but the deferral is recorded.

### GD-COMBAT-SYSTEMS-09: Re-applying an affliction resets its clock, so the shipped generator and spender DoTs never tick in normal play

- **Severity:** low (reviewer said high) · **Verdict:** deliberate · **Category:** game-design/balance · **Status:** contradicts-recorded-decision · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:123`

Afflict() overwrites the state with nextTick = tick + period on every hit: `state = { .damage = ..., .ticksLeft = affliction.ticks, .use = use, .nextTick = tick + period };`. The shipped fire bolt (cooldown 0.25 s) and poison lance (0.5 s) both re-apply faster than affliction_period (1 s). For a 1,000-sword volley with an affliction, each sword's hit would reset it too. The tests pin this behaviour ('a second bolt refreshes the burn... the first use ends' with only the hit).

**Evidence:** abilities.yaml: fire_bolt cooldown 0.25 with burn {ticks 3, damage 40}; poison_lance cooldown 0.5 with poison {ticks 4, damage 60}; combat.affliction_period 1. The chunk 4 Outcome 'Notes for later chunks' says: 'the bolt held on one fox keeps its burn from ever ticking'.

**Impact:** The burn and poison only deal damage once the player stops using the ability that applies them, which punishes the obvious play pattern. Element identity, the main differentiator between the generator and the spender, is invisible in sustained combat. Any future DoT build (ToI-like poison or fire cultivation) inherits a mechanic that rewards not attacking.

**Recommendation:** Reopen the decision. On refresh, keep the cadence (leave nextTick alone when ticksLeft > 0 and only reset ticksLeft and damage). Or make re-application stack intensity or duration with a cap. Or give each application its own tick schedule. Then pin the chosen rule with a test of a sustained stream at the shipped cooldown.

**Verification:** Afflict (combat.cpp:123) does reset nextTick = tick + period as described. But the chunk4 spec records it as a user decision: 'An affliction's clock (2026-11-12): A new hit refreshes an affliction's clock (the user, 2026-11-12), so a target hit faster than once a second doesn't tick until the hits stop. This supersedes ruling 74; Afflict and its tests stay as they were.' Line 123 also says refresh with no stacks. The finding is accurate, but it asks to reopen a settled ruling. Worth a design note only if the game-design brainstorm revisits DoTs.

### GD-COMBAT-SYSTEMS-10: No status/effect model: two hard-coded DoTs, no buffs, debuffs, crowd control, resistances, crits or triggers

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** game-design/depth · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/combat.hpp:163`

Afflictions holds exactly `burn` and `poison`. AfflictionOf maps every non-Poison element to the burn slot. There is no slow, stun, root, vulnerability, shield or armour, no stacking, no on-hit/on-kill/on-crit hooks, and no resistances or elemental interactions. HitKind mixes an element (Burn, Poison) with an outcome (Damage, Healing).

**Evidence:** combat.cpp:100-102 `return element == core::HitKind::Poison ? afflictions.poison : afflictions.burn;`; hit.hpp HitKind has 4 values.

**Impact:** Combat depth (element interactions, defence, control) has nowhere to plug in. Each new status means new component fields, new save layout and new event kinds.

**Recommendation:** Plan a generic status list per creature (id from data, stacks, duration, period, modifiers) with data-defined interactions, and separate the element from the hit outcome. This is part of the effect-system design above.

**Verification:** Afflictions has only burn and poison, and AfflictionOf (combat.cpp:100-102) maps every non-Poison element to burn, as stated. Chunk4 has no slow, stun or buff. The backlog's data-driven sweep covers hard-coded elements (HitKind) but not a generic status system. The missing combat depth is a real design gap, though still future scope rather than a bug. game-design-gaps.md was only partly checked.
