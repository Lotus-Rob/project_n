# Review: gd-toi-coverage

**Scope:** GAME DESIGN: compare what exists (src/game, src/core, assets/data) and what is planned (docs/superpowers/specs, docs/design) against the systems that define a Tale-of-Immortal-like game: cultivation realms and breakthroughs, attributes and destinies/traits, spirit roots/elements, manuals/skills/martial and spiritual arts, artefacts and items, alchemy/crafting, sects and sect politics, NPC life simulation (ageing, relationships, grudges, marriage, death, rivals advancing), world events and calamities, towns and facilities, quests, economy, exploration and map regions by realm, combat (bullet-heaven style skills, movement arts, ultimate), death/reincarnation or inheritance, modding. For each: present / planned (where) / absent, and the architectural cost of adding it to the code as it stands. Also judge where the project should deliberately differ.

**Findings:** 23 standing (0 critical, 0 high, 9 medium, 12 low, 2 info), 0 raised but refuted on verification.

**Parts:** gd-toi-coverage.md, gd-toi-coverage-part2.md, gd-toi-coverage-part3.md

## Assessment

## Scope: game design coverage against a Tale-of-Immortal-like game

**The short version:** project_n has an engine and a world-map demo. It does not have a game yet. The code is careful and deterministic, and it is built to scale for what it does: 50,000 NPCs travelling between cities, roads, fog, a seeded world saved as a delta, and 1,000-sword volleys. But almost every system that makes a ToI-like game is absent. The planned chunks (6 towns, 7 combat scenes, 9 view models) cover a little of it, and they predate the procedural-map decision. Not having a game design yet is already recorded (game-design-gaps.md: the GD brainstorm runs after chunk 5). This review adds the **concrete architectural reasons** why some systems will cost more than the gap list suggests. Several current structures would have to be reworked before the cultivation core can exist.

### Systems map (present / planned / absent, and what adding it costs in today's code)
| System | State | Where | Cost of adding it as the code stands |
|---|---|---|---|
| Cultivation realms, breakthroughs | Absent (placeholder `Npc::power` f32, +1 per month spent cultivating) | npcs.cpp:99; chunk 9 plans a tier name from power | **High.** The player has no persistent character to hold a realm (finding 1). NPC power inflates without bound (finding 4). Travel bands and visitor kind are keyed on raw power. |
| Attributes, destinies, traits, talents | Absent | none | **High.** Same blocker (no persistent player). `HitAmount` has no caster or target to read stats from. |
| Spirit roots, elements | Absent. `HitKind` mixes the effect (damage, healing) with the element (poison, burn) | core/hit.hpp, abilities.hpp | **Medium–High.** Closed enums (known: data-driven sweep). `Afflictions` has two fixed slots. Element handling has data bugs (finding 10). |
| Manuals, skills, martial and spiritual arts | 4 hard-coded abilities and 8 slots. The loadout is in hashed game data | abilities.hpp; chunk 7 plans preset loadouts | **High.** The loadout and Qi are not player state (finding 2). `AbilityKind` is a closed enum (known). No learning or proficiency. |
| Artefacts, items, inventory, equipment | Absent | chunk 7 orbs are counted only | **Medium.** Needs new component types and world records with delta fields, plus actions. Nothing blocks it, but there is no owner to hang an inventory on until the player is persistent. |
| Alchemy, forging, talismans, formations as a craft | Absent (seals are visual only) | chunk 8 mentions a cauldron UI | **Medium.** Needs items first. |
| Economy (spirit stones, shops, prices) | Absent | chunk 6 places shops with no gameplay | **Medium.** Cities have no stock or treasury. Travel already has trader/cultivator visitor kinds that could carry trade. |
| Sects and sect politics | Absent (a city keyed `sect` is just a city) | chunk 6: sects as world records, fixed membership | **Medium–High.** No faction concept anywhere: combat targets only `CreatureKind::Fox`. The NPC month step has no group-level system. |
| NPC life simulation | Partial: needs (rest, company), 3 actions, travel, relationships that only grow | npcs.*, travel.* | **High.** No age, lifespan, death or birth. Population turnover undermines the delta-save design (finding 5). Two separate deciders (finding 6). The relationship model can't express grudges, marriage or rivals (finding 7). The tier machinery is unused (finding 8). |
| World events, calamities, beast tides, secret realms | Absent | chunk 7 spawns sites | **Medium.** `core::Event` is only combat presentation events. No scheduled world-system slot outside `Host::AdvanceCalendar` (finding 16). |
| Towns and facilities | Absent (cities are tiles and paintings). Entering is pending (5c2) | chunk 6 | **Medium–High.** `Activate` can't activate a homed NPC (finding 8). The spec assumes the hand-made map (finding 12). |
| Quests | Absent | "later" in chunks 6, 7 | **Medium.** Needs actions, world records, UI. |
| Exploration, map regions by realm | Fog and sight are present. No difficulty gradient. Danger sites are inert apart from route cost | fog.*, world.yaml | **Low–Medium.** The procedural generator must zone difficulty (finding 24). |
| Combat (bullet-heaven skills, movement arts, ultimate) | Player-side volleys are strong (homing swords, bolt, lance, dash). The Ultimate slot is empty. No enemy attacks, no collision, no player death | combat.*, volley.* | **High** for ToI-style dodge combat. No free-projectile or collision system (finding 3). |
| Death, reincarnation, inheritance | Absent | none | **High.** Needs a persistent character, ageing, and a decision on how death sits with saves and replays. |
| Modding | Absent (YAML data is a start) | known: data-driven sweep | **High.** Closed enums (known) plus the compiled `SpriteId` set (finding 11). One data folder, no layering. |

