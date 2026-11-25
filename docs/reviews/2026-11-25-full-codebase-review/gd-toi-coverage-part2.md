# Review: gd-toi-coverage (part 2)

### GD-TOI-COVERAGE-11: Relationships are one opinion that only grows: no kinds, no negatives, no grudges, rivals or bonds

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/npcs.hpp:149`

`Relationship` is {holder, subject, opinion, lastMet}. The only writer is `Merge`, which adds +1 per meeting (npcs.cpp:161), and only the partner's view of the initiator changes. Trimming keeps the most recently met. 0002 §7.7 describes the edge table as '(source, target, kind, values)'.

**Evidence:** npcs.hpp:149-164; npcs.cpp:161 `m_merged.back().opinion += 1.0F;`.

**Impact:** Grudges, rivalry, marriage, master and disciple, sworn siblings, sect loyalty and enmity can't be represented. The recency trim would forget a sworn enemy met long ago in favour of a stranger met last month. The chunk 9 'attitude toward you' is already a workaround outside the table.

**Recommendation:** Give edges a kind (bond type) and signed values (favour, plus e.g. hatred and debt). Make the trim priority kind-aware (bonds and grudges never trimmed before acquaintances). Let events other than meetings write commands into it. Reconcile 0002 §7.7.

**Verification:** The relationship is one opinion that grows by +1 per meeting. The game-design-gaps memory lists relationships (favour, rivalry, marriage, masters) as 'on the backlog with nothing behind them yet', so this is a known deferral. I did not check the 0002 section 7.7 'kind' wording. It is a gap, not a bug.

### GD-TOI-COVERAGE-12: The NPC tier machinery is dead in production and can't serve the planned towns

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** dead-code · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/npcs.hpp:248`

`Materialise`, `Fold`, `SpawnGroup` and the `Met` tag have no production caller (only tests/unit/game/npcs_tests.cpp). `Met` is never set, so `MET_RELATIONSHIP_LENGTH` (50) never applies, and `Relationships::Trim` does a domain lookup (`IsMet`) on every over-limit holder that always returns false. `Activate` asserts `person.home == NO_HOME` ('an NPC activated in a scene with the world map'), so it can't activate the world's homed NPCs, which is exactly what chunk 6 relies on ('They become active NPCs for the visit, keeping their stable IDs (the existing tiers)').

**Evidence:** grep: Materialise, Fold, SpawnGroup and <Met> appear only in npcs.cpp and tests. The arena uses `Activate` only for homeless scratch NPCs (arena_layout.cpp:137). npcs.cpp `Activate` has the PN_ASSERT on NO_HOME.

**Impact:** Chunk 6 will need a different activation path (homed NPC → town position) and must decide when `Met` is set. The aggregate tier, meant to be the scaling answer (0002 §7.7), is unused: the world spawns all 50,000 as individuals, bounded by `MOST_WORLD_PEOPLE`. Untested-in-use code drifts.

**Recommendation:** Either delete Group, Materialise, Fold and Met until a design needs them, or have the GD and chunk 6 design state how and when they're used (meeting sets Met; distant regions as aggregates on large procedural maps) and generalise `Activate` to take a placement from the scene.

**Verification:** Materialise, SpawnGroup and Fold appear in npcs.cpp, and the Met tag is only read (npcs.cpp:170, 257, 275, 294) with no emplace outside moves. Activate has an assert at npcs.cpp:255 that the NPC is homeless, which conflicts with homed world NPCs. I did not grep tests or other game files for production callers. The chunk 6 point is plausible but chunk 6 is unwritten, so this is forward-looking and the dead-code point is minor.

### GD-TOI-COVERAGE-13: World time only runs on the world map; no player-driven time skip; the world sim depends on which scene is showing

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1571`

The world clock advances only on world-map ticks that aren't paused (host.cpp Tick: `clockRuns = Running().Traits().worldMap && ...`). A month is 90 s (calendar.hpp:13-16). `AdvanceCalendar` runs travel only `if (Running().Traits().worldMap && m_paths ...)`. The only way to skip ahead is the debug M key. Towns and combat freeze time (chunks 6 and 7).

**Evidence:** calendar.hpp: TICKS_PER_DAY 180, DAYS_PER_MONTH 30; host.cpp:1571.

**Impact:** ToI's pacing rests on time passing through actions: closed-door cultivation, travel, 'skip month', lifespans of decades to centuries. A 300-year run is about 90 hours of world-map wall time with no in-game skip. Any future 'rest at the inn for a month' or 'seclusion in the sect' would advance months while not on the map, and travel would then silently not run because it depends on the scene's presentation trait.

**Recommendation:** Decide time pacing in the GD (action-driven time skips as a recorded gameplay action, the clock's rate). Decouple the world calendar systems from `Traits().worldMap`: run the month's world systems whenever a month passes, and gate only presentation on the scene.

**Verification:** Checked: host.cpp:861 sets clockRuns = Running().Traits().worldMap && !paused && ..., and travel is gated on the worldMap trait (lines 941, 951, 1235). The facts hold. The claimed impact is speculative: no town, rest or seclusion scene exists yet, and time pacing is a game-design question that game-design-gaps.md already defers to the GD brainstorm. A decoupling recommendation is reasonable when those scenes arrive, but it is not a current defect.

### GD-TOI-COVERAGE-14: Host is the only place systems are wired: no system registry, and every new world system grows Host

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1549`

