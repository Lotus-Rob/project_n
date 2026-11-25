# Review: assets-data

**Scope:** assets/data/*.yaml, assets/manifest.yaml, assets/AGENTS.md, art/sprites.toml: data correctness, consistency with the loaders in src/data and the structs in src/core, dangling references, balance oddities, values that look like placeholders, schema gaps for a ToI-like game.

**Findings:** 12 standing (0 critical, 0 high, 0 medium, 7 low, 5 info), 1 raised but refuted on verification.

**Parts:** assets-data.md, assets-data-part2.md

## Assessment

Scope: assets/data/*.yaml, manifest.yaml, assets/AGENTS.md, art/sprites.toml. I read every file except the 85-row world grid and the seals showcase bodies, which I checked only by script and by their header. I checked cross-references mechanically:
- The 160 sprites in sprites.toml, manifest.yaml and assets/sprites/*.png match exactly: same names, same order, same feet, no orphan PNGs.
- Every sprite named by world_look.yaml, seals.yaml and animations.yaml exists.
- The world grid is a clean 150x85 rectangle using only declared symbols.
- All 26 places sit on distinct tiles, none on deep_water or peaks. Every place name has en and zh strings, and every string has both languages.
- Roads and rivers stay in bounds, no road tile is impassable, and the people sum to exactly 50,000.

Strengths: the data is consistent and well documented, and the header comments in each file double as schema docs. The hashed versus presentation split is explicit. Loaders validate ranges and warn on unknown keys.

Weaknesses: this is a presentation and test-bed dataset, not a game dataset. For a Tale of Immortal-like game the schema is almost empty:
- creatures.yaml is hard-wired to two creatures (cultivator and fox), and the loader hard-codes those two keys.
- There are no NPC attributes, realms or power curve, items, sects or factions, events, and no calendar data. Months, years, seasons and lifespans do not exist in data.
- The only simulation-facing data is world.yaml's travel block, the places, and the names.
- Names are Chinese-only (zh plus pinyin), with no gender or other metadata.
- Several numbers in the comments are cached results of a run and will rot.

Overall health: good for what it is. The main risk is that the data layer cannot yet express the game described in the brief, and the placeholder values (power bands, place draws, ability stats) could be mistaken for balance. Everything below is minor to medium. I found nothing critical.

**Files read in full:** (not reported)

## Findings

### ASSETS-DATA-01: Creature table is hard-wired to exactly 'cultivator' and 'fox'

- **Severity:** low (reviewer said medium) · **Verdict:** deliberate · **Category:** schema-gap · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `assets/data/creatures.yaml:6`

creatures.yaml holds two entries, and data/game_data.cpp calls Creature(root,"cultivator") and Creature(root,"fox") and warns on any other key. core::GameData has fixed cultivator and fox members. The villager and the other sprites (crane, fox_spirit, treant_blighted, wisp_red) have no creature entry.

**Impact:** A ToI-like game needs a data-driven set of creature and NPC archetypes: stats, realm, drops, AI. Adding a third creature means C++ changes plus a loader change, and any extra key in the file is only a warning.

**Recommendation:** Make creatures an open map keyed by id, with GameData holding a table indexed by a stable id. This belongs in the planned data-driven sweep.

**Verification:** Confirmed facts: game_data.cpp:115-120 hard-codes cultivator and fox, and unknown keys only warn. The sweep-up-backlog 'Data-driven sweep' item and the 'Debug tuning UI' item record it as a known deferral. creatures.yaml's header calls it 'the test bed's creatures'. The villager and other sprites have no entry because they are not creatures in this model. Low, not medium, because it is scheduled.

### ASSETS-DATA-02: No cultivation, realm, power, item, sect or event schema; 'power' is only a travel threshold

- **Severity:** low (reviewer said medium) · **Verdict:** deliberate · **Category:** schema-gap · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `assets/data/world.yaml:47`

The only simulation data is the travel block: bands: from_power 0 and 5, cultivator_power: 5, reach_per_power: 20, and per-place draws { cultivators, traders }. There is no table for realms, breakthroughs, lifespan, talents or roots, techniques, items, sects (a city key 'sect' is only a place), factions or relationships. Places have only people and draws. The calendar has no data (see the separate finding on days and months).

**Impact:** Mortal versus cultivator is a single integer threshold, which is a placeholder for the central ToI system. Retrofitting realms onto power bands later will change what the hashed travel data means.

**Recommendation:** State in this file or the spec which fields are provisional. Design the realm table as the first non-test data file, and have the power bands reference realm keys rather than raw numbers.

**Verification:** The facts are right: world.yaml holds only bands/travel data. The backlog's game-design-gaps and 'game design brainstorm after chunk 5, before chunk 6' record realms, sects and items as unplanned for now. Per the sweep-up item the world map is a test map. This is a roadmap observation, not a data defect.

### ASSETS-DATA-03: Numeric results of simulation runs are baked into data comments

- **Severity:** low · **Verdict:** confirmed · **Category:** docs-rot · **Status:** new · **Reviewer confidence:** medium
- **Where:** `assets/data/world.yaml:44`

The comment above `travel:` says 'about 2,400 to 3,300 of the 50,000 are on the roads at once', 'the capital's nearest costs 126, the sect's 162; the capital to the oasis costs a mortal 173, round Jade Cave, and the strong 152', and 'the sect to the isle costs 562'. These are derived path costs. They go stale whenever a road, ford, kind cost, danger_reach or profile changes, and they are hashed-data comments with no test behind them.

**Impact:** A silent contradiction after the next road reroute (`world_map.py --roads` rewrites roads). A reader trusts a stale figure.

**Recommendation:** Move the figures into a test that asserts them, or drop the numbers and keep only the intent.

**Verification:** world.yaml:120-123 does quote derived figures (2,400 to 3,300, 126, 162, 173, 152, 562) in a comment. I found no test asserting them. The comment is plain prose and the figures are accurate only until the next reroute. The risk is real but small.

### ASSETS-DATA-04: Names pool has no gender or age, and drawn names will collide heavily at 50,000 NPCs

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** data-quality · **Status:** new · **Reviewer confidence:** medium
- **Where:** `assets/data/names.yaml:111`

There are 105 surnames and 217 single-character given names, and a given name is one character (30%) or two (70%). That gives about 47k two-character combinations per surname, uniform, while the surname distribution is heavily skewed (王, 李 and 张 have weight 90, 90 and 85 against a total of roughly 2,000). The pool also mixes male-coded, female-coded and unsuitable characters, with no gender field. Examples: 婷, 艳, 娜 and 芳 are female-coded; 雄, 强 and 帅 are male-coded; 蛇, 牛, 羊, 马 and 地 are odd as given names; 德仁义礼智信 and 日 read as virtue words. The random draw is therefore uncorrelated with sex. 欧阳, 司马 and the other compound surnames get weight 1 while the single-character 万 and 向 get 2.

**Impact:** There will be many identical full names among tens of thousands of NPCs, and gendered mismatches such as a male NPC called 婷. Both undermine the 'living NPC' feel. Pinyin collisions (xu for 徐 and 许, li for 李 and 黎, yu for 于 and 余) make English forms ambiguous.

**Recommendation:** Add an optional gender tag per given character and an age or era flavour. Add a uniqueness policy (a nickname, courtesy name, or Dao name for cultivators). Document the collision rate, and add a test that measures duplicate-name share for the shipped tables.

**Verification:** names.yaml header confirms weighted surnames, 1 or 2 uniform given characters and no gender field. The duplicate-name concern is plausible arithmetically, but I did not measure it. Names are derived from the stable ID, so duplicates are not a correctness bug. The gendered-character claims are subjective. Treat as a game-design wish.

### ASSETS-DATA-05: Flying swords' outer ring lies outside its seal

- **Severity:** low · **Verdict:** unverifiable · **Category:** balance-consistency · **Status:** new · **Reviewer confidence:** medium
- **Where:** `assets/data/abilities.yaml:40`

flying_swords uses ring_radius 160, ring_step 38 and waves 10, so the last ring sits at 160+9*38 = 502 world units. The seal's `size` is 420, and the ability's hit `radius` is 260. The comment on the seal says it is raised until the last sword leaves its ring.

**Impact:** The outer three waves form outside the seal that is meant to mark the formation, which looks unintended. radius 260 versus ring extent 502 is also an unexplained mismatch.

**Recommendation:** Either size the seal to the outermost ring (at least about 502), or reduce the rings. State what `radius` means for swords.

**Verification:** The arithmetic holds: abilities.yaml has ring_radius 160, ring_step 38 and waves 10, so the last ring is at 502, against a seal size of 420 and radius 260. The header comment says the seal is raised until the last sword leaves its ring, so it is a lifetime rule, not a containment rule. Whether the size should match the ring extent is a visual or design call. A look-at-the-game check would decide it, so I could not settle it by reading.

### ASSETS-DATA-06: Ability display strings share the flat global namespace with UI strings

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `assets/data/strings.yaml:161`

Ability names are keyed by the ability id (fire_bolt, poison_lance, dash, flying_swords) next to UI keys such as health, qi and cooldown. Place names use a place_ prefix, but abilities and creatures (fox_name, cultivator_name) do not follow one scheme. The string `qi` is also the same word as the `qi:` key of abilities.yaml, a different thing.

**Impact:** Adding data-driven abilities, items or realms produces key collisions or an ad hoc prefix per file. The C++ string table (core/strings.hpp) lists known keys by name, so every new ability needs a code change.

**Recommendation:** Namespace data-derived strings (ability_<id>, creature_<id>, place_<key>) consistently, and derive them from the data tables instead of the C++ list.

**Verification:** strings.yaml does mix keys such as fox_name and cultivator_name with ability ids, and the C++ StringId list names the keys. The backlog's Data-driven sweep and Localisation to industry practice items cover StringId generation, so this is a deferral. The 'qi' key colliding with abilities.yaml's qi key is superficial, because they are different files and namespaces.

### ASSETS-DATA-07: Calendar is absent from data: the date line is month and day only

- **Severity:** low · **Verdict:** confirmed · **Category:** schema-gap · **Status:** new · **Reviewer confidence:** medium
- **Where:** `assets/data/strings.yaml:152`

date_line is 'Month {month}, day {day}', with no year, era, season or lunar naming. The days-per-month value lives only in C++ (game::DAYS_PER_MONTH, core calendar.hpp). Nothing in assets/data defines it.

**Impact:** For a game where lifespan, aging and breakthroughs run over centuries, the format has no year and cannot wrap. The month length cannot be tuned or tested from data.

**Recommendation:** Move the calendar (days per month, months per year, era names) into data and add year to the date line when ageing lands.

**Verification:** DAYS_PER_MONTH = 30 is only in C++ (src/core/calendar.hpp:16), and the date_line string has month and day only. The schema gap is real but is a feature not yet built (ageing, years). Not a defect today. Not checked against game-design-gaps.md.

### ASSETS-DATA-08: Ability numbers are placeholders with odd scale relationships

- **Severity:** info (reviewer said low) · **Verdict:** deliberate · **Category:** balance · **Status:** new · **Reviewer confidence:** low
- **Where:** `assets/data/abilities.yaml:37`

Fire bolt: hit radius 120 against a 112-pixel-wide fox sprite, speed 1500, burn 3 ticks of 40 once a second. Flying swords: 1000 swords at 20 damage each (20,000 total against a fox with 1200 health) at a 6 s cooldown for 60 Qi. Poison lance: 360 plus 4x60. Dash: 260 units in 0.15 s (about 1,730 units/s against walk_speed 4/tick, 240 units/s). Qi: max 100, start 50, with the generator granting 10 per 0.25 s (40 Qi/s) so a spender is always available. Aura: damage 50 to 400, healing 50 to 200, per second.

**Impact:** These are stress-test values (the swords exist to load the renderer), not balance. If they ship as the loadout default they define pacing. Nothing marks which are test-bed values.

**Recommendation:** Mark the file as a performance and test bed, and keep gameplay balance in a separate data set.

**Verification:** abilities.yaml is part of the test bed, and 1000 swords exist as a renderer load. The backlog's Debug tuning UI and Data-driven sweep items treat the numbers as tunable placeholders. The finding is true but already understood. The 40 Qi/s generator figure follows from 10 per 0.25 s as stated.

### ASSETS-DATA-09: World map is a single hand-tuned fixed grid whose data comments claim it is also the test map

- **Severity:** info · **Verdict:** deliberate · **Category:** scaling · **Status:** contradicts-recorded-decision · **Reviewer confidence:** low
- **Where:** `assets/data/world.yaml:1`

world.yaml is 150x85 tiles with 26 hand-placed places and 50,000 people. The comment says it is 'the source from then on' (rerunning world_map.py loses the hand edits), and the stricter doc in assets/AGENTS.md describes 'danger_reach', roads and fords as regenerated by `--roads`. Per the recorded decision the shipped map becomes procedural per seed.

**Impact:** Nothing here exercises a very large map. A 50,000-person load on a 12,750-tile grid says nothing about scaling. The rest of the data (profiles, bands) must stay generator-compatible.

**Recommendation:** Add at least one generated large-map fixture, and keep the hashed data minimal (profiles, kinds) as the world gains seeds.

**Verification:** world.yaml lines 1-4 do say it is hand-tuned and the source from then on. The memory note procedural-world-map.md records world.yaml as only a test map, with maps generated per seed and possibly 2x-50x larger. So the finding's 'contradicts-recorded-decision' status is wrong, because the finding itself follows the recorded decision. The large-map fixture suggestion is a plan item, not a data defect.

### ASSETS-DATA-10: Danger site on the capital-port trunk road and a danger cost of exactly rock

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** data-consistency · **Status:** new · **Reviewer confidence:** low
- **Where:** `assets/data/world.yaml:47`

bone_forest is at [86,42], and road 0 runs [75,42] to [128,42] past [90,39]. With danger_reach 4 the main trunk route of the map goes through a danger site's reach. on_foot_wary adds 40 per tile, which the comment says is 'as dear again as rock'. The place is also only 11 tiles from the capital.

**Impact:** Mortals are forced to leave the shortest road for the main route, which is deliberate ('take the road round a danger site wherever one leads round it'). The 40 equals rock's cost by hand, and nothing keeps them equal if rock changes.

**Recommendation:** Express the wary danger cost relative to a named cost, or add a test pinning the comment's claim.

**Verification:** Road 0 runs (75,42) to (90,39) to (128,42), and bone_forest is at (86,42). That is about 3 tiles from (86,40) on the road, so it is within danger_reach 4. Rock costs 40 (line 18) and the wary danger cost is 40 (line 115), so the equality is real, and the comment at lines 110-111 states it. The detour is intended behaviour as the finding admits, and on_foot_wary has the same cost either way. A sort of comment-versus-value drift is possible, but the data is a test map and the value is a plain tunable. No bug.