### Strengths
- The deterministic machinery is excellent and will carry a living world: counter-based RNG, fixed chunks, merged commands, saves as a delta against the seeded world, and 1-vs-N-worker tests.
- Combat amounts are i64 and every amount passes through one place (`HitAmount`), which is good groundwork for long progression.
- The travel layer (profiles, legs, power bands, visitor kinds) was designed to grow mounts and other ways of moving. The shared-routes design already anticipates trips to points of interest.
- The fog spec designed sight so that realms and items can extend it.

### Weaknesses and risks
1. **The player is a throwaway scene entity.** This is the single biggest structural mismatch with the genre. Everything ToI is about hangs off a persistent cultivator who lives in the same world as the NPCs.
2. Character-defining data (loadout, Qi capacity, creature stats) lives in **hashed, global game data** rather than per-entity state.
3. The world sim is a placeholder (power +1 a month, opinions +1 a meeting), but its numbers already drive travel tuning, and over months of play they drift away from the values that tuning assumed.
4. The specs for chunks 6, 7 and 9 were written before the procedural-map decision and before a game design. If they are built as written, the GD brainstorm would have to rework them afterwards.
5. Work keeps going into world-map performance and visuals (roads, river heads, shared-routes) while the core loop (world map → site → fight → reward → growth) cannot be played. Combat exists only in a dev scene.

### Where to deliberately differ from ToI
- **Make the living world the differentiator.** 50,000 deterministic NPCs who grow, travel and remember beats ToI's thinner NPC layer. Let the player meet NPCs as rivals and allies whose growth is real simulation, not scripted.
- **Combat:** keep the player-side power fantasy (massed homing swords) that ToI lacks. Add enemy patterns sparingly, with accessibility options (reduce flashing, slower bullets), rather than copying ToI's dense bullet hell.
- **Breakthroughs and tribulations:** design for readable odds and skill expression rather than ToI's opaque RNG and save-scumming. Deterministic replays and saves make "reload until it works" trivial, so failure should be meaningful but not run-ending by dice alone.
- **Time pressure:** ToI's lifespan clock is a core tension. Decide early whether project_n keeps it. The 90-second month and the frozen town/combat time make this a pacing decision that affects the calendar, the delta save and NPC ageing.
- **Modding:** ToI's mod scene is a big part of its longevity. Choose runtime-loaded content tables over the compiled enums before content grows.

**Overall:** the engine is healthy. As a game it is at prototype stage. The GD brainstorm should settle, before chunk 6: (a) the player as a persistent world character, (b) where character state lives, (c) the NPC lifecycle and how it interacts with delta saves, (d) a combat model with threats to the player, and (e) whether chunks 6, 7 and 9 still stand once the map is procedural.