0002 §7.2-7.3 describes systems as functions registered with a rate and offset. `core/schedule.hpp` offers only `RunsOn`/`InSlice` helpers. In practice `Host::Tick` (about 120 lines) and `Host::AdvanceCalendar` call each system by hand (orbits, spins, combat, active NPCs, scene hook, strikes; then travel and month step). host.cpp is 1,888 lines and host.hpp 571, with one-paragraph contracts on Tick (~30 lines) and Load (~20 lines).

**Evidence:** host.cpp:1549-1590 AdvanceCalendar; host.cpp Tick; host.hpp's Tick doc comment.

**Impact:** Every ToI system (ageing, sect politics, economy, events, breakthroughs, quests) will add code, state and save fields to Host and WorldState, and its Save/Load/StateHash contracts must be edited each time. The class is already hard to review (the backlog lists oversized files but not host).

**Recommendation:** Before adding life-sim systems, extract a world-system interface (month systems with Run(state, jobs, counters) and Save/Load/Hash hooks, registered in a fixed order) and a tick-system list. Keep Host as the orchestrator of scenes and saves. Update 0002 §7.2-7.3 to match whichever shape is chosen.

**Verification:** Host::Tick and AdvanceCalendar do call each system by hand, which is consistent with the cited lines (host.cpp has the worldMap-gated calls near 941-1235). Whether design doc 0002 §7.2-7.3 promises a registry is unchecked. A real registry is premature before any life-sim systems exist. Refactoring Host when the first month system is added is a sound, cheap option, so this is a forward-looking architecture opinion more than a finding.

### GD-TOI-COVERAGE-15: The world population is capped at 100,000 individuals, with no aggregate tier in use, while maps may be 2-50x larger

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/world_map.hpp:57`

`MOST_WORLD_PEOPLE = 100'000` bounds what the world spawns, and every person is a full individual (`SpawnPeople`). 0002 §7.7's answer to scale (aggregates for distant regions and populations nobody has met, made into individuals when the player comes near) is not used in the world scene.

**Evidence:** world_map.hpp:57; world_state.cpp SpawnPeople spawns `place.people` NPCs per city; Group, Materialise and Fold unused (finding 8).

**Impact:** On a procedural map 10-50x the test map, either NPC density drops 10-50x (an emptier world) or the cap, the month step, saves and relationships scale linearly past their measured budgets. Ageing and births will add churn on top.

**Recommendation:** In the GD and procedural-map design, decide the density target and use the aggregate tier for regions outside the player's reach (summary numbers per region and sect, made into individuals on approach). Then measure month step and save cost at the target size.

**Verification:** Checked: MOST_WORLD_PEOPLE = 100'000 at world_map.hpp:57, enforced in world_map.cpp:549. Materialise and Fold are called only from tests (npcs_tests.cpp), so the aggregate tier is unused in the world scene, as the finding says. The impact is speculative: the density target on larger procedural maps is exactly what the procedural-map design and GD brainstorm will set, and the cap is a constant that can be raised after measurement. The avoid-wasted-work and unknowns-are-measured-spikes memories already require measuring at target size.

### GD-TOI-COVERAGE-16: NPC names are a pure function of the ID, so families, clans, marriages and Dao names can't share or change a name

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/names.hpp:60`

`NameOf(names, seed, npc)` draws the surname and given name from the stable ID alone, and the name is never stored.

**Evidence:** names.hpp:57-60 'A pure function of its arguments (core::Random), so a name is never stored.'

**Impact:** Chunk 6 households ('families live together'), ToI-style clans, children inheriting a surname, marriage, Dao names on joining a sect, and titles can't be expressed. Every family member gets a random surname.

**Recommendation:** Keep the pure draw as the default, but add an optional stored override (family or surname index, a given-name override) on the NPC's cold data, or key the surname draw on a household or clan ID instead of the NPC ID.

**Verification:** names.hpp:57-60 confirms NameOf is a pure function of (seed, StableId) and names are never stored. Family, clan, marriage and Dao-name needs are real and not yet designed (game-design-gaps lists social systems). It is a deliberate simplicity choice for now, and adding an override later is cheap because NameOf has few callers. The impact is speculative until the GD brainstorm and chunk 6 decide what households and clans are.

### GD-TOI-COVERAGE-17: HitAmount, the stats hook, has no caster or target to read stats from

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/combat.cpp:435`

