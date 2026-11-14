# Phase 6, chunk 5, first part: scenes

Date: 2026-11-14 · Status: **implemented 2026-11-14** (approved 2026-11-14; see Outcome) · Chunk spec: `2026-11-10-phase6-chunk5-world-map-design.md` (split by the user,
2026-11-14: this part, then 5a, 5b, 5c) · Phase spec: `2026-11-10-phase6-design.md`

## In short

- **A clean, extendable scene layer in `game`:** a host that lasts the whole game (the world, the calendar, the
  player, combat) and one small class per scene in its own file. Adding a scene means adding a file and a line in a
  list, never editing checks spread across a shared class.
- **Scene changes as one sequence** the logic runs and publishes: requested, leaving, swap, loading, entering,
  running. The main thread answers the leaving and entering stages at once for now, so nothing changes on screen; chunk
  10 draws the transitions and the loading screen into those stages without touching the logic.
- **Nothing a player or a test can see changes:** reference pictures, replay tests and the save format stay as they
  are. That is how this part proves itself.

## Intent

The scenes still to come are many: the world map (5a, 5b), three towns (chunk 6), one-off arenas and two dungeons
(chunk 7). Today `game::Arena` (about 1,100 lines) is every scene at once: `game::Scene` lists four, and checks for
them are scattered through it (which view, whether it's a combat scene, which demo glows, what `Build` spawns, which
scenes a save may name). A fifth scene would add a case to each, and every later one more. The user wants "a clean,
extendable scene management system", built on its own before 5a so 5a starts on clean ground, and built so that
transitions and loading screens can come in later (the user, 2026-11-14).

## Approaches

| Way | What it trades |
|---|---|
| **Code, shaped like data (picked, the user, 2026-11-14)** | Each scene is a small class implementing one interface; its contents are plain tables in its file, the shape a data file would have. Small, and changes nothing. Chunks 6 and 7 can move the tables into YAML when hand-made towns need it. |
| Data now | `assets/data/scenes.yaml` read by the host, with named code hooks and hot reload. Truest to "behaviour is data" (0002 §7.7), but brings a new reader, fuzzing, a built-in mirror and a new data hash that breaks existing replays: not small. |
| A fifth case in `Arena` (no part) | Cheapest today; 5a, 5b and 5c would then grow the switch and 5c pull it apart. |

## History