**Files read in full:** src/game/npcs.hpp, src/game/npcs.cpp, src/game/world_state.hpp, src/game/world_state.cpp, src/game/components.hpp, src/game/random_purposes.hpp, src/game/scenes.hpp, src/game/scene.hpp, src/game/combat.hpp, src/game/host.hpp, src/core/game_data.hpp, src/core/abilities.hpp, src/core/combat_rules.hpp, src/core/calendar.hpp, src/core/names.hpp, src/core/schedule.hpp, assets/data/creatures.yaml, assets/data/abilities.yaml, assets/data/effects.yaml, assets/data/animations.yaml, assets/data/world.yaml, assets/AGENTS.md, docs/superpowers/specs/2026-11-10-phase6-design.md, docs/superpowers/specs/2026-11-10-phase6-chunk6-towns-design.md, docs/superpowers/specs/2026-11-10-phase6-chunk7-combat-scenes-design.md

## Findings

### GD-TOI-COVERAGE-01: The player's cultivator is a throwaway scene entity, not a persistent character in the world

- **Severity:** medium (reviewer said high) · **Verdict:** confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/world_state.cpp:240`

`WorldContext::SpawnPlayer` creates the cultivator through `Spawn`, which takes a scene-range ID (`state.Create(ACTIVE, core::IdRange::Scene)`). `Host::Build` (host.cpp:798) resets `m_player = {}`. `ClearScene` (host.cpp:411-431) destroys every entity with an ID at or above `SCENE_ID_BASE`, the player included, at each scene change. The player gets health and Qi again from game data on every scene entry (world_state.cpp:244-245). It has no `Npc` component, no home, power, relationships or world-range stable ID. The relationship table (`Relationships::Within`) rejects scene IDs.

**Evidence:** world_state.cpp:225 `state.Create(ACTIVE, core::IdRange::Scene)`; host.cpp:421 `if (std::to_underlying(entity) >= core::SCENE_ID_BASE) { state.Destroy(entity); ...}`; npcs.cpp `isNpcId` requires `< core::SCENE_ID_BASE`. The chunk 9 spec has to invent a separate per-NPC `attitude toward the player` field because the player can't be a relationship subject.

**Impact:** Every ToI system belongs to a persistent character: realm, cultivation progress, techniques learned, inventory, sect rank, age and lifespan, relationships with NPCs, reputation. None has an owner today. Damage taken in a fight can't carry to the map. NPCs can't befriend, hate or marry the player through the same machinery they use with each other. Each later chunk will add player special cases (attitude field, loadout state, orbs counted) unless this is fixed first.

**Recommendation:** Before chunk 6, decide in the GD brainstorm that the player is a world-range character. Options: a world NPC with `Npc`/`Needs` plus player-only components, activated into each scene under its stable ID the way `Activate`/`Deactivate` move NPCs. Or a dedicated world-domain player record that scenes project an avatar from. Save health, Qi, loadout and progression on it. Let relationships name it, so `attitude` becomes an edge, or a dense per-NPC column if the 50,000 edges are a concern.

**Verification:** ClearScene (host.cpp:~411-431) destroys every entity at or above SCENE_ID_BASE and only Deactivates world NPCs, so the player (scene-range) is destroyed. The structural claim holds. Not recorded as deliberate. The game-design-gaps memory says the GD brainstorm runs before chunk 6, so it is a known pre-design gap. Severity is high-ish but the cost is a design decision, not a bug today; medium.

### GD-TOI-COVERAGE-02: Loadout, Qi capacity and creature stats live in hashed global game data, not per-entity state

- **Severity:** medium (reviewer said high) · **Verdict:** confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/abilities.hpp:187`

`core::Abilities` holds `qiMax`, `qiStart`, the ability table and the `loadout` (which ability sits in each slot). It is part of `core::GameData`, which `GameData::Hash()` hashes and replays check. `Caster` (combat.hpp) holds only current Qi and cooldowns. `CreatureData` (health, walk speed, sight) is one record per kind (`GameData::cultivator`, `GameData::fox`), not per creature.

**Evidence:** game_data.hpp:64-71 `CreatureData cultivator{}; CreatureData fox{}; Abilities abilities{};` and `Fields` archives all of them into the hash. `Combat::Cast(..., const core::Abilities& abilities, ...)` reads the loadout from game data. `Host::SetData` re-applies max health from the data to every creature.

