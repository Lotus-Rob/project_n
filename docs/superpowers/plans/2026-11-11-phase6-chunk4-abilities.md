# Phase 6 Chunk 4: Abilities and the Flying Swords Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An ability loadout in the arena (fire bolt, poison lance, dash, and 1,000 flying swords that burst out in a
ring and curve into the target, homing on the enemies tagged there), enemies with health that die in a pixelated
dissolve and respawn, and a temporary HUD for Qi and cooldowns. All of it deterministic and replayable.

**Architecture:** A cast is an action (`ActionKind::Cast`: slot, world point), recorded like any input. The arena keeps
the player's Qi and cooldowns, the creatures' health and afflictions, and each projectile cast as **one volley record**
(`core::Volley`) whose swords are a pure function of time (`core::SwordAt`). The logic schedules every hit from that
function's arrival ticks; it never moves 1,000 entities. The snapshot carries the volleys and their tagged targets. The
renderer evaluates every sword at the exact render time, against its target's interpolated aim point, into per-frame
overlay sprites (sword, halo) and ribbon trails, drawn over the world and under the text. Death is a dissolve progress
in the instance's spare flags, which the sprite shader turns into blocky pixels.

**Tech Stack:** C++23, EnTT (the state's domains), SDL_GPU with HLSL through `./dev shaders`, yaml-cpp (via `data`),
doctest, the reference pictures on lavapipe, Tracy (releasedbg).

**Spec:** `docs/superpowers/specs/2026-11-10-phase6-chunk4-abilities-design.md` (phase spec
`2026-11-10-phase6-design.md`, decisions 14 to 18). Task 1 adds the spec's missing Accessibility section (phase
decision 17), worded as in **Accessibility** below, once the user approves it.

## Global Constraints

- **Determinism:** the simulation draws randomness only from `core::Random` (keyed on the seed, `counters.state` and a
  purpose from `game/random_purposes.hpp`) and computes paths with `core::math` functions in f64. A recorded fight
  replays with every hash matching. Events change no state.
- **A volley is one record, not 1,000 entities.** Sword positions are `core::SwordAt(volley, sword, tick, aim)`, shared
  by the logic (arrival ticks, plunge points) and the renderer (the exact render time).
- **Smooth, not stiff, not jittery:** paths curve and ease, with no straight segments and no sudden turns. The renderer
  evaluates at `core::RenderTick(snapshot.simulationTick, alpha)`, never stepping by ticks.
- **One cast per click:** only a control's press casts. A held button never repeats.
- **Combat only:** `Cast` acts only in a combat scene (the arena). In the stress, painted and cells scenes it does
  nothing and logs at debug level.