- **The engine side exists and stays** (0002.3 §6, 0002.3.1): each manifest scene lists its sprites in
  `art/scenes.toml` (`core::SceneId`); `core::Simulation::Scene()` names the sprite set the state needs; while it isn't
  resident, `core::Logic::RunBurst` runs no tick, counts nothing and records nothing, taking only Pause, Resume,
  `SceneResident` and `ReloadData`, so a load costs replays no determinism; the renderer decodes on workers and
  uploads within a 2 ms budget a frame, drawing the clear colour meanwhile ("the loading screen's view model and
  picture come with the UI, phase 6"). `game::CheckScenes` refuses to start when a scene can't load its creatures'
  sprites.
- **Domains exist** (0002 §7.1, §7.7): `core::State` has domains, `game::ACTIVE` and `game::WORLD`
  (src/game/npcs.hpp); NPCs move between them under the same stable ID. The host owning the world domain and the scene
  the active domain follows from that.
- **Calendar steps already publish a transition** (`Snapshot::transition`, `Logic`'s `Calendar` phases: requested,
  published, then the step on the next tick); the scene sequence follows the same pattern.
- **Today a scene change** happens inside a tick: `CycleScene` makes `Arena::Tick` call `Build` for the next scene,
  which first ends every open use (`Combat::EndUses()`, UseEnded events) and then rebuilds both domains, the world NPCs
  included (`stress` spawns 50,000, the others fewer); the tick that changes the scene ends its burst.
- **Speed rules** (ci-and-local-speed Outcome): no test is loosened; `tests/unit/game/arena_tests.cpp` is slow on
  windows (address), and its split is planned for 5c.

## Goals

1. **The host** (the class that is the `core::Simulation`): owns the state's domains, the world NPCs and the calendar,
   the player's cultivator, selection, combat (`game::Combat`), the game data, saving, loading and hashing, and
   publishing's shared work. It knows no scene by name.
2. **Scenes:** one interface, and one class per scene in its own file (`arena`, `stress`, `painted`, `cells`), each
   giving:
   - its name and the manifest scene (sprite set) it draws with;
   - its view rectangle, whether it is a combat scene, and its camera kind (follow the cultivator, or still; 5a adds
     free);
   - its contents as plain tables (props, spawns, orbs, the crowd), and how many world NPCs it asks for (unchanged
     from today until 5c keeps the world alive);
   - its own behaviour through hooks the host calls: build, leave, each tick, and publish (the arena's aura, demo
     glows and showcase seals; the stress spawner; the cells grid).
3. **A scene list:** the scenes in F2's order, which `SceneNamed`, `SceneName`, `CheckScenes`, the command line's
   `--<name>` flags and loading a save all read. Adding a scene is a file and a line here.
4. **The scene-change sequence**, run by `core::Logic` and published in every snapshot:
   1. **Requested:** the game asks for a scene (F2 today; an enter prompt in 5c; a save loaded from another scene).
      The tick that asks ends its burst. The old scene's leave step runs on that tick (open uses end, as now), so its
      events keep their tick.
   2. **Leaving:** the old scene stays on screen, frozen. The main thread plays the transition out (nothing yet), then
      reports it done.
   3. **Swap:** the host builds the new scene.
   4. **Loading:** the existing protocol, unchanged; the loading screen's place.
   5. **Entering:** the new scene is drawn frozen while the transition in plays (nothing yet); the main thread reports
      it done.
   6. **Running.**
   - Leaving, loading and entering run no tick, count nothing and record nothing, as loading does now, so a transition
     of any length on any machine leaves replays exact. The main thread's reports are its news, never input to the
     simulation, and never recorded (as `SceneResident`).
   - The snapshot says which stage it is in and the scenes on either side, so a later transition can choose how to
     look (a fade, an ink wipe, a different one into combat).
   - In this part the main thread reports leaving and entering done at once: a real report each time, so the path is
     used and tested from the start (the old scene shows frozen for a frame or two before the clear colour, where F2
     goes straight to the clear colour today).
   - **Every change of what's on screen runs it** (the user, 2026-11-14): F2, later the enter prompt, **loading a save
     and starting a replay**, and the game's start (loading, then entering). A load's bytes are read and checked on
     the tick that asks, as now, so a broken save is still refused there (and a recording finishes there, as now); the
     checked state is held and restored at the swap, where the new timeline epoch starts. A load into the scene
     already shown runs leaving, the swap and entering (nothing to load).
5. **Nothing else changes:** the same pictures once a scene is running, the same replay and save tests, the same save
   format and version, the same events at the same ticks, the same debug keys and flags.

## Non-goals

- Drawing transitions or a loading screen (chunk 10); the world surviving a scene change, and its save format (5c);
  the world scene and the free camera (5a); scene contents as YAML (chunks 6 and 7, when needed).
- Stacking UI. Windows, prompts and modal screens stack in the `ui` module (chunk 8: Escape closes the front window)
  over whatever scene is current; modals pause the game with the recorded `Pause` and resume actions (chunk 10) and
  never change the scene. Screens never become scenes. The meeting points are the published stage (the loading screen
  draws while loading; transitions draw while leaving and entering) and chunk 10's main menu, whose backdrop can be one
  more scene in the list.
- A stack of scenes. There is one current scene; returning from a town or dungeon goes to a place the world remembers
  (the player's tile, 5c). Dungeon rooms fit this as scenes of their own.
- Changing `core::SceneId`, the manifest, `art/scenes.toml` or the renderer's loading.

## Anti-goals (a failure even if it works)

- **Anything visible or recorded moving:** a reference picture, a replay test's hashes, a save's bytes, an event's
  tick. (Replay files recorded by an older build aren't promised to check clean: determinism holds within one binary,
  0002 §7.4.)
- **A new scene still needing edits outside its file and the list** (a check on the scene's identity anywhere in the
  host, the logic or `app`).
- **A transition that could cost determinism:** any tick counted, or any input recorded, while leaving, loading or
  entering.
- **The host knowing scenes' insides,** or scenes reaching into each other.

## Decisions

The user's, 2026-11-14:

1. **This part comes first, on its own**, before 5a: a clean, extendable scene management system.
2. **Scenes are code, shaped like data:** a class per scene with its contents as plain tables; YAML later if needed.
3. **Built for transitions and loading screens to come in later:** the six-stage sequence above, with the main
   thread's part instant for now.
4. **One current scene,** no stack; the world keeps where the player is.
5. **Loading a save and starting a replay run the same sequence** as any scene change, so transitions and the loading
   screen serve them too.

## Calls made here (not the user's; check them)

- **The sequence lives in `core::Logic`,** beside the waiting it already does for loads; the game only asks for a scene
  and builds it when told. `Simulation` gains what that needs (the scene asked for; build it).
- **The leave step runs on the requesting tick,** so UseEnded events keep their tick and the hashes don't move; what is
  drawn while leaving is the snapshot that tick published.
- **`game::Scene` the enum goes,** replaced by the list; scenes are found by name or by their place in the list (what a
  save stores today stays byte-identical).
- **`Arena` is renamed** for what it becomes (the host); the arena is one scene among the others.
- **The camera kind travels in the snapshot** (follow or still now), so `app` stops inferring it; 5a adds free.
- **The snapshot names the scenes on either side by the game's own key** (its place in the list, which core passes
  through without reading) with each one's sprite set and whether it's a combat scene, so a transition can tell a way
  into combat, and scenes sharing a sprite set (dungeon rooms) stay distinct.
- **The stage isn't hashed:** leaving, loading and entering take no counted tick, so no replay check falls inside
  them, and adding the stage to `Logic::Hash` would change every hash. Where a requesting tick falls on a replay's hash
  tick, the hash is taken as that tick leaves the state (the leave step done, the swap still to come), the same on
  recording and playback.
- **The per-scene comparison with today's `Build` is a one-off** in the refactoring commits (determinism is per
  binary, so committed positions wouldn't hold across presets); it goes with the old code. The lasting proof is the
  existing suites.

## Testing

- **The existing suites, unchanged, are the main proof:** reference pictures, replay round trips (including F2 while
  recording), save round trips and the save fuzz corpus, the scene-change cases (mid-volley included).
- **The sequence** (core, with a fake simulation): a request ends its burst; leaving, loading and entering run no
  tick, count nothing and record nothing; each stage is published in order; the main thread's reports move it on; a
  replay recorded across a scene change with slow and instant reports plays back with the same hashes.
- **The list:** names round-trip; every listed scene passes `CheckScenes`; F2 visits them in order and wraps.
- **Each scene:** builds the same entities in the same places as before (a one-off comparison with today's `Build`
  while refactoring).
- **Loads and replays through the sequence:** a broken save is refused on its tick, changing nothing; a sound one is
  restored at the swap, the epoch moving there; a load into another scene waits for it to be resident; a replay
  starting in another scene plays back with the same hashes; the game's start runs loading, then entering.
- **The harnesses** (the replay tests' session, the app and logic test rigs, `App`) send the main thread's reports
  through one helper.

## Left to the builder

The interface's exact shape and names; the host's name; how the snapshot carries the stage; file layout within
`src/game/`; how the existing tests are re-pointed (never loosened); commit split and order.

## Outcome

Implemented on `worktree-phase6-chunk5-scenes`, 2026-11-14, by the plan
`docs/superpowers/plans/2026-11-14-phase6-chunk5-scenes.md` (Tasks 1 to 5, in the plan's waves A to D). This records
what was built, every ruling of the plan's ledger with what it costs if wrong, the final review's findings and what is
left open. Commits are named by subject: the rebase merge rewrites every hash. Each task was reviewed once (Opus; Task
5, a rename, on Sonnet), every finding fixed, Minor included, and the fix re-reviewed (Sonnet; comment-only fixes on
Haiku); builds were verified at each task's head (`test`, `lint`, `reference`), `check` at the branch's.

### What was built

- **Task 1 (wave A), entering after the request:** "enter a scene after the tick that asks for it", with "cover a
  scene sharing sprites and the camera's reset" and "leave the scene alone when nothing asked for one" from its review.
  `core::SceneKey` and `core::SceneFacts` (the game's key, the sprite set, whether it is a combat scene),
  `Simulation::Current()`, `Requested()` and `EnterScene()`, and `Snapshot::camera` (`core::CameraKind`, reset to Pan
  before each publish).
- **Task 2 (wave B), the stages:** "run scene changes through leaving, loading and entering", with "cover a Load and a
  stale SceneEntered during a scene change". `core::SceneStage`, `Snapshot::sceneChange` (stage, the scenes on either
  side, the change's number; the start is change 1, loading), `ActionKind::SceneLeft` and `SceneEntered` carrying the
  change's number, and `core::SceneReports`, which App and every test harness answer through (at once, until chunk 10
  draws the transitions). The stages are `Logic::Wait()`: no tick, nothing counted, nothing recorded, stale reports
  dropped, and the replay reader refuses the reports.
- **Task 4 (wave B, beside Task 2), a class per scene:** "compare each scene's build with the code before" (the
  one-off comparison: `StateHash`, the player, `Current()` and the full save bytes per scene, all four equal, then
  deleted with the old code), "make each scene a class of its own" and "keep each scene's knowledge in its own files".
  `game::Scene` (then named `game::SceneClass`) in `game/scene.hpp` with `SceneTraits` (combat, creatures, demo glows,
  camera, world NPCs, view, home), the four stateless scenes in `game/scenes/`, `SCENE_LIST` in `game/scenes.cpp` (F2's
  order; each key constant checked against its place at compile time). The host never checks which scene it runs.
- **Task 3 (wave C), loads and replays through the change:** "load saves and replays through the scene change", with
  "cover pausing and a step asked for during a load". A save or a replay's start is read and checked on its tick (a
  broken one refused there, a recording finished there); the simulation holds the sound state (`Simulation::Load`,
  then named by `Requested()`) and `Logic` its counters, pausing and calendar phase (`Restored`) until the swap, where
  the epoch moves and a replay's start hash is checked. A load has no leave step.
- **Task 5 (wave D), the names:** "name the arena class the host and drop the scene enum". `game::Arena` became
  `game::Host` (`game/host.hpp`), the enum went, `game::SceneClass` took the name `game::Scene`, and locals, comments
  and file pointers followed.
- **The final review's fixes:** "record a pause taken while the scene changes", "say a loaded timeline begins once the
  old scene is left", "guard the save's one-byte scene key", "say host where comments and tests still said arena",
  "leave the swords' seal out of pictures without seal looks" and "point the design docs at the host and the scene
  classes".
- **What `check` caught at the branch's head** (the first `check`: builds are verified per task with the native preset
  only): "build a held load whole, without a default constructor" (MSVC's C4623 on `Host::Loaded`, which holds a
  `core::State`; logged under src/AGENTS.md's Gotchas) and "keep one HeadOf, beside the components it reads" (Task 4
  had given the scenes a public `game::HeadOf` while `combat.cpp` kept an identical one, ambiguous in a unity build;
  the one left is in `game/components`).

The save format (`ARENA_FORMAT`, `ACTIVE_VERSION`, `WORLD_VERSION`) and every reference picture are unchanged; the
existing replay, save and scene-change suites pass unloosened (re-pointed where a load now completes at the swap: a
check on the loaded snapshot moved to the entered one).

### Rulings (the plan's ledger)

1. **A replay hash taken on a requesting tick sees the old scene** (the swap waits for `SceneLeft`), where Task 1 saw
   the new one. The spec's call allows it, and replay tests compare a recording with its own playback in one binary.
   If wrong: replay files from before this branch diverge at that tick, which 0002 §7.4 never promised. The final
   review agreed.
2. **Tasks 1, 3 and 5 ran in the feature worktree itself,** each its wave's only task; wave B's two implementers ran
   in worktrees nested in it (`.worktrees/task2`, `.worktrees/task4`, gitignored), as this session could run git only
   inside its own worktree. If wrong: only folders move.
3. **The interface was `game::SceneClass` until Task 5** removed the enum: two types can't share a name in one
   namespace. Task 5 renamed it `game::Scene`, written `game::Scene` inside the host, where `Simulation::Scene()`
   shadows the name. If wrong: one more symbol to rename.
4. **A held Load wins over a pending scene request in `Requested()`, and `EnterScene()` clears both;** `Host::Load` no
   longer clears a pending request (Task 3). The logic can't produce both at once (a Load returns before the
   simulation ticks, and a Load during a change is dropped), so this is defence. If wrong: a request made before a
   load would be lost, not a load.