**Impact:** Learning a manual, equipping an artefact, raising Qi capacity through a breakthrough, or an NPC or enemy having its own skills would each change hashed game data, which is shared by every entity and refused by replays. A cultivator's growth can't be expressed. Chunk 7 already says 'the chosen loadout becomes saved player state', which the current shape doesn't allow.

**Recommendation:** Split content definitions (game data: what each ability or creature kind is) from character state (saved per entity: known abilities, loadout, max Qi, stats, realm). Give `Caster` (or a new `Loadout`/`Stats` component) the per-entity values, and keep `GameData` for definitions only. Do it together with finding 1.

**Verification:** core::Abilities holds loadout (abilities.hpp:195) and sits in GameData, which is hashed. Specs for chunks 7 and 10 (chunk10 spec lines 69-75 and 152) plan loadout editing saved as player state, so the shape must change. I did not trace Caster. Real, and planned work will force it. Pairs with finding 0.

### GD-TOI-COVERAGE-03: No free-projectile or collision system: every hit is pre-assigned, so enemies can't attack and dodge combat can't exist

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:72`

Volleys `Tag` enemies within a radius at cast time and schedule each sword's arrival tick at a fixed target (`core::SwordTarget`). The lance hits every tagged creature at once. No spatial grid, collision test or projectile that hits whatever it meets exists anywhere in `src/` (grep for collision/spatial finds nothing). The stress scene's '10,000 bullets' are spinning sprites with no gameplay effect. 0002 §2 promised 'collision between circles through a spatial hash grid'.

**Evidence:** combat.cpp:76 `if (entities.get<Creature>(entity).kind != CreatureKind::Fox ...) continue;`. Hits are scheduled by `Flight::arrivals`. The chunk 7 spec's non-goals: 'Enemies attacking, combat AI, player death and defeat'.

**Impact:** ToI's real-time combat (and the 'bullet-heavy' goal) needs enemy patterns the player dodges, projectiles that hit the first thing in their path (the chunk 7 spec itself wants 'a bolt meeting a crate breaks it'), player damage and death. None can be built on the target-assigned volley model. With hundreds of trash enemies in chunk 7, `Tag` also scans every creature linearly per cast.

**Recommendation:** Design the combat model in the GD brainstorm: a deterministic spatial hash in `core` (fixed cell size, stable-ID order), a projectile kind that is evaluated per tick against hurtboxes, factions (player, enemy, neutral) instead of `CreatureKind::Fox` checks, and player health loss and defeat. Keep homing volleys as one kind beside it. Plan it before chunk 7 rather than after.

**Verification:** Tag in combat.cpp:72 scans creatures within a radius at cast time, and I found no collision or spatial-hash code under src. Chunk 7 spec lists enemies attacking and player death as non-goals, which I read only in part. Its goal is a thousand swords into a crowd, which the target-assigned model supports. The finding's claim that this cannot support later combat is a forward-looking opinion. The cost is real but the lack is a scoped choice, so medium rather than high.

### GD-TOI-COVERAGE-04: Every NPC cultivates at the same rate, so 'mortals' become 'cultivators' within ~10-15 months and the travel tuning drifts

- **Severity:** medium · **Verdict:** partially-confirmed · **Category:** game-logic · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/npcs.cpp:99`

`LiveMonth` scores Cultivate as `0.6 * (1 - rest) + jitter` for every NPC, whatever its power, and adds `power += 1.0F` each time. A fresh NPC (rest 0) cultivates in month 1, rests the next month, and so on, roughly +0.5 power a month. The 80% 'mortals' (power 0-5) cross `cultivator_power: 5` after about 10-15 months, and power grows without bound after that.

**Evidence:** npcs.cpp:85-101. world.yaml: `cultivator_power: 5`, bands `{from_power: 5, profile: on_foot}`, `reach_per_power: 20`. travel.cpp `ProfileFor`/`VisitorOf`/`DestinationWeight` all read raw power. The 5b2 spec tuned '2,400 to 3,300 on the roads' and mortal versus strong reach against the starting spread only; it lists 'a power growth model beyond today's month step' as a non-goal but records no drift.

