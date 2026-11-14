# Phase 6 chunk 5, first part: scenes, implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A clean, extendable scene layer in `game` (a host and one class per scene) and a scene-change sequence in
`core::Logic` (requested, leaving, swap, loading, entering, running) that transitions and the loading screen will draw
into, with nothing a player or a test can see changing.

**Architecture:** `core::Simulation` gains three calls (the current scene, the scene asked for, enter it); `core::Logic`
runs the sequence, waiting without ticks through leaving, loading and entering as it already waits for loads, and
publishes the stage in `Snapshot::sceneChange`. The main thread answers leaving and entering at once through one helper.
In `game`, `Arena` becomes `Host` (state, world, calendar, player, combat, saves) and each scene a stateless class in
`src/game/scenes/` behind `game::Scene`, listed in `game/scenes.hpp`.

**Tech Stack:** C++23, EnTT through `core/ecs.hpp`, doctest, XMake through `project_n`, all from Pixi.

**Spec:** `docs/superpowers/specs/2026-11-14-phase6-chunk5-scenes-design.md` (read it first: its History, Decisions and
Calls are this plan's ground). Design docs 0002.3 §6 and 0002.3.1 describe the loading protocol the sequence extends.

## Global Constraints

- Nothing visible or recorded moves: reference pictures (`pixi run project_n reference`), replay test hashes, save bytes
  (format and versions: `ARENA_FORMAT`, `ACTIVE_VERSION`, `WORLD_VERSION`, `SAVE_FORMAT` unchanged), events and their
  ticks, debug keys and flags.
- A save stores the scene as one byte (today's `game::Scene` enum value); it stays one byte, the scene's key.
- Leaving, loading and entering run no tick, count no counter and record nothing; the main thread's reports are never
  recorded (as `SceneResident`).
- Never delete, skip or loosen a test; re-pointing a test to the new calls (an `EnterScene()` after a `Load`, a report
  sent in a harness) is allowed and must keep its assertions.
- `game` links only `core`; simulation code stays deterministic (`core/math.hpp`, `core/random.hpp`).
- src/AGENTS.md rules apply (sized types, `pch.hpp`, Doxygen on every public declaration, no `switch` over a whole enum,
  `PN_PROFILE_ZONE` on per-frame functions, log outcomes only).
- Gates: an edit round runs `pixi run project_n test --suite <s> [--test <t>]`; a task ends with
  `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD`.

## Review Focus

1. A scene change asked for while paused (F2 then Pause before the leaving report): the sequence still completes and
   the game stays paused after it. Task 2 adds the test.
2. A second F2 (or a Load) pressed during leaving, loading or entering: dropped with a debug line, as loading drops
   actions today; never a second change stacked on the first. Task 2 adds the test.
3. A stale report (a `SceneLeft` or `SceneEntered` naming an earlier change) arriving in a later change's stage:
   ignored. Task 2 adds the test.
4. Hot-reloaded data (`ReloadData`) arriving mid-sequence: taken, as loading takes it today, and the sequence goes on.
   Task 2 adds the test.
5. A save loaded while a replay records, into another scene: the recording finishes on the load's tick, and the swap
   later doesn't record anything. Task 3 adds the test.

## Rulings made while planning

- **The camera kind is published, not yet read:** `Snapshot::camera` carries each scene's kind (Task 4), tested in
  `game`; `app` keeps inferring from `Snapshot::follow` until 5a's free camera, its first reader, so this part leaves
  `app`'s camera code untouched (the spec's call that "app stops inferring" lands with 5a).
- **The leave step is the game's, on the requesting tick:** `CycleScene` ends open uses (`Combat::EndUses()`) and records
  the request; a Load has no leave step (as today: a load emits no UseEnded events).
- **Scene changes are counted** (`SceneChange::change`, 1 for the start); each report names its change, so a stale one
  is ignored.

## Waves

Each task's implementer works in its own worktree branched from the feature branch's head, rebased onto it as its review
passes (AGENTS.md: Working with superpowers). One task is dispatched per session (AGENTS.md).

| Wave | Tasks | What each needs |
|---|---|---|
| A | 1 (the swap moves out of the tick, full) | nothing |
| B | 2 (leaving and entering, full), 4 (scene classes, full) | Task 1's `Simulation` calls; no shared file (Task 2: core, app, the harnesses; Task 4: `src/game/`, `tests/unit/game/arena_tests.cpp`) |
| C | 3 (loads and replays through the sequence, full) | Tasks 2 and 4 (it edits the host's `Load` that Task 4 moved) |
| D | 5 (`Arena` → `Host`, the enum → keys, light) | everything: it renames across every file |

---

### Task 1: the swap moves out of the tick

**review: full** (determinism, the logic thread)

**Files:**
- Modify: `src/core/frame.hpp` (new types beside `SceneId`), `src/core/logic.hpp` (`Simulation`), `src/core/logic.cpp`
  (`RunTick`), `src/game/arena.hpp`, `src/game/arena.cpp` (`Tick`'s `CycleScene`, the three new overrides)
- Test: `tests/unit/core/logic_tests.cpp` (its fake simulation implements the new calls), `tests/unit/game/arena_tests.cpp`

**Interfaces:**
- Produces, in `core/frame.hpp`:
  - `enum class SceneKey : u16 {};` the game's own name for a scene (its place in the game's list), passed through unread
  - `struct SceneFacts { SceneKey key{}; SceneId sprites{}; bool combat = false; }` with defaulted `operator==`
  - `enum class CameraKind : u8 { Follow, Pan };` and `Snapshot::camera` (default `Pan`), which `Logic`'s publish
    resets with the other fields; `Arena::Publish` sets `Follow` when it has a player (Task 4 moves this to the scene's
    traits). Here so Task 4 needn't touch `core/frame.hpp`, which Task 2 edits in the same wave.
- Produces, in `core::Simulation` (pure virtual):
  - `[[nodiscard]] virtual SceneFacts Current() const = 0;` (its `sprites` is what `Scene()` returns; `Scene()` stays)
  - `[[nodiscard]] virtual std::optional<SceneFacts> Requested() const = 0;` the scene its last tick asked to change to,
    until `EnterScene()`
  - `virtual void EnterScene() = 0;` makes the requested scene current (builds it); asserts one was requested
- Produces, in `game::Arena`: the three overrides; `Current().key` is the `game::Scene` value as a `SceneKey`

- [ ] **Step 1: Write the failing tests**
  - `logic_tests.cpp`, "a scene asked for is entered after the tick that asks": the fake's tick sets a request; after
    `RunBurst`, `EnterScene` was called once, after the fake's `Tick` and before `Publish`; the burst ended after that
    tick (later ticks of the burst didn't run); the snapshot names the new scene's sprites.
  - `arena_tests.cpp`, "CycleScene asks for the next scene and EnterScene builds it": after a tick with `CycleScene`,
    `Requested()` holds the next scene's facts (stress after arena, its `combat` false) and `Current()` is still the
    arena; open uses ended on that tick (a UseEnded event, as the existing mid-volley case checks); after
    `EnterScene()`, `Current()` is the stress scene and `Requested()` is empty.
  - `arena_tests.cpp`, "the snapshot names the camera": the arena publishes `CameraKind::Follow`, the painted scene
    `CameraKind::Pan`.
- [ ] **Step 2: Run them to see them fail**
  `pixi run project_n test --suite core_tests --test logic` and `pixi run project_n test --suite game_tests --test arena`:
  compile errors (no `Requested`, `EnterScene`).
- [ ] **Step 3: Implement.** `Arena::Tick` on `CycleScene`: `EndUses`, record the next scene, return (no `Build`).
  `EnterScene()` builds it. `Logic::RunTick`: after the simulation's tick, if `Requested()` holds a scene, call
  `EnterScene()` at once (Task 2 puts the stages between). The hash a replay checks at that tick therefore still sees the
  new scene, as today.
- [ ] **Step 4: Run them to see them pass,** then the whole of `core_tests` and `game_tests` (replays, saves, the
  mid-volley scene change: all unchanged).
- [ ] **Step 5: Gate and commit:** `pixi run project_n test`, `pixi run project_n lint --range <base>..HEAD`;
  `refactor(core): enter a scene after the tick that asks for it`.

### Task 2: leaving and entering

**review: full** (the logic thread, replays)

**Files:**
- Create: `src/core/scene_reports.hpp`, `src/core/scene_reports.cpp` (the main thread's answers)
- Modify: `src/core/frame.hpp` (`SceneStage`, `SceneChange`, `Snapshot::sceneChange`, two action kinds,
  `Action::change`), `src/core/logic.hpp`, `src/core/logic.cpp` (the sequence; `Wait` generalised), `src/app/app.hpp`,
  `src/app/app.cpp` (sends the reports), `docs/design/0002.3.1-manifest-and-scene-loading.md` (an "as built" note on
  the sequence), `src/AGENTS.md` (the scene loading sentence)
- Test: `tests/unit/core/logic_tests.cpp`, `tests/unit/core/scene_reports_tests.cpp` (new),
  `tests/unit/core/replay_tests.cpp`, `tests/unit/game/replay_tests.cpp` (its session), `tests/unit/app/game_tests.cpp`

**Interfaces:**
- Consumes: Task 1's `SceneKey`, `SceneFacts`, `Simulation::Requested()`, `EnterScene()`.
- Produces, in `core/frame.hpp`:
  - `enum class SceneStage : u8 { Running, Leaving, Loading, Entering };`
  - `struct SceneChange { SceneStage stage = SceneStage::Running; SceneFacts from{}; SceneFacts to{}; u64 change = 0; }`
    (while running, `from` and `to` are both the current scene; `change` counts changes, the start being 1)
  - `Snapshot::sceneChange` (a `SceneChange`), set by `Logic` on every publish
  - `ActionKind::SceneLeft`, `ActionKind::SceneEntered`, appended after `Cast` (`ACTION_KINDS` follows), and
    `u64 Action::change` naming the change they answer; the replay's archive never writes it (they're never recorded)
- Produces, in `core/scene_reports.hpp`: `class SceneReports` with
  `[[nodiscard]] std::optional<Action> Answer(const SceneChange& change);` the `SceneLeft` or `SceneEntered` to send for
  a snapshot's change, once per stage of each change (none while running or loading, none twice)

- [ ] **Step 1: Write the failing tests** (`logic_tests.cpp`, with the fake simulation):
  - "a scene change leaves, swaps, loads and enters without a tick": the requesting burst publishes `Leaving` (from the
    old scene, to the new one, change 2) and the old scene's sprites; bursts until a matching `SceneLeft` run no tick and
    move no counter; the `SceneLeft` burst calls `EnterScene` and publishes `Loading` (or `Entering` when the new
    sprites are resident already); `SceneResident` moves it to `Entering`; `SceneEntered` to `Running`, that burst
    ending on a stall; the counters are those of the requesting tick throughout.
  - "the start loads, then enters": the first bursts publish `Loading` (change 1, from = to = the start scene), then
    `Entering` once resident, then `Running` after `SceneEntered`.
  - "nothing is recorded while a scene changes": a replay recorded across a change with the reports sent at once and
    one recorded with them sent ten bursts late play back with the same hashes as each other.
  - Review Focus 1–4: "a change asked for while paused completes and stays paused"; "a second CycleScene during a
    change is dropped"; "a stale report is ignored" (a `SceneLeft` with change 2 during change 3's leaving moves
    nothing); "new game data mid-change is taken and the change goes on".
  - `scene_reports_tests.cpp`, "answers each stage once": `Leaving` change 2 → `SceneLeft{2}`, the same again → none,
    `Loading` → none, `Entering` → `SceneEntered{2}`, again → none, `Leaving` change 3 → `SceneLeft{3}`.
- [ ] **Step 2: Run them to see them fail:** `pixi run project_n test --suite core_tests --test logic`.
- [ ] **Step 3: Implement the sequence in `Logic`.** `m_change` holds the current `SceneChange` (start: `Loading`,
  change 1). After a tick, a `Requested()` scene starts `Leaving` (change + 1) and ends the burst. A burst whose stage
  isn't `Running`, or whose scene isn't resident, waits (today's `Wait`, now also taking `SceneLeft`/`SceneEntered`
  that name the current change and dropping other actions with its debug line). At a matching `SceneLeft`: `EnterScene()`,
  then `Loading` or `Entering`. Resident while `Loading`: `Entering`. A matching `SceneEntered`: `Running`, ending on a
  stall. The stage stays out of `Logic::Hash` and out of saves.
- [ ] **Step 4: Implement `SceneReports`** and send its answers from `App` each frame for the drawn snapshot (beside
  where `LoadScene` sends `SceneResident`). Re-point the harnesses that change scenes to answer through it: the logic
  tests' rig, `tests/unit/core/replay_tests.cpp`, the game replay tests' `Session`, `tests/unit/app/game_tests.cpp`
  (comments that count bursts after F2 are corrected; no assertion is weakened).
- [ ] **Step 5: Run** `pixi run project_n test --suite core_tests`, `--suite game_tests`, `--suite app_tests`, then
  `pixi run project_n reference` (the pictures are taken once a scene runs: unchanged).
- [ ] **Step 6: Docs:** 0002.3.1's as-built note (the stages, why no ticks, who answers) and src/AGENTS.md's scene
  loading sentence, in the same commit.
- [ ] **Step 7: Gate and commit:** `pixi run project_n test`, `pixi run project_n lint --range <base>..HEAD`;
  `refactor(core): run scene changes through leaving, loading and entering`.

### Task 3: loads and replays through the sequence

**review: full** (saves, replays, the logic thread)

**Files:**
- Modify: `src/core/logic.hpp`, `src/core/logic.cpp` (`Load`, `TogglePlayback`, `Restore`), the host's `Load` and
  `Requested`/`EnterScene` (`src/game/arena.cpp`, as Task 4 left it), `tests/fuzz/save.cpp` (enters what it loaded)
- Test: `tests/unit/core/logic_tests.cpp`, `tests/unit/game/save_tests.cpp`, `tests/unit/game/replay_tests.cpp`,
  `tests/unit/game/arena_tests.cpp`

**Interfaces:**
- Consumes: Task 2's sequence; Task 4's host.
- Changes `Simulation::Load`'s promise (signature unchanged): on success it **holds** the state, which `Requested()`
  names (the saved scene's facts) and `EnterScene()` makes current; on failure it changes nothing, holding nothing.
- `Logic` holds a sound load's counters (and a replay's paused and calendar state) until the swap, where they replace
  its own and the epoch moves.

- [ ] **Step 1: Write the failing tests:**
  - `logic_tests.cpp`, "a load is checked on its tick and restored at the swap": a broken save is refused on its tick,
    counting a frozen tick, starting no change; a sound one starts `Leaving`, the state, counters and epoch untouched
    until `SceneLeft`, then replaced, the epoch one more; into the scene shown, the stage goes from the swap to `Entering`
    (no loading).
  - `logic_tests.cpp`, Review Focus 5, "a load while recording finishes the recording on its tick": the replay file is
    written on the load's tick; nothing more is recorded at the swap.
  - `game/replay_tests.cpp`, "a replay starting in another scene plays back with the same hashes", answering the
    reports at once and late.
  - `save_tests.cpp`, existing round trips re-pointed: `Load` then `EnterScene()`, the same assertions; new: "Load holds
    and changes nothing until EnterScene" (the state hash before `EnterScene` is the old one).
- [ ] **Step 2: Run them to see them fail:** `pixi run project_n test --suite core_tests --test logic`,
  `--suite game_tests --test save`.
- [ ] **Step 3: Implement.** The host's `Load` parses into a held state (it already builds locals and moves them in on
  success; it keeps them instead), and `EnterScene()` takes it. `Logic::Load` and `TogglePlayback` read and check on
  the tick, hold the counters, start the change; the swap restores and moves the epoch; the recording finishes on the
  load's tick as now. A replay starts playing its input on the first running tick, as today after its restore.
- [ ] **Step 4: Run** the three suites, `pixi run project_n reference`, and the save fuzz seeds
  (`pixi run project_n test --suite game_tests` covers "the mid-fight save loads"; the save layout is unchanged, so the
  fight seed stays valid).
- [ ] **Step 5: Gate and commit:** `pixi run project_n test`, `pixi run project_n lint --range <base>..HEAD`;
  `refactor(core): load saves and replays through the scene change`.

### Task 4: scenes as classes

**review: full** (game logic; the one-off comparison guards it)

**Files:**
- Create: `src/game/scene.hpp`, `src/game/scene.cpp` (the interface and `SceneContext`), `src/game/scenes.hpp`,
  `src/game/scenes.cpp` (the list, `SceneNamed`, `SceneName`, `NextScene`, `CheckScenes`), `src/game/scenes/arena.hpp`,
  `src/game/scenes/arena.cpp` (the arena scene; its layout tables and the feature list now in the arena header's
  comment), `src/game/scenes/arena_layout.hpp`, `src/game/scenes/arena_layout.cpp` (the ground, props, cultivator,
  foxes and orbs the arena and stress share), `src/game/scenes/stress.{hpp,cpp}`, `src/game/scenes/painted.{hpp,cpp}`,
  `src/game/scenes/cells.{hpp,cpp}`
- Modify: `src/game/arena.hpp`, `src/game/arena.cpp` (the host keeps no scene's insides), `src/game/components.hpp`
  (`Orbit` and `OrbitPoint` move here), `xmake.lua` or `src/game/xmake.lua` if sources are listed by folder, `src/AGENTS.md`
  (the arena rule names `game/scenes/arena.hpp`)
- Test: `tests/unit/game/arena_tests.cpp`, `tests/unit/game/scenes_tests.cpp` (new)

**Interfaces:**
- Consumes: Task 1's `SceneKey`, `SceneFacts`.
- Produces, in `game/scene.hpp`:
  - `struct SceneTraits { std::string_view name; core::Rect view{}; core::Vec2 home{}; bool combat = false; bool creatures = false; bool demoGlows = false; core::CameraKind camera = core::CameraKind::Pan; u32 worldNpcs = 0; }`
    (`name` is also the manifest scene's; `view` what a publish culls to without a player; `home` where orbits return
    when the data stops them circling the player; `worldNpcs` how many world NPCs it asks for, unchanged from today)
  - `class SceneContext` (abstract; the host implements it): `State()`, `Manifest()`, `Data()`,
    `Spawn(core::Vec2 foot, const Look& look) -> core::Entity`, `SpawnFox(const Orbit&, core::Vec2 foot) -> core::Entity`,
    `SpawnPlayer(core::Vec2 foot) -> core::Entity` (the cultivator, made the player), `Player() -> core::StableId`,
    `Damage(core::StableId, i64 amount, u64 tick)`
  - `class Scene` (stateless; all state lives in the host's saved state): `Traits() const -> const SceneTraits&`,
    `Build(SceneContext&) const` (pure), and hooks defaulting to nothing: `Tick(SceneContext&, const core::TickCounters&, std::vector<core::Event>&) const`,
    `Respawn(SceneContext&, const core::TickCounters&, u32 index) const`, `Publish(const SceneContext&, core::Snapshot&) const`
- Consumes: Task 1's `core::CameraKind` and `Snapshot::camera`, now set by the host from the scene's traits.
- Produces, in `game/scenes.hpp`: `ARENA_SCENE`, `STRESS_SCENE`, `PAINTED_SCENE`, `CELLS_SCENE` (`core::SceneKey`
  constants 0–3), `Scenes() -> std::span<const Scene* const>` (F2's order; index = key), `SceneOf(core::SceneKey) -> const Scene&`,
  `SceneNamed(std::string_view) -> std::optional<core::SceneKey>`, `SceneName(core::SceneKey) -> std::string_view`,
  `NextScene(core::SceneKey) -> core::SceneKey` (wraps), `CheckScenes` (moved, same signature and messages)
- `game::Scene` the enum stays for now as the constructor's argument, mapped to keys; Task 5 removes it.

- [ ] **Step 1: Write the one-off comparison** (`arena_tests.cpp`, "each scene builds what the old Build built", in its
  own commit before the move): for each scene, every entity's components, stable IDs and the world domain, compared
  between `Build` and the scene classes' build (both present during the move). It is deleted with the old `Build` in
  Step 5's commit; the lasting proof is the existing suites and the reference pictures.
- [ ] **Step 2: Write the failing lasting tests** (`scenes_tests.cpp`): "names round-trip" (`SceneName(SceneNamed(n))`
  for every listed scene); "the list is F2's order and wraps" (`NextScene` from cells is arena); "every listed scene
  passes CheckScenes against the shipped manifest"; "each scene's camera reaches the snapshot" (arena and stress
  `Follow`, painted and cells `Pan`).
- [ ] **Step 3: Run them to see them fail:** `pixi run project_n test --suite game_tests --test scenes`.
- [ ] **Step 4: Move each scene into its class.** The arena's aura and showcase seals become its `Tick` and `Publish`
  hooks; fox respawns at the ground's edge its `Respawn`; demo glows a trait the host reads in its publish loop. The
  host's `Build` builds the common part (state, relationships, combat, selection), asks the scene to build, then spawns
  its `worldNpcs`. The host never compares a scene's identity (no `m_scene == ...` left). The save still writes the scene
  as one byte.
- [ ] **Step 5: Run** `pixi run project_n test --suite game_tests` with the comparison passing, then delete the old
  `Build` and the comparison; run the suite again and `pixi run project_n reference`.
- [ ] **Step 6: Gate and commit** (the comparison's commit, then the move): `pixi run project_n test`,
  `pixi run project_n lint --range <base>..HEAD`; `refactor(game): make each scene a class of its own`.

### Task 5: `Arena` becomes `Host`, and the enum goes

**review: light** (a mechanical rename across files; behaviour unchanged)

**Files:**
- Rename: `src/game/arena.{hpp,cpp}` → `src/game/host.{hpp,cpp}`, `tests/unit/game/arena_tests.cpp` →
  `tests/unit/game/host_tests.cpp`
- Modify: every user of `game::Arena` and `game::Scene` (`src/app/game.hpp`, `src/app/app.hpp`, `src/main.cpp`,
  `src/core/manifest.hpp`'s comment, `tests/bench/headless.cpp`, `tests/fuzz/save.cpp`,
  `tests/reference/reference_tests.cpp`, `tests/unit/app/*`, `tests/unit/data/manifest_tests.cpp`, `tests/unit/game/*`),
  `src/AGENTS.md`, `tests/AGENTS.md` (suite and file names), `tools/project_n.py` if it names the suite file
- Use the `LSP` tool's `findReferences` on `game::Arena` and `game::Scene` to find every caller.

**Interfaces:**
- Produces: `game::Host` (was `Arena`; constructor `Host(std::shared_ptr<const core::Manifest>, core::SceneKey = ARENA_SCENE, const core::GameData& = BuiltInData())`);
  no `game::Scene` enum (`game::Scene` is now only the interface).

- [ ] **Step 1: Rename** with the LSP's references; `game::Scene::Stress` → `game::STRESS_SCENE` and so on.
- [ ] **Step 2: Run** `pixi run project_n test` (every suite, unchanged in count: the test-file selftest checks the
  renamed suite) and `pixi run project_n reference`.
- [ ] **Step 3: Gate and commit:** `pixi run project_n lint --range <base>..HEAD`;
  `refactor(game): name the arena class the host and drop the scene enum`.

---

## After the last task

The next session runs the final whole-branch review (AGENTS.md, Finishing a branch, step 3) with GLM alongside, then
copies the ledger's rulings into the spec's Outcome, marks the spec implemented and the phase table's row 5 done, and
opens the PR. 5a's draft spec is then re-checked against what this part built.