5. **One `NOLINT(readability-redundant-member-init)` on `SceneTraits::demoGlows{}`:** Clang's
   `-Wmissing-designated-field-initializers` warns in each scene whose traits omit it, and tidy calls the `{}`
   redundant; src/AGENTS.md's established pattern. If wrong: a warning in four scenes or a tidy finding.
6. **Task 5 ran on Sonnet** as a mechanical rename (review: light). If wrong: a fix round on a stronger model.
7. **Stale file pointers left by Task 4's move were fixed in Task 5** (comments in `reference_tests.cpp`,
   `host_tests.cpp`, `save_tests.cpp`), as the user's rule asks for every finding.

### The final review (Opus, whole branch)

GLM's secondary review was skipped: the Z.ai usage limit was reached (resets 2026-11-15 00:51).

- **Important, fixed test-first:** a Pause or Resume taken during a scene change while recording (the window losing
  its focus during F2's change) was applied but never recorded, as a recording takes only running ticks' input; the
  playback ran those ticks unpaused and diverged at the next hash. It predated the branch for the loading wait, but
  the branch made the stages every transition's path, and chunk 10's transitions will last visibly long. `Wait()` now
  holds the last pausing it takes (not while a replay plays, whose pausing is its own), and the first running tick
  puts it in front of its input, where it is applied and recorded; live input since still wins. Test: "a Pause sent
  while recording during a scene change is recorded, so the replay plays back alike" (failed with 0 of 3 hashes
  matched before the fix).