**Impact:** After about 15-25 minutes of world-map time, nearly the whole population walks the non-wary profile (ignoring danger sites), counts as cultivator visitors (shifting city character draws), and reaches farther each month, so journeys lengthen and roads fill. The tuned numbers and the 80/18/2 starting shape stop meaning anything. The long-run determinism test (10,000 months) only checks determinism, not plausibility.

**Recommendation:** Either cap growth for now (only NPCs above the cultivator threshold may cultivate, or growth per month by talent with diminishing returns), or freeze power until the realm design lands. Add a test that the share of cultivators after N months stays within a band of the starting share.

**Verification:** npcs.cpp:97-100: Cultivate adds power += 1.0F each time and has no cap. The 5b2 spec line 192 lists 'a power growth model beyond today's month step' as a non-goal, so the unbounded growth is known and deferred. The drift to cultivator status is plausible, but the 10-15 month figure is unmeasured. A plausibility test is still missing.

### GD-TOI-COVERAGE-05: NPC behaviour is two hard-coded deciders, not the data-driven utility AI the engine doc promises

- **Severity:** medium · **Verdict:** partially-confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/npcs.cpp:23`

What an NPC does in a month is decided twice and independently. `Travel::Month` / `SetOut` picks travellers by a flat random share (`share_per_mille`) regardless of needs. `MonthStep`/`LiveMonth` then scores three actions with constants compiled into npcs.cpp (CULTIVATION_DRIVE 0.6, MONTHLY_FATIGUE 0.1, ...). 0002 §7.7 says 'utility AI over actions defined in YAML data, so new behaviour is data, not code'.

**Evidence:** npcs.cpp:17-25 constants; LiveMonth's fixed `std::array<..., 3>` of scores; travel.cpp:142 `if (ToBelow(draw(VISIT_DRAW_SETS_OUT), SHARE_WHOLE) >= rules.sharePerMille) continue;`; host.cpp:1571-1589 runs Travel then MonthStep.

**Impact:** Each ToI behaviour (missions, gathering, hunting, sect duty, seclusion, courting, revenge, breakthrough attempts) has to be wired into one of two unrelated code paths, and travel can't respond to needs or goals (the user's 2026-11-22 wish that NPCs act like cultivators). `Action` is also a closed enum validated in `Npc::Valid`.

**Recommendation:** In the GD brainstorm, define one monthly decision per NPC (goals and needs scored over an action table loaded from data), where 'travel to X' is one action whose target comes from the goal. Move the constants into data. Update 0002 §7.7 or the code so they agree.

**Verification:** LiveMonth uses compiled constants (the CULTIVATION_DRIVE score and a fixed array of three scores in npcs.cpp). Travel decides who sets out separately by share_per_mille. The 2026-11-22 wish for NPCs with their own purposes is recorded in game-design-gaps. I did not verify the 0002 section 7.7 wording that the finding quotes. The mismatch with the engine doc is plausible and the cost is architectural.

### GD-TOI-COVERAGE-06: Creature kinds are hard-coded to the player and foxes throughout the code

- **Severity:** medium · **Verdict:** confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/components.hpp:91`

`CreatureKind { Cultivator, Fox }` is a closed enum. `GameData` has the fixed fields `cultivator` and `fox`. The creatures loader accepts only the keys 'cultivator' and 'fox' (data/game_data.cpp:115). `SceneContext::SpawnFox` is part of the scene interface. `Host::DataOf` picks fox-or-cultivator. Combat's `Tag` treats only `Fox` as an enemy, and `Combat::Tick` queues respawns only for foxes.

**Evidence:** components.hpp:91-94; game_data.hpp:66-68; data/game_data.cpp:115-126; scene.hpp `virtual core::Entity SpawnFox(...)`; host.hpp `DataOf(CreatureKind kind) { return kind == CreatureKind::Fox ? m_data.fox : m_data.cultivator; }`; combat.cpp:76.

**Impact:** Chunk 7 needs at least 5 themes × 4 tiers of enemies, ToI needs beasts by realm, and mods need new creatures. Each new kind is a code change across core, data, game and the scene interface. The data-driven sweep's list names AbilityKind and HitKind but not CreatureKind or the fixed GameData fields.