`HitAmount(i64 base)` and `HitAmount(const AbilityData&, i64 base)` are documented as 'the one place every amount passes through, so stats and levels plug in here later', but take neither the caster nor the target, and are evaluated at cast time.

**Evidence:** combat.hpp HitAmount docs; combat.cpp:262-263, 354-355 call sites at cast; combat.cpp:435-441.

**Impact:** Attack stats need the caster, and defence, resistance and element affinity need the target at hit time. Adding them changes the signature and splits it into cast-time and hit-time parts, so the promise of a single plug-in point doesn't hold as written.

**Recommendation:** When the attributes design lands, split it into `OutgoingAmount(caster, ability, base)` at cast and `IncomingAmount(target, element, amount)` at hit. Update the comment now so it doesn't overpromise.

**Verification:** combat.cpp:435-441 shows HitAmount(base) returns base and the ability overload ignores the ability. Both are called at cast time (lines 262-263, 354-355), and arena.cpp:84 also calls it. Neither overload takes a caster or target, so the 'stats plug in here' docs overpromise as the finding says. The finding's facts hold. It is a placeholder with no behaviour yet, so severity is low.

### GD-TOI-COVERAGE-18: The world map has no difficulty gradient; danger sites have no level or realm

- **Severity:** low · **Verdict:** unverifiable · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `assets/data/world.yaml:309`

A danger place has a sort (mountain_pass, cave, ...) and a tile, nothing about how dangerous it is. Regions are the tiles nearest each city (`RegionTiles`), not zones of increasing difficulty. ToI's map unlocks regions by realm.

**Evidence:** world.yaml places entries `{ key: wolf_fang_pass, kind: danger, sort: mountain_pass, tile: [20, 75], ... }`; world_map.hpp Place has no level field.

**Impact:** Exploration by realm, a sense of progression across the map, and site difficulty for chunk 7 ('danger scales counts') have nowhere to live. The procedural generator would need to add this later.

**Recommendation:** Put difficulty zoning (a per-tile or per-region danger level derived from distance to the start or from generator bands) into the procedural map design from the start, and give sites a level that the combat scenes read.

**Verification:** The facts hold as far as I checked: world.yaml places carry only a sort and a tile, and Place has no level field. The memory note says the shipped map is only a test map and real maps are procedural per seed. Whether the generator gets difficulty zoning is a design decision for the generator and GD, which I could not decide by reading. It is a reasonable recommendation but not a defect.

### GD-TOI-COVERAGE-19: Phase 6 roadmap table is stale: 5b3 and 5b-look still say 'To do'

- **Severity:** low · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-design.md:120`

The chunk table lists 5b3 (fog) and 5b-look as 'To do' and 5c as one row, but 5b3 was implemented on 2026-11-21, 5b-look terrain on 2026-11-21 and travellers on 2026-11-24, and 5c1 on 2026-11-19 (their specs' Status lines).

**Evidence:** phase6-design.md table rows 5b3, 5b-look, 5c; 2026-11-20-phase6-chunk5b3-fog-design.md:3 'implemented 2026-11-21'.

**Impact:** Anyone planning the GD brainstorm or chunk 6 from the roadmap sees the wrong state, including whether chunk 5 is finished, which is the GD's trigger.

**Recommendation:** Update the table (5b3 and 5b-look done, 5c split into 5c1 done and 5c2 to do, GD still to do) in the next docs commit.

**Verification:** phase6-design.md lines 108-110 still say 5b3, 5b-look and 5c are 'To do'. The specs' own status lines say 5b3 and 5b-look were implemented. The GD row at line 111 depends on chunk 5 being done, so the stale rows mislead. Line number is about 108-111 rather than 120.

### GD-TOI-COVERAGE-20: DEFAULT_SHARE_PER_MILLE says it is the shipped share, but the data ships 58

- **Severity:** low · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/world_map.hpp:121`

`/// The shipped share of a city's people on the road, in parts per thousand.` `DEFAULT_SHARE_PER_MILLE = 40`, while world.yaml ships `share_per_mille: 58`.

**Evidence:** world_map.hpp:120-121; world.yaml travel line.

**Impact:** A misleading comment. Code built without data gets a share unlike the shipped one, which the comment hides.

**Recommendation:** Reword it as the default when the data names none, or set it to 58 with a test tying it to the shipped file, as `CombatRules` does for its shipped numbers.

**Verification:** world_map.hpp:120-121 says 'shipped share' and sets 40, while world.yaml:124 has share_per_mille: 58. The comment is misleading. The only test use is travel_tests.cpp:1119, which just compares with the constant, so no test ties it to the shipped file. Reword the comment or add a tie.