- **Minor, all fixed:** stale "arena" where the host is meant (tests' build helper and support headers, two test
  names, `combat.hpp`, design docs 0002.1, 0002.3 and 0002.3.1); the NOLINT's reason; the logs' "begins once its scene
  is entered" (the epoch moves at the swap); a `static_assert` that the scene list fits the save's one byte.
- **The seal warning** `reference` printed three times ("a seal (key 0xc74c275c95fdfe22) isn't in seals.yaml"): it
  predated the branch. The key is `sword_formation`'s; the volley pictures and the effects case draw a swords cast with
  a renderer that has no seal looks. Their snapshots now drop the seal (it has its own picture, `seal_swords`); no
  picture changed, and the run is clean.
- **Set aside by the reviewer, ruled here:**
  - App's camera resets when the sprite set changes (`app/view.cpp`), so two scenes sharing sprites wouldn't reset
    it: none do yet; 5a owns app's camera and keys it on the change or the scene's key.
  - `Snapshot::camera` is published but app doesn't read it yet: the plan left that to 5a, which adds the free camera.
  - F5, F9, F10 and F7 pressed during a change are dropped with only a debug line, as loading dropped them: feedback
    is chunk 10's UI.
  - F2 while paused does nothing (the host sees `CycleScene` only on a ticking tick): unchanged from main; the pause
    menu (chunk 10) owns what may happen while paused.
  - `ARENA_FORMAT` keeps its name: renaming it is cosmetic and the format stays.
  - `Host::Current()` looks the scene's sprite set up by name each publish: a few string comparisons per burst, no
    measured cost.
- **The fixes' re-review (Sonnet):** clean but for one Minor, rejected: `#include <limits>` in `game/scenes.cpp` for the
  new `static_assert`; src/AGENTS.md keeps standard headers out of `.cpp` files, and `pch.hpp` includes `<limits>`.

### Left open

- 5a's spec (`2026-11-14-phase6-chunk5a-measure-camera-design.md`) is a draft, re-checked against this as built
  before its plan: app's camera from `Snapshot::camera`, and its reset keyed on the scene change.