**Recommendation:** Add this to the data-driven sweep explicitly. Make creatures a keyed table (`std::vector<CreatureData>` indexed by a stable creature ID from data), store the kind ID in `Creature`, make enemy status a faction property, and replace `SpawnFox` with `SpawnCreature(kindId, ...)`.

**Verification:** Checked: components.hpp:91 has the closed CreatureKind enum, and SpawnFox is a virtual on SceneContext (scene.hpp:127), overridden at world_state.hpp:119. The backlog's Data-driven sweep (sweep-up-backlog line 53) names HitKind, AbilityKind, Effects and other enums and says 'a mod adds a new element, ability or creature', but it does not list CreatureKind or the fixed GameData fields. The gap is real, though the sweep's intent covers it. Medium is right.

### GD-TOI-COVERAGE-07: Ability element data bugs: 'healing' abilities damage enemies, and the burn/poison key is ignored

- **Severity:** medium · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/data/abilities.cpp:371`

The loader accepts `element: healing` for any ability, but `Combat::DealStrike`, `Arrive` and `TickAfflictions` always call `Damage(state, target, +amount)` on tagged enemies (combat.cpp:165, 292, 580). Only the arena's aura flips the sign for Healing (arena.cpp:90). So a healing ability damages foxes while its Hit events show heal numbers. Also, the `burn:` versus `poison:` key only names the error message: the affliction is stored untyped, and `AfflictionOf` picks the slot from the ability's element (combat.cpp:100: anything but Poison goes to the burn slot). So `element: poison` with `burn:` poisons, and `element: damage` or `healing` with `poison:` leaves a Burn that deals Burn hits.

**Evidence:** abilities.cpp:338-383; combat.cpp:100-102 `return element == core::HitKind::Poison ? afflictions.poison : afflictions.burn;`; combat.cpp:144 iterates only Burn and Poison.

**Impact:** Data authors and modders get silently wrong behaviour rather than a refusal. A heal-over-time or support ability (a staple of ToI's spiritual arts) can't be made. The bugs will multiply when elements become data.

**Recommendation:** Test-first: refuse `healing` on abilities that target enemies (or implement ally targeting with a negative amount), and refuse an affliction key that doesn't match the element (or make the key set the affliction kind). Fold it into the data-driven sweep's element design (elements with traits such as 'leaves DoT', 'heals', 'resisted by').

**Verification:** Checked: abilities.cpp:338-383 accepts element 'healing', and nothing refuses it for abilities that target enemies. The burn/poison key only feeds the error text (line 370ff). combat.cpp:101 AfflictionOf picks the slot from the element alone: Poison goes to the poison slot, everything else to burn. The tick path calls Combat::Damage(state, target, affliction.damage) with no sign flip, and combat.cpp has no Healing branch. I did not re-read the arena aura at arena.cpp:90 or the DealStrike lines. The core claim holds: a mismatched key and element gives the wrong slot, and a healing element on an enemy-targeting ability gets no refusal.

### GD-TOI-COVERAGE-08: The sprite set is a compiled enum, so mods and data can't add creatures or items with new art without a rebuild

- **Severity:** medium · **Verdict:** confirmed · **Category:** modding · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/generated/atlas.hpp:12`

`core::atlas::SpriteId` and `SPRITE_NAMES` are generated from art/sprites.toml into a committed header. `core::SpriteNamed` resolves data names only against that compiled list, and `Look::Valid` bounds-checks against `SPRITE_NAMES.size()`. The manifest is loaded at runtime, but its sprite set must equal the compiled one.

**Evidence:** atlas.hpp:12 `enum class SpriteId : u16 { Ground, RockLarge, ... }`; manifest.hpp:27 `SpriteNamed` searches `atlas::SPRITE_NAMES`.

**Impact:** The user made mod support and hot reload a high priority (sweep-up 'Data-driven sweep'), and that note says generated code must never be the only way content reaches the game. Sprites are exactly that today: no mod can add a beast, artefact icon or portrait. A ToI-scale content set (hundreds of items, techniques and beasts) also means recompiling for each art addition.

**Recommendation:** Add SpriteId to the data-driven sweep: runtime sprite IDs assigned by the loaded manifest (with name lookup and a mod-layered manifest), keeping generated constants only for engine sprites code must name (shadow, ground, portal).