- **Every use emits exactly one `UseEnded`, after its last hit**, including a use cut short by a death, a refresh, a
  scene change or a load (chunk 3's contract, `core::EventKind::UseEnded`).
- **UI and effect colours come from the table** (`core::UiColours`, Task 2): no such colour is a constant in code or a
  shader, and none is set by the simulation, which reports state (health, Qi, cooldowns, flashes) and leaves
  colouring to the main thread. Effects are coloured by element, never by ability; recolourable art is white and
  tinted.
- **Fast combat, slow long-term progression** (the user, 2026-11-11):
  - Health, damage, healing, affliction damage, Qi and every hit's value are **i64**: `Event::value`,
    `TextItem::value`, the damage numbers' sums, `Health`, `AbilityData::damage`, `Affliction::damage`,
    `CreatureData::health`.
  - Base values are in the hundreds (Task 1).
  - Every amount an ability deals passes through **one function**, `game::HitAmount(const core::AbilityData&, i64 base)`
    in `game/combat.hpp` (today it returns `base`). A hit, an affliction tick and the aura all call it, so stats and
    levels plug in there later without touching the abilities.
- **Small unit tests** (the user, 2026-11-12: "We should never have 1.9 million assertions in one suite, that is
  ludicrous. Just 20 samples should be enough, its just a unit test. We have profiling for profiling."): a test checks
  a behaviour on a handful of deliberately chosen samples, about 20 (first, last, a middle one, the edge case that
  matters), never a sweep over every sword and fine time step. Never one `CHECK` per sample in a big loop: compute the
  worst value and assert it once. Timing is profiling's job, breadth is fuzzing's.
- **Nothing dropped silently:** overlay instances and ribbon quads past their buffers are reported through
  `ReportOverflow` / `core::RepeatedFailure`, as instances and text quads are.
- **Formats:** `REPLAY_FORMAT` 3 → 4 (`Cast` carries a slot and a point); `ARENA_FORMAT` 2 → 3 (the combat state);
  `ACTIVE_VERSION` 3 → 4 (the new components). Regenerate the `replay` and `save` fuzz seeds; add an `abilities` yaml seed.
- **Spec values:** 1,000 swords; the dissolve takes about half a second (30 ticks); respawn a few seconds later
  (180 ticks) at the ground's edge, easing into an orbit over about three seconds (180 ticks); burn and poison tick
  once a simulation second (60 ticks); specials 2–4 and the ultimate are empty; the stress scene isn't a combat scene.
- **Repo rules** (AGENTS.md, src/AGENTS.md, tests/AGENTS.md): sized types, `std::expected` (no throws escape), Doxygen on
  every public declaration, `PN_PROFILE_ZONE()` in per-frame functions (not per-item ones), logging outcomes only,
  standard headers via `pch.hpp`, no third-party types in headers, `TEST_CASE`s in an anonymous namespace with
  `constexpr` test values, the MSVC gotchas (double braces on `std::array`, named temporaries inside `CHECK`).
- **Each task ends** with `pixi run project_n test` and `pixi run project_n lint` passing, and every commit builds on its
  own. Tasks that move pixels run `pixi run project_n reference --update`, look at the new pictures (look-at-the-game
  skill) and commit the PNGs with the change. After editing `shaders/`, run `pixi run project_n shaders` and commit
  `shaders/bin/` and `src/gfx/generated/` with it. After editing `art/`, run `pixi run project_n atlas` and commit
  `assets/manifest.yaml`, `src/core/generated/atlas.hpp` and `art/atlas.sha256` with it.
- **Commits:** Conventional Commits; scopes from `SCOPES`; `feat`/`fix` only for game code; one commit per task unless
  the task says otherwise; docs cite commits by subject.

## Review Focus

1. **F2 mid-volley:** cycling the scene while swords fly, or while a burn ticks, ends every open use with one
   `UseEnded` and leaves no volley in the snapshot, so combined numbers never wait forever (Task 7 test
   `a scene change mid-volley ends every open use`).
2. **Hot reload mid-volley** (debug builds, `abilities.yaml` edited while swords fly): swords in flight keep the shape
   and damage they were cast with; the next cast takes the new data (Task 7 test
   `new game data leaves a volley in flight as it was cast`).
3. **Casting while paused or unfocused:** a `Cast` taken on a frozen tick does nothing, then or after resuming (Task 4
   test `a cast sent while paused does nothing`).
4. **Keys 1–5 or Space with no cursor over the window** (the cursor outside the window, or the gamepad in use): no cast,
   no crash, a debug log line (Task 4 test `a key cast without a cursor sends nothing`).
5. **The selected or hovered fox dies:** the selection clears when the entity is destroyed, a dying fox carries no ID
   (it can't be hovered, outlined or picked mid-dissolve), and it loses its label and health bar at once (Task 3 tests
   `a dying fox carries no ID, label or health bar` and `the selection clears when the selected fox is removed`).

## Accessibility

Phase decision 17 requires this section in the spec, and the spec lacks it. Task 1 adds it to the spec, after the user
approves it:

- **Every UI colour can be changed** (the user, 2026-11-11: "we should be able to recolour basically any UI element in
  the accessibility settings menu"). Every UI colour, chunk 3's and this chunk's, lives in one table of named colours
  in the player's settings (`core::UiColours`), grouped by area as the menu shows them (the user: **Accessibility →
  Colours → area → element**):
  - **Combat Text:** each damage-number kind, with its variant for text without outline and shadow.
  - **Effects:** one colour per element (damage, healing, poison, burn), never per ability. Every ability's halos,
    trails, glows and bursts take their element's colour.
  - **Names and Captions:** labels, the caption, the text ink.
  - **Selection:** the hover and selection outlines.
  - **HUD:** the player's health bar, the Qi bar, enemies' health bars, the bars' background, and the HUD's text,
    cooling and warning colours.

  The main thread reads the table where each element is drawn, and a change shows from the next frame. The
  simulation never colours anything. Chunk 11's menu edits the table, and its colour-blind palettes are presets of it.
- **Text size:** the HUD's lines go through `gfx::TextOptions::scale`, as all game text does.
- **Colour is never the only signal:** health bars show health by the fill's length. A slot on cooldown shows its
  seconds left as a number, not just dimmed. Too little Qi shows the words "Not enough Qi", not just a red tint. The
  fire bolt and the poison lance differ in shape as well as colour (a flying bolt with a trail; a burst at the point).
- **Reduce flashing (chunk 11 wires it):** the halos and the bolt's glow take their brightness from one constant,
  `gfx::HALO_INTENSITY`, and the lance's burst from `gfx::FLASH_INTENSITY`. Chunk 11 lowers both under reduce
  flashing. The dissolve fades and never flashes.
- **Reduce motion (chunk 11 wires it):** the trail length is one constant, `gfx::TRAIL_TICKS`, which chunk 11 shortens
  under reduce motion. The swords' flight is the ability itself and stays. Nothing shakes the camera.
- **Input:** every ability is on the mouse and keyboard. Playing with a gamepad is a phase non-goal (decision 18 covers
  UI only), so casts have no gamepad bindings yet.

## File Structure

- `src/core/hit.hpp` (new): `HitKind`, `HIT_KINDS`, moved out of `frame.hpp` so the ability data can name them.
- `src/core/ui_colours.hpp` (new): `UiColour`, `UiColours`, the defaults; `core::Settings::uiColours`. Read by the
  renderer (labels, caption, ink, outlines, health bars), `app::DamageNumbers` and `app::Hud`.
- `src/core/volley.{hpp,cpp}` (new): `VolleyShape`, `Volley`, `VolleyTarget`, `SwordPose`, and the path and assignment
  functions the logic and renderer share.
- `src/core/abilities.hpp` (new): `Slot`, `SlotKind`, `AbilityKind`, `Affliction`, `AbilityData`, `Abilities`.
- `src/core/game_data.hpp`: `CreatureData::health`; `GameData::abilities`.
- `src/core/frame.hpp`: `ActionKind::Cast` and `Action::slot`/`point`; `EventKind::NotEnoughQi`; `Snapshot::combat`,
  `caster`, `volleys`, `volleyTargets`, `healthBars`; `VolleyView`, `CasterView`, `HealthBar`.
- `src/core/instances.{hpp,cpp}`: `SpriteDraw::dissolve` and `widthScale`. `core/frame.hpp` also carries `Flash`,
  `Snapshot::flashes`.
- `src/core/replay.cpp`: encodes `Cast`. `src/core/profile.hpp`: unchanged.
- `src/data/abilities.{hpp,cpp}` (new): `ParseAbilities`. `src/data/game_data.{hpp,cpp}`: reads both tables.
- `assets/data/abilities.yaml` (new), `assets/data/creatures.yaml` (`health`), `assets/data/strings.yaml` (HUD strings).
- `src/game/combat.{hpp,cpp}` (new): the combat components (`Health`, `Dying`, `Afflictions`, `Caster`, `Dashing`,
  `Arriving`) and `game::Combat`, the arena's combat state (volleys, pending lance hits, flashes, respawns) with its
  tick, save and publish. `src/game/arena.{hpp,cpp}`: owns a `Combat`, routes casts, the aura's real damage, the
  health it reports, combat-scene flag, save versions; labels and the caption lose their colours.
- `src/platform/window.{hpp,cpp}`: `InputCode::MouseRight`, `KeySpace`, `Key1`–`Key5`; `InputEvent::alt`.
- `src/app/bindings.{hpp,cpp}`: `Control::Primary` (was `Select`), `Secondary`, `Movement`, `Special1`–`Special4`,
  `Ultimate`; `ControlEvent::alt`. `src/app/cast.{hpp,cpp}` (new): pure click and key → action rules, cast latency.
- `src/app/hud.{hpp,cpp}` (new): the temporary HUD's lines. `src/app/app.{hpp,cpp}`: casts, the HUD, `--cast-swords`.
- `src/gfx/volley_draw.{hpp,cpp}` (new): swords, halos and trails at the render time, and the health bars, pure.
- `src/gfx/text_place.{hpp,cpp}`, `src/gfx/internal/features/meshes.{hpp,cpp}`, `shaders/mesh.frag.hlsl`,
  `src/app/damage_numbers.{hpp,cpp}`: colours from the table; `TEXT_INK`, `INK`, `KIND_COLOURS`,
  `DARK_KIND_COLOURS`, `LABEL_COLOUR`, `CAPTION_COLOUR`, `HOVER_COLOUR`, `SELECTED_COLOUR` go.
- `src/gfx/internal/features/ribbons.{hpp,cpp}` (new): the additive ribbon pipeline. `src/gfx/renderer.{hpp,cpp}`:
  draws the overlay sprites, ribbons and HUD lines.
- `shaders/sprite.*`: the dissolve. `shaders/ribbon.frag.hlsl` (new; vertices through `mesh.vert.hlsl`).
- `art/placeholders.py`, `art/sprites.toml`, `art/scenes.toml`: `sword`, `halo`, `fire_bolt`, `lance_strike`,
  `solid` (the bars), in the arena scene; all but `sword` white, tinted by the colour table.
- Tests: `tests/unit/core/volley_tests.cpp`, `tests/unit/data/abilities_tests.cpp`, `tests/unit/game/combat_tests.cpp`,
  `tests/unit/gfx/volley_draw_tests.cpp`, `tests/unit/app/cast_tests.cpp`, `tests/unit/app/hud_tests.cpp`,
  `tests/unit/core/ui_colours_tests.cpp`, additions to
  the arena, save, replay, bindings and reference suites; `tests/fuzz/yaml.cpp` and corpus seeds.

---

### Task 1: The ability data and the loadout

**Files:**
- Create: `src/core/hit.hpp`, `src/core/abilities.hpp`, `src/core/volley.hpp` (types only here, functions in Task 6),
  `src/data/abilities.hpp`, `src/data/abilities.cpp`, `assets/data/abilities.yaml`, `tests/unit/data/abilities_tests.cpp`,
  `tests/fuzz/corpus/yaml/abilities`
- Modify: `src/core/frame.hpp` (include `hit.hpp`), `src/core/game_data.hpp`, `src/data/game_data.{hpp,cpp}`,
  `assets/data/creatures.yaml`, `src/game/arena.cpp` (`BUILT_IN_DATA`), `tests/fuzz/yaml.cpp`, `tests/AGENTS.md`
  (yaml seeds), `docs/superpowers/specs/2026-11-10-phase6-chunk4-abilities-design.md` (the Accessibility section),
  `docs/superpowers/specs/2026-11-10-phase6-design.md` (decision 17),
  `docs/superpowers/specs/2026-11-10-phase6-chunk11-settings-design.md` (the colour menu)

**Interfaces:**
- Produces (`core/volley.hpp`):
  `struct VolleyShape { u32 count = 0; f32 ringRadius = 0; u32 staggerTicks = 0; u32 outTicks = 0; u32 flightTicks = 0; f32 sweep = 0; u32 fadeTicks = 0; Fields(); bool Valid() const; }`
  (`flightTicks` 0 means "from the distance and `AbilityData::speed`", used by the bolt).
- Produces (`core/abilities.hpp`):
  `enum class Slot : u8 { Generator, Spender, Movement, Special1, Special2, Special3, Special4, Ultimate }`,
  `SLOTS = 8`, `SLOT_NAMES` (`generator spender movement special_1 special_2 special_3 special_4 ultimate`);
  `enum class SlotKind : u8 { Generator, Spender, Movement, Special, Ultimate }`, `constexpr SlotKind KindOf(Slot)`;
  `enum class AbilityKind : u8 { None, FireBolt, PoisonLance, Dash, FlyingSwords }`, `ABILITY_KINDS = 5`,
  `ABILITY_NAMES` (`none fire_bolt poison_lance dash flying_swords`);
  `struct Affliction { u32 ticks = 0; i64 damage = 0; }` (ticks of damage, one a second);
  `struct AbilityData { SlotKind slot; u32 cooldownTicks; i64 qiCost; i64 qiGain; i64 damage; HitKind hit; f32 radius; Affliction affliction; f32 distance; u32 durationTicks; f32 speed; VolleyShape volley; }`
  (`distance`, `durationTicks`: the dash; `speed`: the bolt, world units a tick; `volley`: the swords' and the bolt's shape);
  `struct Abilities { i64 qiMax; i64 qiStart; std::array<AbilityData, ABILITY_KINDS> table; std::array<AbilityKind, SLOTS> loadout; const AbilityData& Of(AbilityKind) const; AbilityKind In(Slot) const; }`.
  Each has `Fields()` (for the hash) and, where read from a save, `Valid()`.
- Produces: `CreatureData::health` (i64); `GameData::abilities` (`core::Abilities`), both in `Fields()`.
- Produces (`data/abilities.hpp`): `std::expected<core::Abilities, core::Error> ParseAbilities(std::string_view abilities, std::vector<std::string>* warnings = nullptr)`;
  `data::ParseGameData(std::string_view creatures, std::string_view abilities, std::vector<std::string>* warnings = nullptr)`;
  `LoadGameData(folder)` reads `creatures.yaml` and `abilities.yaml`.

- [ ] **Step 1: Accessibility in the specs** (the user approved amending them, 2026-11-11). Add the **Accessibility**
  section above to chunk 4's spec, before "Decisions", with the same wording, plus the user's HUD bars (the player's
  health and Qi, Task 9) under Goal 10. In the phase spec, amend decision 17: *every UI colour, and every combat
  effect's colour by element (never by ability), can be changed in Accessibility → Colours, organised by area
  (Combat Text, Effects, Names and Captions, Selection, HUD) then element; colours live in one table the main thread
  reads, never in the simulation* (amended 2026-11-11, the user). In chunk 11's spec, its menu builds Accessibility →
  Colours → area → element from `core::COLOUR_AREA_INFO` and `UI_COLOUR_INFO`, and the colour-blind palettes become
  presets of that table.
- [ ] **Step 2: Write the failing tests** in `tests/unit/data/abilities_tests.cpp`:
  - `the abilities parse`: the shipped text gives `qiMax == 100`, `qiStart == 50`, `loadout[Special1] == FlyingSwords`,
    `Of(FlyingSwords).volley.count == 1'000`, `Of(FireBolt).hit == HitKind::Burn`, `Of(PoisonLance).hit == HitKind::Poison`,
    `Of(Dash).slot == SlotKind::Movement`, `In(Slot::Special2) == AbilityKind::None`, and a cooldown of `0.25` seconds
    is 15 ticks.
  - `an ability in a slot of another kind is refused` (`spender: dash`): error naming the line and both names.
  - `an unknown ability or slot is refused, naming the line`.
  - `a negative, non-finite or missing number is refused, naming the ability and the key`.
  - `an unknown key is a warning with its line, and the rest still parse`.
  - `a loadout may leave slots empty` (no `special_2` key: `AbilityKind::None`).
  - `the shipped abilities table is the built-in data` (in `tests/unit/game/arena_tests.cpp`, beside the creatures
    one: `data::LoadGameData(assets/data)` equals `game::BuiltInData()` by `Hash()`).
- [ ] **Step 3: Run them to see them fail:** `pixi run project_n test --suite data_tests --test abilities` → fails to
  compile (no `data/abilities.hpp`).
- [ ] **Step 4: Implement.** Move `HitKind`/`HIT_KINDS` into `core/hit.hpp` and include it from `frame.hpp`. Write the
  types above. `ParseAbilities` follows `data/game_data.cpp` (`internal::NoteUnknownKeys`, errors as
  `abilities line N: ...`). Seconds in the YAML become ticks rounded to nearest, at least 1 where the value is above 0.
  `assets/data/abilities.yaml`, these starting numbers (the spec leaves them to the builder; tune by eye in Task 8):
  ```yaml
  qi: { max: 100, start: 50 }
  abilities:
    fire_bolt:     { slot: generator, cooldown: 0.25, qi_gain: 10, damage: 140, element: burn, radius: 120, speed: 1500,
                     burn: { ticks: 3, damage: 40 } }
    poison_lance:  { slot: spender, cooldown: 0.5, qi_cost: 25, damage: 360, element: poison, radius: 90,
                     poison: { ticks: 4, damage: 60 } }
    dash:          { slot: movement, cooldown: 1.0, distance: 260, duration: 0.15 }
    flying_swords: { slot: special, cooldown: 6, qi_cost: 60, damage: 20, radius: 260,
                     swords: { count: 1000, ring_radius: 140, stagger: 0.5, out: 0.2, flight: 1.1, sweep: 420, fade: 0.3 } }
  loadout: { generator: fire_bolt, spender: poison_lance, movement: dash, special_1: flying_swords }
  ```
  (`speed` in world units a second becomes units a tick.) Add `health: 1000` to the cultivator and `health: 1200` to
  the fox in `creatures.yaml`, and mirror both files in `BUILT_IN_DATA`. These numbers follow the user's direction
  (2026-11-11): **fast combat, slow long-term progression.**
  - **Fast:** a fox dies to about six fire bolts (1.5 s) or three lances. A volley's 20,000 damage, spread over the
    three or four foxes it tags, kills them all, and the leftover swords plunge. Qi refills for a volley in about six
    bolts.
  - **Slow progression has room:** base values are in the hundreds, so a small gain (+1%, +1 level) changes a number
    you can see, rather than rounding away. Combat values are i64, so progression over a long game never overflows.
  - **The balance itself is a later plan:** a tool that simulates and generates stat and XP curves (backlog). These
    numbers only have to feel right in the arena now. `tests/fuzz/yaml.cpp` also calls
  `ParseAbilities(text, &warnings)`. Seed `corpus/yaml/abilities` with the shipped file, and add the line to
  tests/AGENTS.md.
- [ ] **Step 5: Run** `pixi run project_n test` and `pixi run project_n lint`: PASS.
- [ ] **Step 6: Commit** `feat(data): read the abilities and the loadout` (with the three specs' changes as
  `docs(design): make every colour recolourable and give chunk 4 its accessibility`, a separate commit before it).

### Task 2: Every UI colour in one table

The user's rule (2026-11-11): **"we should be able to recolour basically any UI element in the accessibility settings
menu."** Chunk 11 builds the menu. This chunk adds UI elements (health bars, the HUD), so it first gives every UI
colour, old and new, one home: a table of named colours in the player's settings, read on the main thread where each
element is drawn. No UI colour stays a scattered constant or is set by the simulation. Today four places hold them:
the simulation (`LABEL_COLOUR`, `CAPTION_COLOUR` in `arena.cpp`), a shader constant (`INK` in `mesh.frag.hlsl`, mirrored
by `gfx::TEXT_INK`), the renderer (`HOVER_COLOUR`, `SELECTED_COLOUR`) and the app (`KIND_COLOURS`, `DARK_KIND_COLOURS`).

**Files:**
- Create: `src/core/ui_colours.hpp`, `tests/unit/core/ui_colours_tests.cpp`
- Modify: `src/core/settings.hpp`, `src/core/strings.hpp`, `assets/data/strings.yaml`, `tests/unit/data/strings_tests.cpp`, `src/core/frame.hpp` (`TextItem::colour`'s doc), `src/game/arena.cpp`,
  `src/gfx/text_place.{hpp,cpp}`, `src/gfx/renderer.{hpp,cpp}`, `src/gfx/internal/features/meshes.{hpp,cpp}`,
  `shaders/mesh.frag.hlsl`, `src/app/damage_numbers.{hpp,cpp}`, `src/app/app.cpp`, `src/main.cpp`,
  `tests/unit/gfx/text_place_tests.cpp`, `tests/unit/app/damage_numbers_tests.cpp`, `tests/unit/game/arena_tests.cpp`,
  `tests/reference/reference_tests.cpp` (one new picture)

**Interfaces:**
- Produces (`core/ui_colours.hpp`), ordered by area as the menu lists them:
  `enum class ColourArea : u8 { CombatText, Effects, NamesAndCaptions, Selection, Hud }`, `COLOUR_AREAS = 5`;
  `enum class UiColour : u8 { DamageNumber, HealingNumber, PoisonNumber, BurnNumber, DarkDamageNumber, DarkHealingNumber, DarkPoisonNumber, DarkBurnNumber, DamageEffect, HealingEffect, PoisonEffect, BurnEffect, Label, Caption, TextInk, HoverOutline, SelectedOutline, HealthBar, QiBar, EnemyHealthBar, BarBackground, HudText, HudCooling, HudWarning }`,
  `UI_COLOURS = 24`;
  `struct ColourAreaInfo { std::string_view key; StringId name; }` and `COLOUR_AREA_INFO` (keys `combat_text`,
  `effects`, `names_and_captions`, `selection`, `hud`);
  `struct UiColourInfo { ColourArea area; std::string_view key; StringId name; }` and `UI_COLOUR_INFO`, one per
  `UiColour`: its area, its key within the area (`damage`, `healing`, `poison`, `burn`, `damage_no_outline`, …;
  effects `damage`, `healing`, `poison`, `burn`; `label`, `caption`, `text_ink`, `hover`, `selected`, `health_bar`,
  `qi_bar`, `enemy_health_bar`, `bar_background`, `text`, `cooling`, `warning`) and the string that names it in the
  menu;
  `UiColour EffectColour(HitKind element)`, `UiColour NumberColour(HitKind kind, bool dark)`. **Effects are coloured
  by element, never by ability** (the user, 2026-11-11: there will be hundreds of abilities). An ability's halo,
  trail, glow and burst take the colour of its element (`AbilityData::hit`): the swords Damage, the fire bolt Burn,
  the poison lance Poison. A new ability never adds a colour. The menu (chunk 11) is **Accessibility → Colours → area →
  element**, built from these two tables (the user, 2026-11-11: "Accessibility → Colours → Combat Text →
  Damage/Health/Poison", "Accessibility → Colours → HUD → Health Bar/Qi Bar"). The settings file nests the same way,
  `colours: { combat_text: { damage: "#RRGGBBAA" } }`, when chunk 11 reads it. The strings (`colour_area_*`,
  `colour_*`, English and Chinese) join `strings.yaml` here, so the menu's words exist and are prewarmed;
  `struct UiColours { std::array<u32, UI_COLOURS> colours; u32 Of(UiColour) const; void Set(UiColour, u32); }`,
  default-built to `DEFAULT_UI_COLOURS`. Values are packed as drawn: linear, premultiplied, red in the low byte (as
  `TextItem::colour`). The defaults are **today's constants, bit for bit**, so no picture changes. The new elements'
  defaults, in sRGB: effects damage #6FA8FF (azure; Task 10e, from the near-white #CFE3FF), healing #8FE3A8, poison
  #7CD957, burn #FF8A3D; health bar and enemy health bar #D8574A, Qi bar #5AA9E6, bar background #20242A; HUD text
  #F4EFE6, cooling #8A8F96, warning #F08A5D.
  `u32 FromSrgb(u8 r, u8 g, u8 b, u8 a)` and its inverse, for chunk 11's menu (exact round trip on every default).
- Produces: `core::Settings::uiColours` (`core::UiColours`, defaults). `settings.yaml` doesn't read it until chunk 11.
- Produces: `gfx::Renderer::Create(..., const TextOptions&, const core::UiColours&)` and
  `void Renderer::SetUiColours(const core::UiColours&)` (from the next frame, no restart);
  `gfx::TextColour(const core::TextItem&, const core::UiColours&, bool outline, bool shadow)` (labels and captions
  from the table, numbers keep their own; `TEXT_INK` goes); `internal::TextStyle::ink` (a float4 the field shader
  takes in place of `INK`); `app::DamageNumbers::SetColours(const core::UiColours&)`, colours looked up when the
  numbers become texts, so a change recolours live numbers too.

- [ ] **Step 1: Write the failing tests:**
  - core: `every UI colour belongs to an area, with a key unique in its area and a name`; `each area's colours sit
    together, in the menu's order, and no area is empty`; `every area and colour name reads in both languages`
    (strings_tests); `the defaults are today's colours` (each old constant's
    value, written out in the test: label `0xFFCADCE7`, caption `0xFF5AB1E2`, ink `0xFF060404`, hover `0xFFF0F4F4`,
    selected `0xFF30C0FF`, and `KIND_COLOURS`/`DARK_KIND_COLOURS`' current values); `sRGB round-trips through the
    table on every default`.
  - arena: `labels and the caption leave their colour to the table` (published `TextItem`s keep the default colour).
  - text_place: `a label and a caption take their colour from the table`; `with the outline and the shadow off they
    take the table's ink`.
  - damage_numbers: `a number takes its kind's colour from the table, the dark one without outline and shadow`;
    `a new table recolours the numbers already showing`.
- [ ] **Step 2: Run them to see them fail.**
- [ ] **Step 3: Implement.** Route each old constant through the table and delete it. The field shader takes the ink
  from `TextStyle` (run `pixi run project_n shaders`). `main` passes `settings.uiColours` to the renderer, and `App`
  passes it to `DamageNumbers`.
- [ ] **Step 4: Prove nothing moved:** `pixi run project_n reference` passes **without** `--update` on every existing
  picture. Then add `TEST_CASE("labels, the caption, outlines and numbers in a recoloured table")` (every entry set
  to a distinct, garish colour) and run `reference --update` for that one only. Look at it: every element changed.
- [ ] **Step 5: Run** `pixi run project_n test` and `pixi run project_n lint`: PASS.
- [ ] **Step 6: Commit** `feat(gfx): draw every UI colour from one table in the settings`.

### Task 3: Health, death and respawn

**Files:**
- Create: `src/game/combat.hpp`, `src/game/combat.cpp`, `tests/unit/game/combat_tests.cpp`
- Modify: `src/game/arena.{hpp,cpp}`, `src/game/random_purposes.hpp`, `src/core/frame.hpp`,
  `src/app/damage_numbers.{hpp,cpp}`, `src/gfx/renderer.cpp` (a number's text), `tests/unit/app/damage_numbers_tests.cpp`,
  `src/core/instances.{hpp,cpp}`, `shaders/sprite.vert.hlsl`, `shaders/sprite.frag.hlsl`, `shaders/sprite.hlsli`,
  `tests/unit/game/{arena,save}_tests.cpp`, `tests/reference/reference_tests.cpp` and its PNGs

**Interfaces:**
- Consumes: `CreatureData::health` (Task 1).
- Produces (`game/combat.hpp`), each with `Fields()` and `Valid()`:
  `struct Health { i64 current; i64 max; }` (valid: `0 <= current <= max`, `max > 0`);
- Produces: `Event::value` and `TextItem::value` widen to i64, with `app::DamageNumbers`' combined sums (an
  amount's digits are whole, however many; compact notation such as 1.2M or 1.2亿 comes with the progression plan);
  `i64 game::HitAmount(const core::AbilityData& ability, i64 base)` (returns `base` for now; the aura passes its own
  base).
  `struct Dying { u64 startTick; }`;
  `struct Arriving { core::Vec2 from; u64 startTick; u32 ticks; }` (a respawned creature easing into its orbit);
  constants `DYING_TICKS = 30`, `RESPAWN_TICKS = 180`, `ARRIVE_TICKS = 180`.
- Produces: `class Combat` with `void Damage(core::State&, core::StableId target, i64 amount, u64 tick)` (also heals
  with a negative amount, capped at `max`; at 0 adds `Dying`); `void Tick(...)` (dying → destroyed after `DYING_TICKS`, a
  respawn queued `RESPAWN_TICKS` later, a queued respawn spawning a fox at a random point on the ground's edge with an
  `Orbit` round the player and `Arriving`); `Save`/`Load`/hash of its queue. Arena calls them; the exact split between
  `Arena` and `Combat` is the builder's.
- Produces: `core::SpriteDraw::dissolve` (f32, 0 to 1), packed into the high half of `Instance::layerFlags` as unorm16
  (`layerFlags`' doc names it).
- Produces (`core/frame.hpp`): `struct HealthBar { StableId entity; Vec2 previous; Vec2 current; f32 fraction; }` (the
  head points, as labels have them; `fraction` = current / max) and `Snapshot::healthBars`, refilled in place, for
  each living enemy in a combat scene. The simulation reports health; the main thread draws the bars in the table's
  colours (Task 8). The simulation never colours UI.

- [ ] **Step 1: Write the failing tests** (`tests/unit/game/combat_tests.cpp`; ticks driven as arena_tests does):
  - `every creature starts at its kind's health`: the player at 1,000, each fox at 1,200.
  - `the aura's damage lowers a fox's health and heals the player up to its most`: a fox in reach loses exactly its
    hit's value; the player, damaged first, gains its healing but never passes 1,000.
  - `a hit of three billion is shown and summed whole`: an `Event` of 3,000,000,000 becomes a number reading
    "3000000000"; two combined make 6,000,000,000 (damage_numbers_tests).
  - `health at zero gives a dying state, then removal, then a respawn on the edge`: a fox damaged by 1,200 has `Dying`
    at that tick; `DYING_TICKS` later it's gone (`State().DomainOf(id)` empty); `RESPAWN_TICKS` after that a new fox
    exists (a new ID) whose foot is on the ground's border (x is 0 or 1920, or y is 0 or 1080, within 1e-3); the fox
    count is back to 12.
  - `a respawned fox eases into its orbit`: its foot moves from the edge to `OrbitPoint` over `ARRIVE_TICKS`, the step
    between ticks never more than twice the largest step of an orbiting fox plus the approach (no snap), and equals
    `OrbitPoint` from then on.
  - `a dying fox carries no ID, label or health bar` (Review Focus 5): its instance's ID is zero and its dissolve
    progress (high half of `layerFlags`) rises from 0 to 65535 over `DYING_TICKS`. The snapshot has no `TextItem` for it.
  - `the selection clears when the selected fox is removed` (Review Focus 5).
  - `each living enemy in a combat scene reports its health over its head`: one `HealthBar` per living fox, at its
    head, `fraction` = current / max; none for the player, a dying fox, or in the stress scene.
  - `damage to a dying or removed creature does nothing` (no second `Dying`, no crash on a stale ID).
  - Save: `health, the dying and the respawns survive a save` (mid-dissolve and mid-respawn-wait, the loaded game's
    `StateHash()` equal after ticking both alike), and `a save with health above its most is refused`.
- [ ] **Step 2: Run them to see them fail:** `pixi run project_n test --suite game_tests --test combat` → compile errors.
- [ ] **Step 3: Implement.** The aura (`Arena::Aura`) now applies its hits through `Combat::Damage` (healing on the
  player), still emitting the same events; its values scale ×10 (`AURA_DAMAGE_*` 50–400, `AURA_HEALING_*` 50–200)
  to match the new magnitudes. Widen `Event::value`, `TextItem::value` and the damage numbers to i64 (the arena's
  existing aura and burst tests keep their meaning, with the new values). The debug burst stays numbers only. `ARENA_FORMAT` 3, `ACTIVE_VERSION` 4,
  `ActiveComponents` gains `Health, Dying, Arriving`; `Consistent()` requires `Health` on every `Creature`. Publish a
  dying creature's instance with `dissolve = (tick − startTick) / DYING_TICKS` and ID zero. Publish `healthBars`.
  `LABEL_OFFSET.y` becomes −12 to leave room for the bar Task 8 draws between the head and the label.
- [ ] **Step 4: The dissolve in the sprite shader.** The vertex stage passes `dissolve` (nointerpolation) on. The
  fragment stage, for `dissolve > 0`, discards a block when a hash of its block coordinate is below `dissolve`, and
  scales the rest's alpha by `1 − dissolve`. Blocks are `DISSOLVE_BLOCK_PIXELS = 4` screen pixels square, anchored to
  the sprite (`floor(uv / (fwidth(uv) × DISSOLVE_BLOCK_PIXELS))`) so the pattern travels with it and stays crisp at
  every zoom. Outlines ignore it. Run `pixi run project_n shaders`.
- [ ] **Step 5: Reference pictures.** Add `TEST_CASE("a fox dissolving, at half its progress")`: the arena built with
  game data whose fox health is 1, so the aura's first hit (tick 60) kills the foxes in its reach, captured
  `DYING_TICKS / 2` ticks later at `gfx::MAX_ZOOM`, centred on the dying fox nearest the crystal. Run
  `pixi run project_n reference --update`, look at every changed picture (the arena's labels sit higher), and commit
  them.
- [ ] **Step 6: Run** `pixi run project_n test`, `pixi run project_n lint`, `pixi run project_n reference`: PASS.
- [ ] **Step 7: Commit** `feat(game): give creatures health, a dissolving death and a respawn`.

### Task 4: The cast action and its input

**Files:**
- Create: `src/app/cast.hpp`, `src/app/cast.cpp`, `tests/unit/app/cast_tests.cpp`
- Modify: `src/core/frame.hpp`, `src/core/replay.cpp`, `src/platform/window.{hpp,cpp}`, `src/app/bindings.{hpp,cpp}`,
  `src/app/app.{hpp,cpp}`, `src/game/arena.{hpp,cpp}`, `tests/unit/core/replay_tests.cpp`,
  `tests/unit/app/bindings_tests.cpp`, `tests/unit/core/logic_tests.cpp`, `tests/fuzz/corpus/replay/seed`

**Interfaces:**
- Produces (`core/frame.hpp`): `ActionKind::Cast` (last kind: `ACTION_KINDS` follows), `Action::slot` (`Slot`),
  `Action::point` (`Vec2`, world units); `Snapshot::combat` (bool: the scene is a combat scene).
- Produces (`platform/window.hpp`): `InputCode::MouseRight, KeySpace, Key1, Key2, Key3, Key4, Key5` (after
  `PadRightShoulder`: 29 codes, still under the 32 bits `Bindings` gives them); `InputEvent::alt` (Alt held, for mouse
  presses, from `SDL_GetModState()`); `Bindings`' `static_assert` on the last code moves to `Key5`.
- Produces (`app/bindings.hpp`): `Control::Primary` replaces `Control::Select` (left click); new `Secondary` (right
  click), `Movement` (Space), `Special1`–`Special4` (1–4), `Ultimate` (5); `ControlEvent::alt`.
- Produces (`app/cast.hpp`, pure):
  `std::optional<core::Slot> SlotOf(Control control)` (Primary → Generator ... Ultimate → Ultimate; others none);
  `std::optional<core::Action> CastAction(Control control, bool pressed, bool combat, bool alt, std::optional<core::Vec2> point)`
  (a cast only on a press, in combat, with a point; Primary with Alt held, or out of combat, is a selection instead:
  `bool SelectsInstead(Control, bool combat, bool alt)`).
- Produces: `game::Arena::IsCombatScene() const` (from `SceneEntry::combat`: the arena only).

What each mouse button does (the spec's goals 1 and 9, decision 6):

| Scene | Left click | Alt + left click | Right click | Space, 1–5 |
|---|---|---|---|---|
| Combat (the arena) | casts the **generator** (the fire bolt) at the point | selects (debug, until chunks 5–6) | casts the **spender** (the poison lance) | cast movement, specials 1–4, ultimate |
| Any other | selects | selects | nothing | nothing |

**Room for melee combos** (spec non-goal, decision 10: one cast per click, with a short window for the next one
later):
- **Every press is its own `Cast` action** carrying the slot and the point. A press is never merged with another,
  never repeated while held, never debounced in `app`, and the action holds no hold time or press count.
- **The simulation decides what a press does.** A press during the generator's cooldown is dropped there (Task 5).
- **A combo needs no change to the action format or to `app`.** It would keep its step and window's end on `Caster`,
  read each generator `Cast` as the chain's next step, and become a save format bump, not a replay one.

The tests pin that shape: `two quick left clicks are two casts` (app), and Task 5's cooldown test.

- [ ] **Step 1: Write the failing tests:**
  - core replay: `a cast's slot and point play back bit for bit`, and `a replay with a cast in a slot past the last, or
    at a non-finite point, is refused`.
  - core logic: `a cast sent while paused does nothing` (Review Focus 3): a `Pause`, a `Cast`, a `Resume`, ticks; the
    simulation saw no `Cast` (a recording `Simulation` stub, as logic_tests already uses).
  - bindings: `the right button, Space and 1 to 5 drive their controls` and `the left button is Primary and carries
    Alt`.
  - cast: `a left click in combat casts the generator at the point`; `a right click in combat casts the spender`;
    `two quick left clicks are two casts` (two presses with a release between, in one frame's events: two actions, in
    order, each with its own point); `a press in combat casts its slot at the point`; `a release casts nothing`;
    `a held button casts once`
    (press, then no further events, then release: one action); `Alt and a left click selects instead in combat`;
    `outside combat a left click selects and the other slots cast nothing`; `a key cast without a cursor sends nothing`
    (Review Focus 4: `point` none → none).
  - arena: `a cast outside a combat scene is ignored` (stress: state hash unchanged, no events);
    `the arena is a combat scene and the others aren't` (`snapshot.combat`).
- [ ] **Step 2: Run them to see them fail.**
- [ ] **Step 3: Implement.** `REPLAY_FORMAT` 4. A `Cast` records `kind, slot, point`, and reading one checks
  `slot < SLOTS` and a finite point. In `App::HandleInput`, a slot control asks `CastAction` with
  `drawn->combat`, the event's Alt flag and the cursor's world point (`WindowToWorld`; the clicked position for
  mouse buttons, `m_window.Cursor()` for keys; none when the cursor isn't over the window or the gamepad is in use),
  and sends the action. `Primary` that `SelectsInstead` goes to `Select()`. The arena takes `Cast` only in a combat
  scene, and in this task does nothing else with it (Task 5 acts on it). Regenerate the replay fuzz seed (an arena
  replay with a cast in it, as its seed was made; tests/AGENTS.md says how).
- [ ] **Step 4: Run** `pixi run project_n test` and `pixi run project_n lint`: PASS.
- [ ] **Step 5: Commit** `feat(app): send a cast per press of an ability's control`.

### Task 5: Qi, cooldowns and the dash

**Files:**
- Modify: `src/game/combat.{hpp,cpp}`, `src/game/arena.{hpp,cpp}`, `src/core/frame.hpp`,
  `tests/unit/game/combat_tests.cpp`, `tests/unit/game/save_tests.cpp`

**Interfaces:**
- Consumes: `Action::slot/point`, `Abilities` (Tasks 1, 4).
- Produces (`game/combat.hpp`): `struct Caster { i64 qi; std::array<u64, core::SLOTS> readyTick; }` on the player
  (a slot casts at a tick ≥ its `readyTick`); `struct Dashing { core::Vec2 from; core::Vec2 to; u64 startTick; u32 ticks; }`.
- Produces (`core/frame.hpp`): `EventKind::NotEnoughQi` (`value` = the slot); `struct CasterView { i64 qi; i64 qiMax; i64 health; i64 healthMax; std::array<u64, SLOTS> readyTick; std::array<AbilityKind, SLOTS> loadout; }`;
  `Snapshot::caster` (`std::optional<CasterView>`, set in combat scenes with a player).
- Produces: the use ID of a cast, `core::AbilityUse(tick, CAST_SOURCE + slot)` with `game::CAST_SOURCE = 16`.

- [ ] **Step 1: Write the failing tests** (`combat_tests.cpp`):
  - `the player starts with the data's starting Qi`.
  - `the generator gains Qi up to the most; the spender and a special cost it`.
  - `a cooldown blocks a recast until it's over`: a second fire bolt `cooldownTicks − 1` ticks later does nothing and
    isn't queued (no bolt appears when the cooldown ends); a bolt cast at `cooldownTicks` goes off.
  - `a cast without enough Qi does nothing but report it`: no Qi change, no cooldown, one `NotEnoughQi` event with the
    slot as its value.
  - `an empty slot casts nothing`.
  - `the dash moves the player toward the point, by its distance, over its duration`: the player's foot reaches
    `from + normalised(point − from) × distance` (or the point, if nearer) at `startTick + ticks`, eased, with held
    movement ignored meanwhile; foxes orbiting the player follow.
  - save: `Qi, cooldowns and a dash under way survive a save`.
- [ ] **Step 2: Run them to see them fail.**
- [ ] **Step 3: Implement.** Casting order in a tick: cooldown, then Qi, then the ability's effect (the dash here; the
  others in Tasks 7 and 9 start their records). `Caster`, `Dashing` join `ActiveComponents` (`ACTIVE_VERSION` stays 4,
  bumped once in this branch). Publish `Snapshot::caster`.
- [ ] **Step 4: Run** `pixi run project_n test` and `pixi run project_n lint`: PASS.
- [ ] **Step 5: Commit** `feat(game): spend and gain Qi, cool abilities down, dash`.

### Task 6: The volley path

**Files:**
- Create: `src/core/volley.cpp`, `tests/unit/core/volley_tests.cpp`
- Modify: `src/core/volley.hpp`

**Interfaces:**
- Produces (`core/volley.hpp`):
  `struct Volley { u64 use; u64 startTick; u64 seed; Vec2 origin; Vec2 point; VolleyShape shape; Fields(); Valid(); }`;
  `struct VolleyTarget { StableId entity; Vec2 previous; Vec2 current; bool alive; Fields(); }` (entity zero: the point
  itself, when nothing was tagged; never alive);
  `struct SwordPose { Vec2 position; Vec2 heading; f32 shown; }` (`heading` unit length; `shown` 0 before launch, 1 in
  flight, falling to 0 over `fadeTicks` after arrival);
  `u64 LaunchTick(const Volley&, u32 sword)`, `u64 ArrivalTick(const Volley&, u32 sword)`, `u64 LastArrival(const Volley&)`;
  `SwordPose SwordAt(const Volley&, u32 sword, f64 tick, Vec2 aim)`;
  `u32 SwordTarget(u32 sword, std::span<const VolleyTarget> targets)` (targets non-empty).

The path, which the tests pin and the builder shapes within (the spec: "burst into a ring, then sweep and converge"):
- **Launch:** `LaunchTick = startTick + ToBelow(Random(seed, sword), staggerTicks)`: the swords stream out over the
  stagger rather than all at once.
- **Out:** for `outTicks`, from `origin` to the ring point `origin + dir × ringRadius`, eased out, where
  `dir` is the angle `2π × sword / count` (a whole ring) with a small jitter from the seed.
- **Flight:** for `flightTicks`, a cubic Bézier from the ring point to `aim`. The first control point lies along `dir`
  (so the heading is continuous where the out phase ends), turned sideways by `sweep` (the same turning sense for
  every sword, so the ring swirls round the caster). The second lies back from `aim` along the approach. The curve's
  end is `aim` at every evaluation, so it follows a moving target. Time is eased in and out.
- **After arrival:** `position` stays at the aim it arrived at, `shown` fades.
- **SwordTarget:** the primary target is `sword % n`. If it's alive, the sword keeps it. If it's dead and some target
  is alive, the sword turns to the living ones, spread evenly: `living[(sword / n) % living.size()]`. So only a dead
  enemy's swords move. If none is alive, the primary (frozen where it died) is where the sword plunges.
- In f64 with `core::math` sine and cosine; `f32` out.

- [ ] **Step 1: Write the failing tests:**
  - `every sword launches within the stagger and arrives after it`.
  - `the path is smooth`: for swords 0, 1, 499 and 999, sampled every 1/8 tick from launch to arrival, with a still aim
    and an aim moving at 3 units a tick: consecutive headings turn by less than 0.25 rad, and consecutive positions are
    never more than 3× the median step apart (no jumps).
  - `the path curves`: the largest distance of the flight from the straight line ring-point→aim is at least
    `sweep / 4` (not stiff).
  - `the swords burst out in a ring`: at `LaunchTick + outTicks` each sword is `ringRadius` from the origin (1e-3), and
    their angles cover the circle (the largest gap under 3 × 2π / count).
  - `the end follows a moving aim`: at `ArrivalTick` the position equals the aim given, for two different aims.
  - `the same volley gives the same positions bit for bit` (two evaluations, and `LastArrival` = max of `ArrivalTick`).
  - `hits split evenly over the tagged targets`: with 4 alive targets and 1,000 swords, each gets 250.
  - `a dead target's swords move to the living, the others stay`: kill target 1 of 4; the swords that had 0, 2, 3 keep
    them; target 1's 250 spread 83/83/84 over 0, 2, 3.
  - `with every target dead, each sword keeps its primary` (to plunge there).
- [ ] **Step 2: Run them to see them fail:** `pixi run project_n test --suite core_tests --test volley`.
- [ ] **Step 3: Implement** as above.
- [ ] **Step 4: Run** `pixi run project_n test` and `pixi run project_n lint`: PASS.
- [ ] **Step 5: Commit** `feat(core): shape flying swords as a function of time`.

### Task 7: The flying swords and the fire bolt in the simulation

**Files:**
- Modify: `src/game/combat.{hpp,cpp}`, `src/game/arena.{hpp,cpp}`, `src/core/frame.hpp`,
  `tests/unit/game/combat_tests.cpp`, `tests/unit/game/save_tests.cpp`

**Interfaces:**
- Consumes: `core::Volley`, `SwordTarget`, `ArrivalTick` (Task 6); `Caster` (Task 5).
- Produces (`core/frame.hpp`): `struct VolleyView { Volley volley; AbilityKind ability; HitKind element; u32 firstTarget; u32 targets; }`
  (`element` picks the effect colour; `ability` picks only the sprite);
  `Snapshot::volleys` (`std::vector<VolleyView>`), `Snapshot::volleyTargets` (`std::vector<VolleyTarget>`), both
  refilled in place.
- Produces (`game/combat.hpp`): `struct AfflictionState { i64 damage; u32 ticksLeft; u64 use; u64 nextTick; }`;
  `struct Afflictions { AfflictionState burn; AfflictionState poison; }` (a creature component).
- The volley's targets are aimed at each creature's chest (its foot raised by half its sprite's foot height × scale).

Rules this task builds (the spec's Design):
- **Tagging:** at the cast, every living enemy within the ability's `radius` of the point, in canonical (stable ID)
  order. With none tagged, one target at the point (`entity` zero). The swords: `count` from the data. The fire bolt:
  a volley of 1 at the tagged enemy nearest the point, `flightTicks` = ⌈distance / speed⌉, `ringRadius` 0, `sweep` 0.
- **Each tick:** update each target's aim and `alive`. A dead target's aim freezes. At each sword's `ArrivalTick`, if
  `SwordTarget`'s target is alive, it takes `damage` and a `Hit` event (the ability's `hit` kind at the target's head,
  the cast's use). Otherwise the sword plunges, with no event. Swords landing on one tick land in sword order.
- **Afflictions:** the bolt's hit also sets `burn` (damage, ticks, the cast's use, next at hit + 60). A second
  application refreshes it, and the old use, if it was waiting only on the affliction, ends then. Each tick of a burn
  is a `Burn` hit of the same use.
- **Ends:** a volley's use ends with `UseEnded` at `LastArrival` when it left no affliction, else after its
  affliction's last tick (or at its creature's death or its refresh). A scene change (`CycleScene`) or a load ends
  every open use first.
- The volley stores its own shape and damage from the cast, so new game data never changes one in flight.

- [ ] **Step 1: Write the failing tests:**
  - `every sword's hit is scheduled once and the use ends with the last`: cast at 3 foxes with ample health. Count the
    `Hit` events with the use (== swords aimed at living targets = 1,000), then exactly one `UseEnded`, at
    `LastArrival`, after every hit.
  - `hits split evenly over the tagged enemies` (4 tagged, health 10,000: 250 each).
  - `a dead enemy's swords move to the living`: tag 2 foxes, one with health 3 × 1. After it dies, every later hit
    goes to the other.
  - `with all tagged enemies dead the rest plunge and emit nothing`: health 3 each for 2 foxes. After both die, no more
    `Hit` events. `UseEnded` still at `LastArrival`.
  - `with none tagged, the swords strike the point and emit nothing but the end`.
  - `a burn ticks its count and stops, each tick a Burn hit of the bolt's use`, and `a second bolt refreshes the burn;
    its later ticks belong to the latest use, and the first use ends`.
  - `the bolt strikes the tagged enemy nearest the point`.
  - `the snapshot carries each volley and its targets` (entity IDs, alive flags, current = aim).
  - `a scene change mid-volley ends every open use` (Review Focus 1): exactly one `UseEnded` per open use on the
    `CycleScene` tick, and the next snapshot has no volleys.
  - `new game data leaves a volley in flight as it was cast` (Review Focus 2).
  - save: `the save round-trips mid-volley`: save at half the flight, load into a fresh arena, tick both to past
    `LastArrival`. Same events (ignoring epoch) and the same `StateHash()` at each tick.
- [ ] **Step 2: Run them to see them fail.**
- [ ] **Step 3: Implement.** `Combat` saves its volleys (count, then each volley, ability, damage, affliction and
  targets) inside the arena's format 3. Each volley's arrival order is derived on load, not saved. `Afflictions`
  joins `ActiveComponents`.
- [ ] **Step 4: Run** `pixi run project_n test` and `pixi run project_n lint`: PASS.
- [ ] **Step 5: Commit** `feat(game): cast the flying swords and the fire bolt`.

### Task 8: Drawing the swords and the health bars: halos, trails, the overlay

**Files:**
- Create: `src/gfx/volley_draw.hpp`, `src/gfx/volley_draw.cpp`, `src/gfx/internal/features/ribbons.hpp`,
  `src/gfx/internal/features/ribbons.cpp`, `shaders/ribbon.frag.hlsl`, `tests/unit/gfx/volley_draw_tests.cpp`
- Modify: `src/gfx/renderer.cpp`, `src/core/instances.{hpp,cpp}`, `art/placeholders.py`, `art/sprites.toml`,
  `art/scenes.toml`, `tests/reference/reference_tests.cpp` and new PNGs, `src/AGENTS.md` (the overlay in the frame's
  order)

**Interfaces:**
- Consumes: `Snapshot::volleys/volleyTargets`, `core::SwordAt`, `SwordTarget`; `Snapshot::healthBars` (Task 3);
  `core::UiColours` (Task 2).
- Produces: `core::SpriteDraw::widthScale` (f32, default 1, scales the quad's width about its foot).
- Produces (`gfx/volley_draw.hpp`, pure): `void DrawHealthBars(std::span<const core::HealthBar> bars, f32 alpha, const core::Manifest& manifest, const core::UiColours& colours, VolleyDrawing& out)`:
  per bar a `solid` quad 44×6 in `BarBackground` and one 40×4 in `EnemyHealthBar`, `widthScale = fraction`, left
  aligned inside it, centred `HEALTH_BAR_RISE = 5` world units over the head interpolated by `alpha`. Normal
  pipeline, drawn before the swords. Health shows by length, whatever the colours. (`solid`: an 8×8 white sprite,
  foot at its left middle, which every bar tints and scales; no bar art.)
- Produces (`gfx/volley_draw.hpp`, pure):
  `inline constexpr u32 TRAIL_SEGMENTS = 8;` `inline constexpr f32 TRAIL_TICKS = 6.0F;` (how far back the trail
  samples) `inline constexpr f32 HALO_INTENSITY = 1.0F;` (chunk 11's reduce flashing lowers it);
  `struct RibbonVertex` (16 bytes, the layout of `MeshVertex`, positions in **world units**; `uv`: u along the trail
  from 0 at the sword to 1 at its tail, v across from 0 to 1);
  `struct VolleyDrawing { std::vector<core::Instance> instances; std::vector<core::Batch> batches; std::vector<RibbonVertex> ribbons; void Clear(); }`;
  `void DrawVolleys(std::span<const core::VolleyView> volleys, std::span<const core::VolleyTarget> targets, f64 renderTick, f32 alpha, const core::Manifest& manifest, const core::UiColours& colours, core::Jobs& jobs, VolleyDrawing& out)`.
- Produces (internal): `RibbonRenderer` (mirrors `MeshRenderer`: `Create(device, format)`, `Upload(copy, vertices)`
  returning quads dropped past `MAX_RIBBON_QUADS = 16'384`, `Draw(commands, pass, worldToClip)`), additive
  (ONE, ONE), vertex stage `mesh.vert.hlsl` fed the world-to-clip transform, fragment `ribbon.frag.hlsl`: colour ×
  (1 − u)² × a soft profile across v.
- Produces: a second `SpriteRenderer` for the overlay, `MAX_OVERLAY_INSTANCES = 4'096`, uploaded every frame.

Drawing rules:
- Each sword at the render time: aim = its `SwordTarget`'s target interpolated by `alpha` (frozen ones stay put).
  `SwordAt(volley, sword, renderTick, aim)` gives its position. Instances have `previous == current` and a rotation
  from the heading (the sword sprite points up), so the renderer's own interpolation is a no-op.
- Per sword: one `halo` (additive, in `EffectColour(element)` × `HALO_INTENSITY` × `shown`), one `sword` (normal,
  its own steel, opacity `shown`), and `TRAIL_SEGMENTS` ribbon quads in the element's colour, sampled back to
  `renderTick − TRAIL_TICKS` (none before launch), tapering. Fixed slots per sword, filled on `jobs` in chunks of 256.
  A hidden sword writes a zero-colour quad, so output order and bytes don't depend on the worker count.
- The ability picks only the sprite: `sword` for the swords, `fire_bolt` for the bolt. The colour of halos, trails
  and the bolt itself comes from the volley's element (the bolt's sprite is white, tinted).
- Order in the world pass: snapshot sprites, outlines, ribbons, overlay halos, overlay swords, then text.
- A sword hitting a living target vanishes at arrival. A plunging one stays and fades over `fadeTicks`.

- [ ] **Step 1: Write the failing tests** (`volley_draw_tests.cpp`, with the test manifest):
  - `each sword is drawn where the shared path puts it at the render time`: positions equal
    `SwordAt(..., renderTick, aim)` with `aim` the target interpolated by alpha (1e-4), at alpha 0.3 and 0.7.
  - `a sword not yet launched shows nothing, nor any trail`.
  - `the trail is sampled from the same path a little back in time`: its tail vertex pair straddles
    `SwordAt(renderTick − TRAIL_TICKS)`.
  - `the halos draw additive, before the swords`: batch order and pipelines.
  - `the same input gives the same bytes with one worker and with many`.
  - `a volley's swords follow a moving target between ticks`: target previous ≠ current; swords at arrival sit on the
    interpolated aim.
  - `a health bar's fill is as long as its health and takes the table's colours`: fill width = 40 × fraction;
    background and fill colours equal the table's, and a recoloured table changes them.
  - `halos and trails take their element's colour, not their ability's`: the swords in `DamageEffect`, the bolt in
    `BurnEffect`; recolouring `BurnEffect` changes the bolt and nothing else.
- [ ] **Step 2: Run them to see them fail.**
- [ ] **Step 3: Implement**, with the art: `sword` (about 10×40, steel with a pale edge, pointing up, foot at its
  centre), and in white so the element's colour tints them: `halo` (soft radial, additive), `fire_bolt` (a teardrop
  pointing up), `lance_strike` (Task 9's burst, made now with the rest), `solid` (8×8). All go in the arena scene. Run `pixi run project_n shaders` and
  `pixi run project_n atlas --placeholders`. Report overflows through `ReportOverflow` as `"overlay instances"` and
  `"ribbon quads"`.
- [ ] **Step 4: Reference picture.** `TEST_CASE("the flying swords mid-volley: the ring and the converging paths")`:
  the arena, a `Cast` of special 1 at a fixed point among the foxes at tick 120, captured at
  `LaunchTick`'s middle stagger plus `outTicks` (the ring) and at half the flight (converging). That's two pictures,
  `volley_ring` and `volley_converging`, at zoom 1. Run `reference --update`, **look at both** (do the swords curve and
  flow, with no straight spokes?), tune the shape in `abilities.yaml` and Task 6's control points if needed, and
  commit them with the arena's other pictures, which now carry health bars.
- [ ] **Step 5: Run** `pixi run project_n test`, `pixi run project_n lint`, `pixi run project_n reference`: PASS.
- [ ] **Step 6: Commit** `feat(gfx): draw flying swords with halos and trails at the render time`.

### Task 9: The poison lance and the temporary HUD

**Files:**
- Create: `src/app/hud.hpp`, `src/app/hud.cpp`, `tests/unit/app/hud_tests.cpp`
- Modify: `src/game/combat.{hpp,cpp}`, `src/game/arena.cpp`, `src/core/strings.hpp`, `assets/data/strings.yaml`,
  `src/gfx/renderer.{hpp,cpp}`, `src/app/app.{hpp,cpp}`, `src/main.cpp` (strings to `App`),
  `tests/unit/game/combat_tests.cpp`, `tests/unit/data/strings_tests.cpp`, `tests/reference/reference_tests.cpp`

**Interfaces:**
- Produces (combat): the lance's hits and its flash. At the cast, every living enemy within `radius` of the point
  takes `damage` as a `Poison` hit and gains `poison` (same rules as burn, Task 7). Then the use ends after the
  afflictions. The lance's burst is state the simulation reports, not a sprite it colours:
  `struct Flash { core::Vec2 point; u64 startTick; core::HitKind element; }` in `core/frame.hpp`, saved by `Combat`
  for `FLASH_TICKS = 12` ticks, and published as `Snapshot::flashes`. `gfx::DrawFlashes(std::span<const core::Flash>, f64 renderTick, const core::Manifest&, const core::UiColours&, VolleyDrawing&)`
  draws `lance_strike` additive at the render time, scaling up and fading, in `EffectColour(element)` ×
  `gfx::FLASH_INTENSITY = 1.0F` (chunk 11's reduce flashing lowers it).
- Produces (strings): `StringId` entries and `strings.yaml` keys, both languages:
  `health` ("Health {value}/{max}" / "生命 {value}/{max}"), `qi` ("Qi {value}/{max}" / "气 {value}/{max}"), `not_enough_qi` ("Not enough Qi" / "气不足"), `cooldown` ("{value}s" / "{value}秒"),
  `slot_empty` ("—" / "—"), `key_left_click` ("Left click" / "左键"), `key_right_click` ("Right click" / "右键"),
  `key_space` ("Space" / "空格"), `fire_bolt` ("Fire Bolt" / "火弹"), `poison_lance` ("Poison Lance" / "毒枪"),
  `dash` ("Dash" / "疾步"), `flying_swords` ("Flying Swords" / "飞剑").
- Produces (`gfx/renderer.hpp`): `struct HudLine { std::string_view text; core::Vec2 topLeft; u32 colour; }`
  (target pixels; drawn last, at `HUD_PIXELS = 18` × text scale);
  `struct HudBar { core::Vec2 topLeft; core::Vec2 size; f32 fraction; u32 fill; u32 background; }` (target pixels:
  the `solid` sprite twice, drawn by the overlay sprite pipeline with a pixels-to-clip transform, under the HUD's text);
  `struct Hud { std::span<const HudLine> lines; std::span<const HudBar> bars; }`;
  `Renderer::Draw(snapshot, view, jobs, extra = {}, hud = {})` and `Capture(..., extra = {}, hud = {})` gain the `Hud`.
- Produces (`app/hud.hpp`, pure): `class Hud { Hud(std::shared_ptr<const core::Strings>, core::Language, const core::UiColours&); void SetColours(const core::UiColours&); void Play(const core::Event&, f64 renderTick); void Build(const core::CasterView&, f64 renderTick, u32 height, std::vector<gfx::HudLine>& lines, std::vector<gfx::HudBar>& bars); }`.
  At the bottom left, top to bottom (the user's choice, 2026-11-11): the player's **health bar** (`HealthBar` on
  `BarBackground`) with "Health {value}/{max}" on it, the **Qi bar** (`QiBar` on `BarBackground`) with
  "Qi {value}/{max}" on it, then one line per slot in order: the key ("Left click", "Right click", "Space", "1"…"5"), the
  ability's name or "—", and the seconds left, rounded up, while cooling down. Bars are 240×18 px × text scale, 16 px
  in from the edges; text is in `HudText`. A slot cooling down is drawn in `HudCooling` *and* shows its seconds. For
  `NOT_ENOUGH_QI_SECONDS = 1.0` after a `NotEnoughQi`, the Qi bar's text reads "Not enough Qi" in `HudWarning`. Every
  value shows as a number as well as a length. Every colour comes from the table; none is a constant here.

- [ ] **Step 1: Write the failing tests:**
  - combat: `the lance strikes every enemy in its area with poison and ends after its last tick`;
    `the lance's flash survives a save`.
  - combat: `the lance's flash carries its element and fades after its ticks` (published `flashes`, none after
    `FLASH_TICKS`).
  - hud: `the HUD shows the health bar, the Qi bar and every slot in order`; `each bar's fill is as long as its
    value, with the value written on it`; `a slot cooling down shows its seconds left, rounded up`;
    `too little Qi shows the words for a second`; `the HUD reads in Chinese with the language set`;
    `the HUD's lines and bars take the table's colours, and a new table recolours them at once`.
  - strings: the new keys parse in both languages (strings_tests' existing "every string in both languages" covers
    it once the enum grows; add `the HUD strings fill their value`).
  - reference: `TEST_CASE("the HUD at zoom 1, one slot cooling down")`.
- [ ] **Step 2: Run them to see them fail.**
- [ ] **Step 3: Implement.** The renderer lays HUD lines out through the glyph cache (`TextLayout::Lay`, left to
  right) at their pixel positions, after every other text. The strings join `Prewarm` through the table, as all
  strings do. `App` keeps a `Hud` and a `std::vector<HudLine>` reused each frame, plays `NotEnoughQi` events into it,
  and passes the lines to `Draw`. `Options::strings` carries the table from `main`.
- [ ] **Step 4: Run** `pixi run project_n test`, `pixi run project_n lint`, `pixi run project_n reference --update`,
  look at the pictures, then run `reference`: PASS.
- [ ] **Step 5: Commit** `feat(app): strike with the poison lance and show a temporary HUD`.

### Task 9b: The swords form in waves of wide, scattered rings

Added 2026-11-11 at the user's request, after seeing the single ring:

> "Can we make the sword spread much wider so the 1000 swords don't just end up looking like a circle? Much wider, much
> more spread, much more randomness in the creation? Make the creation semi-sequenced with rings of swords one after each
> other"

The spec's "burst out of the player in a ring" becomes **a sequence of rings**. Record this as the user's decision in the
spec's Outcome.

**Files:**
- Modify: `src/core/volley.{hpp,cpp}`, `src/core/abilities.hpp` (only if a field moves), `src/data/abilities.cpp`,
  `assets/data/abilities.yaml`, `src/game/arena.cpp` (`BUILT_IN_DATA`), `tests/unit/core/volley_tests.cpp`,
  `tests/unit/data/abilities_tests.cpp`, `tests/fuzz/corpus/yaml/abilities`, `tests/reference/` (the volley pictures),
  `docs/superpowers/specs/2026-11-10-phase6-chunk4-abilities-design.md` (Goal 4's wording)

**Interfaces:**
- `VolleyShape` gains these fields, in `Fields()`, `Valid()` and the YAML under `swords:`:
  - `u32 waves`: how many rings, at least 1. A volley of 1 (the bolt) has 1.
  - `u32 waveGapTicks`: the delay between one ring's launch and the next's.
  - `f32 ringStep`: how much further out each ring forms than the one before, in world units.
  - `f32 scatter`: the fraction of a sword's ring radius it may stray in and out (0 = a line, 0.3 = a band of ±30%).
  - `f32 angleJitter`: radians a sword's place on its ring may stray from even spacing.
  - `f32 sweepJitter`: the fraction by which each sword's flight bend may vary (±).
  The existing `staggerTicks` becomes the launch spread **within** a wave, and `ringRadius` the **first** ring's radius.
- Shipped swords, starting numbers to tune by eye:
  - waves 5, wave_gap 0.12 s, ring_radius 160, ring_step 80 (the rings reach about 480);
  - scatter 0.3, angle_jitter 0.5 rad, sweep_jitter 0.6;
  - stagger 0.15 s (within a wave).
  The bolt's shape keeps 1 wave and zero jitter.
- `count` swords split over the waves as evenly as possible, inner ring first. A sword's wave, ring radius, place,
  launch and bend are all drawn from `volley.seed` with documented local purposes. They stay stateless, deterministic
  and allocation-free.
- Every one of Task 6's rules holds unchanged: the burst spirals out (no straight spokes), continuous position and heading
  into the ring turn and the flight, the commit window (`CommitTicksFor(radius)` uses the tag radius, unchanged), and the
  arrival bit-exact on the struck target.

- [ ] **Step 1: Write the failing tests:**
  - `the swords form in waves, inner first`: every sword's wave is in `[0, waves)`; the waves' sizes differ by at most 1;
    each wave's earliest launch is `waveGapTicks` after the previous wave's.
  - `each wave forms further out, in a scattered band`: at the end of its out phase, each sword's distance from the origin
    lies within `ringRadius + wave × ringStep`, ± `scatter` of that radius. A wave's mean radius rises with its index.
    Across the volley, the distances' standard deviation is at least a set fraction of `ringStep` (not one circle).
  - `places on a ring are jittered, not evenly spaced`: the angular gaps within a wave vary, but each wave still covers the
    circle (largest gap under a bound).
  - `flights bend by different amounts`: the swords' bends vary by at least a set spread.
  - The existing smoothness, ring-turn, retarget, commit-window and arrival tests pass **at their current bounds**.
    Re-anchor any test written against a single ring to its sword's wave, and don't loosen it.
  - data: the new keys parse, are refused when out of range (waves 0, negative or non-finite numbers, scatter ≥ 1), and
    a missing key is refused like the others. The shipped table still equals `BuiltInData()`.
- [ ] **Step 2: Run them to see them fail.**
- [ ] **Step 3: Implement** in core/volley.cpp (the burst and the flight's bend) and the data reader.
- [ ] **Step 4: Pictures.** Run `pixi run project_n reference --update` and look at `volley_ring` (now the rings forming:
  capture it when the third wave is forming, so earlier rings show in flight), `volley_converging` and `volley_plunge`. Do
  the rings read as rings one after another, wide and loose, not one disc? Tune the numbers by eye, keeping
  abilities.yaml and `BUILT_IN_DATA` in step. Measure `DrawVolleys` again (mean and worst, 1 worker and all) against
  the last numbers (201/469 µs, 328/758 µs).
- [ ] **Step 5: Run** `pixi run project_n test`, `pixi run project_n lint`, `pixi run project_n reference`: PASS.
- [ ] **Step 6: Commit** `feat(core): form the flying swords in waves of wide, scattered rings`, plus the spec's Goal 4
  wording as `docs(design): ...`.

### Task 10: A recorded fight, the measurements, the docs

**Files:**
- Modify: `tests/unit/game/replay_tests.cpp`, `src/app/app.{hpp,cpp}`, `src/main.cpp` (`--cast-swords`),
  `src/app/cast.{hpp,cpp}` (latency), `tests/unit/app/cast_tests.cpp`, `tests/fuzz/corpus/save/seed`,
  `src/game/arena.hpp` (its feature list), `src/AGENTS.md`, the spec (Outcome, status)

**Interfaces:**
- Produces (`app/cast.hpp`): `class CastLatency { void Pressed(u64 nowNs); std::optional<u64> Drawn(std::span<const core::VolleyView> volleys, u64 nowNs); }`
  The first frame that draws a volley not seen before gives the time since the oldest press waiting, which App logs
  at debug level, one line per cast: `swords first drawn {} ms after the press`.
- Produces: `--cast-swords` (like `--burst`): every 4 seconds `App` sends a `Cast` of special 1 at the followed
  player's foot + (300, 0), so a profile sees a volley without key presses.

- [ ] **Step 1: Write the failing tests:**
  - replay: `a recorded fight replays with every hash matching`: the arena through the real `Logic`. Every ability is
    cast (fire bolt ×3, lance ×2, dash, swords), enough to kill and see a respawn (fox health lowered by data), with a
    save mid-volley in it. It plays back in a fresh game with every hash matching, the last on the final tick.
  - cast: `the latency runs from the press to the first frame drawing its volley`, and `a volley drawn twice counts once`.
- [ ] **Step 2: Run them to see them fail; implement; run them to pass.** Regenerate the save fuzz seed (format 3).
- [ ] **Step 3: Measure** (Tracy, releasedbg, 2 workers as the phase bar says):
  `pixi run project_n profile --seconds 30 -- --cast-swords`, then the same without the flag, for a baseline. Record in
  the spec's Outcome: main-thread frame time p50, p99 and worst, with and without volleys; the `DrawVolleys`, ribbon
  upload and logic tick zones at a volley's peak; and the input-to-first-sword latency from the log, over 10 presses.
  A frame over budget during a volley is a finding to fix before the PR.
- [ ] **Step 4: Docs.** The arena's feature list in `arena.hpp` (abilities, health, the dissolve, respawns, the HUD).
  `src/AGENTS.md`: the overlay and ribbons in the frame's order, and the snapshot's volleys. The spec: **Outcome**
  (what was built, the plan ledger's rulings with their reasons, the measurements); status "implemented". Mark chunk 4
  done in the phase spec's table.
- [ ] **Step 5: Run** `pixi run project_n check`: PASS.
- [ ] **Step 6: Commits** `test(game): replay a recorded fight with every ability`,
  `feat(app): cast the swords on a timer for profiling and log their latency`,
  `docs(design): record what chunk 4 built`.

### Task 10b: Every tuning value as data

Added 2026-11-11 at the user's request: "Maybe we could plan in a debug UI with imgui in the future and all these values
could be tweakable so we can tune it visually. Just ensure all the values like the distance, speed, range, shape,
convergence, everything etc is data-driven and able to be tuned". The ImGui UI is a later plan (backlog). This task makes
every tuning value data, in two tables, so that UI, and hand edits with hot reload today, can tune them:

1. **`assets/data/abilities.yaml`, game data** (read into `core::GameData`, hashed, so saves and replays stay exact):
   everything that changes what happens or how a sword moves.
   - Each projectile ability's motion, added to `VolleyShape` (saved with the volley, so a volley in flight keeps the
     shape it was cast with) and read under its `swords:`/shape block:
     - the spiral's turn (`SPIRAL_TURN`), the burst and arrival speeds (`BURST_SPEED`, `ARRIVAL_SPEED`);
     - when the ring turn starts (`TURN_START`), and the bend's shape (`SHORT_BEND`, `SIDE_ARM`);
     - the aim scatter (`AIM_SCATTER`);
     - the retarget pace (`RETARGET_TICKS`, `RETARGET_SPEED`; `CommitTicksFor` takes them, and `RETARGET_PEAK` follows
       from them).
   - A `combat:` section, into a new `core::CombatRules` in `GameData`:
     - `dying` (`DYING_TICKS`), `respawn` (`RESPAWN_TICKS`), `arrive` (`ARRIVE_TICKS`), `affliction_period`
       (`AFFLICTION_PERIOD`);
     - the lance burst's length (`FLASH_TICKS`);
     - the aura (`AURA_PERIOD`, `AURA_REACH`, `AURA_DAMAGE_MIN/MAX`, `AURA_HEALING_MIN/MAX`).
     Times are in seconds in the YAML, ticks in core, as elsewhere.
2. **`assets/data/effects.yaml`, presentation** (read by the `data` module into a new `core::Effects`; not hashed;
   reloaded on the main thread by `App`'s existing data watch, and handed to the renderer and `app::Hud` with a setter,
   so a change shows the next frame with no new timeline):
   - per ability, by name: sword, halo and core sizes (`SWORD_SCALE`, `HALO_SCALE`, `CORE_SCALE`, `BOLT_CORE_SCALE`);
     the trail's length, width and taper (`TRAIL_TICKS`, `TRAIL_WIDTH`, `TRAIL_TAPER`); the glow's intensity and the
     core's opacity (`HALO_INTENSITY`, `CORE_OPACITY`, which chunk 11's accessibility settings scale); the flash's
     growth and intensity (`FLASH_START_SCALE`, `FLASH_END_SCALE`, `FLASH_INTENSITY`);
   - health bars: `HEALTH_BAR_RISE`, the bar's and fill's sizes;
   - the HUD: `HUD_MARGIN`, `HUD_BAR_WIDTH`, `HUD_BAR_HEIGHT`, `HUD_LINE_HEIGHT`, `BAR_GAP`, `BAR_TEXT_INSET`,
     `BAR_TEXT_RISE`, `NOT_ENOUGH_QI_SECONDS`, `HUD_COOLING_INK_OPACITY`.

Stays in code, and why (documented where each is declared): engine limits (`MAX_VOLLEY_COUNT`, `MAX_OVERLAY_INSTANCES`,
`MAX_RIBBON_QUADS`, `SWORD_INSTANCES`, `TRAIL_SEGMENTS` which sizes buffers, `MIN_COMMIT_TICKS` as a floor), maths
(`THIRD`, `HALF`, `HERMITE_*`, `FULL_TURN`), formats and versions, random purposes, `CAST_SOURCE`, the debug burst, and the
arena's layout (scene data, chunk 7).

**Rules:**
- Every value keeps its current number as the shipped default, so no behaviour, picture or hash changes beyond what
  moving a value into `GameData` does to the hash itself. `BuiltInData()` and a new `game::BuiltInEffects()` (or
  wherever the defaults live) mirror the YAML, with a test each, as the creatures and abilities do.
- Each reader refuses out-of-range values with the line, like the existing readers (a scale ≤ 0, a negative time, a
  speed of 0, `TURN_START` outside [0, 1)), and warns on unknown keys. The yaml fuzz target also parses `effects.yaml`,
  with its seed.
- The volley's smoothness, retarget, commit and arrival tests run against the shipped shape read from data, and every
  bound stays at its value.
- Hot reload: a test that a changed effects table reaches the renderer's and the HUD's next frame without touching the
  simulation. Game data already reloads through `ReloadData`.
- Docs: the data tables' comments say each value's unit and effect. src/AGENTS.md (and data's docs) name effects.yaml.

- [ ] **Step 1:** the failing tests (readers and refusals, the defaults mirroring the YAML, the reload path).
- [ ] **Step 2:** move the values: core types, data readers, call sites. Run `test`, `lint`, and `reference` with no
  `--update` (nothing moves).
- [ ] **Step 3:** the spec's Outcome gains this task (the user's request, the two tables, what stays in code and why);
  then `pixi run project_n check`.
- [ ] **Step 4: Commits** `feat(data): read the abilities' motion and the combat rules from data`,
  `feat(data): read the effects' looks and the HUD's layout from data`, `docs(design): ...`.

### Task 10c: The rings appear as still circles, o then O

Added 2026-11-11 at the user's request, after seeing the waves: "Can we have it more of a static circle like o O than a
spiral?" It runs **before Task 10b**, since it removes the spiral whose constants 10b would move into data. It replaces
Ruling 41 (the spiral burst), whose only purpose was to avoid straight spokes. The user's decision is recorded in the
spec's Outcome.

**The user's reference** (2026-11-11, an image, "My vision was more like this … Not the spiral we're getting"): the
caster at the centre of a sunburst of swords, each pointing straight out, hilts on the circle, evenly spaced and packed
side by side, standing still as a formation. So the rings are **regular**: no scatter, no angle jitter. Each ring draws
itself in one quick clockwise sweep, and the randomness moves to the departure (a random delay before each sword leaves
its ring) and the flights' bends.

**The new formation** (src/core/volley.cpp, one stateless rule for logic and renderer):
- **No burst from the caster:** a sword **appears at its place on its ring**, fading in (`SwordPose::shown` 0→1 over
  `outTicks`, which now means "appear") without moving, pointing outward (radially), so the ring forms with no spokes,
  no swirl and no trail.
- **Hold:** it stays still on the ring for a new `VolleyShape::holdTicks` (shipped 0.3 s). Then, over the turn window
  (the existing ring turn, planned from the fixed point), it turns from outward to its flight's departure heading, and
  sets off from rest. The flight, waves, bends, retarget chain, commit window and arrival rules are unchanged.
- **Regular rings, as in the reference:** the shipped `scatter` 0.25 → 0 and `angle_jitter` 0.5 → 0, so each ring is a
  perfect, evenly spaced sunburst. Within a wave, sword k of n appears at `stagger × k / n` going clockwise (a sweep;
  `stagger` 0.1 s), not at a random time.
- **Random departure:** a new `VolleyShape::departJitterTicks` (shipped 0.25 s) adds a per-sword random delay, drawn
  from `volley.seed` with a new documented purpose, between the end of the hold and the start of the turn. A ring
  dissolves into flight unevenly while it forms evenly. The flights keep their random bends. Waves stay 0.2 s apart,
  inner ring first, at ring radius 160 + 100 per wave.
- `LaunchTick` is when a sword appears; `ArrivalTick` adds `outTicks + holdTicks + its depart delay + flightTicks`.
- **Arrival and commit:** the arrival tick moves later by the hold, so the swords still strike in step with the
  pictures, and `commitTicks` is unchanged.
- **Data:** `hold` is read under `swords:`, refused when negative, and mirrored in `BUILT_IN_DATA` and the yaml seed.
  The bolt has hold 0 and appears at the caster as before. A volley of 1 with ring radius 0 keeps its straight eased
  flight.

**Tests** (RED first; every smoothness bound at its value):
- `a sword appears at its ring place and holds still`: from its launch until the turn, its position is exactly its ring
  point. Its `shown` rises over `outTicks`, and its heading points outward.
- `the rings are regular sunbursts`: with the shipped shape, each wave's swords lie exactly on their wave's radius, evenly
  spaced (every angular gap equal within 1e-6), each pointing outward during the hold.
- `a ring draws itself clockwise`: within a wave, appearance times rise with the sword's angle.
- `swords leave their ring at different times`: the depart delays spread over `departJitterTicks`.
- `a wave's ring forms before the next`: each wave's first appearance is `waveGapTicks` after the previous wave's.
- The existing smoothness tests now sample from appearance through the hold, the turn, the flight and the fade:
  position is continuous (no motion during the hold), and heading is continuous through the turn.
- Remove the spiral test, which tested Ruling 41's spiral; the user's decision replaces it.
- data: `hold` and `depart_jitter` parse and are refused when negative or missing; the shipped table equals `BuiltInData()`.

**Pictures:** retake `volley_ring` while the third wave appears, so two still rings and a third appearing show,
o then O. Retake `volley_converging` and `volley_plunge`. Look at each: do the rings read as still circles, small to
large? Measure `DrawVolleys` again.

**Commits:** `feat(core): form the swords' rings as still circles that hold, then fly`, plus the spec's wording
(Goal 4 and decision 11) as `docs(design): ...`.

### Task 10d: Small tests, and `check` back to minutes

Added 2026-11-12 at the user's direction. Task 10's `check` took about 30 minutes, about 10× the faster-check work's
3–4.5 minutes, and the volley suite holds about 1.9 million assertions. The user: "We should never have 1.9 million
assertions in one suite, that is ludicrous. Just 20 samples should be enough, its just a unit test. We have profiling for
profiling." Runs after Task 10c (which writes its volley tests small from the start) and before Task 10b.

- **Measure first:** read Task 10's `check` log for each step's time (which presets, which suites) and record it.
- **Shrink every test this branch added or grew that sweeps:**
  - volley_tests.cpp: smoothness, ring turn, retarget, spread, the shortest chord;
  - volley_draw_tests.cpp;
  - combat_tests.cpp and save_tests.cpp, wherever they loop over every sword or tick.
  Use about 20 representative samples per check, and assert a loop's worst value once instead of one `CHECK` per
  sample. Each test keeps the behaviour it pins: the same property and bound, on chosen cases. Name why each sample is
  there (first, last, wave boundary, shortest chord, a death mid-swing).
- **The rule in tests/AGENTS.md**, one line: unit tests use a handful of representative samples, never sweeps; timing is
  profiling's (`profile`, `headless_bench`), breadth is fuzzing's.
- **Re-measure:** `pixi run project_n check` with each step's time. Target: back near the faster-check numbers. Record
  before and after in the spec's Outcome. If something other than tests dominates (builds, Wine, the reference
  pictures), say what, and it becomes its own plan after this branch.
- **Commits:** `test(tests): sample the volley's behaviour, not every sword and tick`, `docs(agents): keep unit tests to a
  handful of samples`.

### Task 10e: A deeper damage colour

Added 2026-11-12 at the user's decision ("Yes, make it deeper") on the open question in the spec's Outcome: the damage
element's default colour, `#CFE3FF`, is near-white, so the swords' tinted cores add only a faint blue cast.

- The damage effect's default in `core::UiColours` (`src/core/ui_colours.hpp`) becomes `#6FA8FF`, a clear azure in the
  same hue family at about twice the saturation, converted to the table's packed form exactly as its neighbours are.
  Every place that lists the defaults (the header's comment, the spec, this plan's Task 2, any test that names the old
  value) says the new one; the Outcome's open question becomes a decision.
- `pixi run project_n reference --update`: look at every changed picture (look-at-the-game skill) and check that the
  swords' cores and anything else tinted by the damage element read clearly blue against the arena, with the halo still
  glowing and no text or HUD ink that uses the damage colour losing contrast (accessibility first). If text drawn in the
  damage colour drops below the contrast the HUD's other inks keep, stop and report it rather than choosing another
  colour.
- `pixi run project_n test`, `lint --range <BASE>..HEAD`, then the full `pixi run project_n check` (it also covers Task
  10b's fix round, Ruling 71).
- **Commit:** one, with the pictures; `feat(<scope>): deepen the damage effect's colour` (scope from `SCOPES`).