**Verification:** The backlog's Data-driven sweep names the closed enums AbilityKind and HitKind, and it says generated code must never be the only way content reaches the game. It does not mention SpriteId. The atlas SpriteId enum is generated and compiled. The sweep note's own mod requirement makes this a real gap. I did not re-open atlas.hpp or manifest.hpp this pass, so the line numbers are unchecked. The backlog also has an open item on sprite sharpness, which touches the atlas but does not cover this.

### GD-TOI-COVERAGE-09: The chunk 6, 7 and 9 specs assume the hand-made test map and predate the game design

- **Severity:** medium · **Verdict:** confirmed · **Category:** plan · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk7-combat-scenes-design.md:97`

Chunk 7: 'Chunk 5's hand-placed danger sites become the world's starting sites', 'fixed landmark dungeons placed on the hand-made map', and two big dungeons pinned by a hash test. Chunk 6: 'Chunk 5's six or so cities each have a size'. Places name themselves through strings.yaml keys (`Place::nameKey`, `place_capital`), which a per-seed generator can't author. The 2026-11-25 decision (memory procedural-world-map.md) makes world.yaml only a test map. Chunk 9's 'cultivation level' and 'attitude' are placeholders for systems the GD will define.

**Evidence:** chunk7 spec lines ~95-101 and 'Calls made here'; chunk6 spec Goal 3; world_map.hpp:108 `std::string nameKey{}`; assets/data/strings.yaml `place_capital:`.

**Impact:** If built as written, chunks 6, 7 and 9 bind content to fixed places and string keys, and the procedural generator and GD would then have to rework them. Generated cities, sects and sites also need generated names (as NPC names are), which no spec plans.

**Recommendation:** After the GD brainstorm, amend the three specs: landmark dungeons and cities come from the generator (with optional pinned seeds for test worlds); add a place, sect and site name generator alongside `core::NameOf`; mark placeholder fields as such.

**Verification:** The chunk 7 spec (2026-11-10) says 'fixed landmark dungeons placed on the hand-made map' (line 34), 'hand-placed danger sites become the world's starting sites' (line 102), and repeats this at lines 179 and 188. It never mentions procedural generation. The memory note procedural-world-map (2026-11-25) makes world.yaml only a test map, so the spec predates that decision. I did not open the chunk 6 or chunk 9 specs. Their claims are plausible but unverified. The generated-names point is a fair extension.

### GD-TOI-COVERAGE-10: No ageing, death or birth. Population turnover will erode the saves-as-delta design

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/npcs.hpp:64`

`Npc` has cell, home, action and power, and no birth month, age or lifespan. Nothing in the game ever destroys or creates a world NPC (Fold and Materialise are unused, see finding 8), so the population stays the same 50,000 forever. The 5c1 save format stores the world as its difference from the seeded base: base NPCs gone (by index), NPCs created (whole), and changed fields.

**Evidence:** npcs.hpp:64-86. docs/design/0002 §7.6 delta description. A month is 90 s of world-map time (calendar.hpp:13-16), a year about 18 min.

**Impact:** ToI's core tension is lifespan: NPCs age, die, are replaced, and rivals rise. Once ageing and death exist, every base NPC eventually dies and is replaced. After a few in-game decades the save holds nearly every NPC whole plus a list of nearly every base index as gone, so the delta's size advantage disappears. Load time also grows, since it rebuilds the base and then deletes most of it. Relationships and journals that name the dead need cleanup paths (`Forget` exists but is only reached through Fold).

**Recommendation:** Treat the NPC lifecycle as a first-class GD topic. Measure a simulated 100-year world for save size under the delta format before committing to it. Consider periodically re-basing (saving a new canonical base when the delta exceeds a ratio), or storing the population as a compact column table rather than a delta. Add birth month and lifespan to the hot NPC record now if the format is changing anyway (no compatibility before 1.0).

**Verification:** Npc has no age or lifespan, and the game-design-gaps memory lists lifespan, ageing and death as unplanned and bound for the GD brainstorm. The erosion of the delta-save claim is speculative: it depends on the save format and on simulation that does not exist yet. It is a valid point to measure at design time, not a defect now.
