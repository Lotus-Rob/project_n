# Phase 6, chunk 5a: the measurements, the world scene and the camera

Date: 2026-11-14 · Status: draft, re-checked 2026-11-14 against the scenes part as merged
(`2026-11-14-phase6-chunk5-scenes-design.md`, its Outcome) · Chunk spec: `2026-11-10-phase6-chunk5-world-map-design.md` · Phase spec: `2026-11-10-phase6-design.md`

## In short

- **The phase's measurements, built first:** one `project_n` command runs the game along a scripted camera path with the
  job system pinned to 2 workers and prints frame-time percentiles, hitches, input latency, the GPU's share and shimmer.
  Every later piece of phase 6 records its numbers against it.
- **A world scene, about 20 screens across,** with placeholder terrain tiles and a stand-in crowd of walkers, so the
  camera and culling are proven on a map too big to see at once before anything real lives on it.
- **The camera:** follows the cultivator, pans freely (drag, screen edge, keys, stick), zooms as today, and a key snaps
  it back.
- **Culling against the window:** the logic publishes what the main thread's view shows plus a margin, in every scene;
  the fixed `FOLLOW_HALF_VIEW` goes.

## Intent

Chunk 5's spec put eight systems and a save-format change into one chunk. Split three ways (the user, 2026-11-14), the
first part lays the ground everything else stands on: a way to measure smoothness and cost that every later chunk
reuses, and a camera and culling that stay smooth over a map far bigger than the screen. Building them on a stand-in
map first means 5b's living map and 5c's world-and-scene split are judged against numbers that already exist.

## The split (approaches)

| Way | What it trades |
|---|---|
| **One chunk**, as the 2026-11-10 spec stands | One very long branch and one huge review; nothing merges for a long time |
| **Two parts:** measurements and camera, then everything else | Mixes the riskiest change (saves and the world surviving scenes) with the largest (the living map) |
| **Three parts (picked, the user, 2026-11-14)** | **5a** this document; **5b** the map as data, fog, time, travel, names and labels, click to walk; **5c** the world-and-scene split, the save version, the enter prompt and the stand-in town |

Each part gets its own spec update, plan, branch and PR. The chunk spec's fourteen decisions stand; each part's spec
says which it carries. 5b and 5c update the chunk spec when they start.

## History

- **Nothing of 5a is built.** The camera is the main thread's (`gfx::Camera`, `app/view.hpp`: follow,
  `ShowScene`, `WindowToWorld`); `ShowScene` resets it when the snapshot's sprite set changes. The logic culls to the
  scene's `SceneTraits::view` or, with a player, to `FOLLOW_HALF_VIEW` (±7,680 × ±4,320, the widest zoom in a 32:9
  window) around it (`game::Host::Publish`, src/game/host.cpp), which its own comment and the backlog (the user,
  2026-11-07; 0002.3 §11) mark for culling relative to the window.
- **The scenes part (merged 2026-11-14) is the layer 5a builds on:**
  - A scene is a stateless class in `game/scenes/` behind `game::Scene`, listed in `SCENE_LIST` (`game/scenes.cpp`, F2's
    order; its key is its place there, its constant in `game/scenes.hpp`). Its `SceneTraits` hold what the host reads
    (view, home, camera, combat, world NPCs, demo glows), and `Scene::Publish` adds the scene's own content (the
    arena's showcase). `--<name>` starts any scene `art/scenes.toml` names.
  - A save stores the scene's key and accepts any listed scene, so a new scene saves and loads with no format change.
  - `Snapshot::camera` (`core::CameraKind`: Follow for the arena and `stress`, Pan for the still scenes) comes from the
    traits, but app doesn't read it yet; its Outcome leaves that, and resetting the camera on each scene change rather
    than on a new sprite set, to 5a.
  - Every scene change runs through stages the logic publishes (`Snapshot::sceneChange`: leaving, loading, entering,
    running, with the change's number). The main thread's news (`SceneResident`, `SceneLeft`, `SceneEntered`) reaches
    the logic beside the tick's input but is never part of it: never recorded, never hashed, and the replay reader
    refuses it.
- **Measurement debts owed to chunk 5:** chunks 3, 4 and 4b each recorded unpinned numbers "until chunk 5" (chunk 3's
  burst 0.43 ms median and 2.6 ms worst on the main thread; chunk 4's 1,000 swords 1.31/2.31/2.60 ms whole frame,
  median/p99/worst; chunk 4b ~685 µs median frame while swords fly, `DrawVolleys` 133 µs mean; all on an RTX 4090 at
  720p). There is no way to pin the job system yet (`Jobs::DefaultWorkers()` ran 30 workers), chunk 3's Outcome asks
  for `tracy-profiler-csvexport` in pixi.toml's `profiler` feature (today read from Pixi's package cache), and the
  captures have no GPU zones (the backlog: measure the GPU before judging any move of work onto it).
- **GPU cost so far** was measured only as a frame-time difference with an effect shown and hidden (chunk 4b), since
  SDL_GPU has no timestamp queries. The method chosen here unblocks the backlog's GPU projectiles, volley instancing
  and seal-layout items.
- **Labels (chunk 3, Ruling 25):** label text is 20 world units, so labels fade out below about zoom 0.45 at 1080p
  (0.68 at 720p). At the widest view (0.25) they are hidden; the measurements judge labels at a middle zoom.
- **Shimmer (chunk 3, Ruling 24)** has a metric already (signed darkness, 1 − luminance) and the reference-picture
  machinery (`project_n reference`, software Vulkan).
- **The `cells` scene** is the mip test and a reference picture (0002 §16 phase 3); the chunk spec's "`cells` becomes
  the world map" would take that away, so the world is a new scene.
- **Determinism (0002 §7.4):** the simulation is deterministic within one binary, with no fast-math; new logic uses
  `core::math` and counter-based random. Floating-point contraction across architectures is unsettled (backlog): 5b
  decides it before route maths lands.
- **Speed rules (ci-and-local-speed Outcome):** no test, check or required job is loosened for speed.

## Goals

1. **The world scene.** A new scene class on the scenes layer (`game/scenes/world.*`, listed after `cells` in
   `SCENE_LIST` with its key constant, and `world` in `art/scenes.toml`, which brings `--world`) about 20
   screens across at zoom 1 (about 38,400 × 21,600 world units), tiled with placeholder terrain from
   `art/placeholders.py` (several kinds, varied enough that panning shows movement and shimmer would show). The tiles
   are static scene content, not entities: the scene's `Publish` finds the ones in view by their grid position, never
   by scanning them all. A stand-in
   crowd of walkers wanders the whole map, enough that the widest view over its busiest part has thousands on screen,
   some with name labels (placeholder names), so culling, labels and the measurements carry real load; 5b replaces them
   with travelling NPCs. The cultivator walks it as today (Move). The walkers are a pure function of their seed and the
   tick, built from existing components, so the saved state's format and `ACTIVE_VERSION` don't change in 5a (the
   world's save format is 5c's); F5 and F9 work in `world` as in any listed scene.
2. **The camera, outside combat** (the user, 2026-11-14): in `world` and the still test scenes. **Combat scenes**
   (the arena, `stress`) keep today's camera, following the cultivator only, so they play and replay as now; chunk 7
   can revisit this for large combat scenes.
   - Each scene's traits name its camera, published as `Snapshot::camera`: Follow (combat scenes), Pan (the still
     scenes) and a third kind for `world`, following until panned, with snap back. App reads the kind from the snapshot
     instead of inferring it from what the snapshot follows.
   - App resets the camera on each scene change (`Snapshot::sceneChange`'s number), not on a new sprite set, so scenes
     that share sprites (5c's and chunk 7's rooms) each start from their own view.
   - Follows the cultivator by default, as today.
   - **Pans freely** by dragging with the mouse (the right or middle button, my call: the left button stays free for
     selecting and 5b's click to walk), by pushing the pointer to the screen's edge, by keys and by the gamepad's right
     stick. In `world`, Move walks the cultivator, so panning has its own keys; the still scenes keep panning with Move
     as today. Panning frees the camera from the cultivator until **snap back** (a key and a gamepad button) returns
     it.
   - Zooms with the wheel or the shoulder buttons as today, within 0.25 to 2.
   - Every camera action works by mouse, keyboard and gamepad. Defaults go in `app/bindings.cpp` (rebinding is chunk
     11). Edge pan can be turned off in `settings.yaml` (its switch comes with chunk 11's settings screen).
   - Motion is smooth at any frame rate: pan speed is in screens per second, so it feels the same at every zoom, and
     the camera never jitters while following, dragging, edge-panning or zooming.
3. **Culling against the window, in every scene.**
   - Each frame the main thread sends the logic its view rectangle plus a margin. The logic publishes only what lies
     inside the latest one it has.
   - The rectangle is the main thread's news, like `SceneResident`: it changes what is published, never the state; it
     is **not recorded** in replays, the replay reader refuses it, and the state hash ignores it.
   - It names the scene change it was taken in, and the logic uses only one taken in the current change, so the first
     publishes after a swap never cull the new scene by the old scene's view.
   - Until one arrives (the start, after each swap, and harnesses that never send one: the reference pictures, the
     headless benchmark), the logic culls to the widest zoom (0.25) of a 32:9 window round where the camera starts
     (what it follows, or the centre of the scene's view): what `FOLLOW_HALF_VIEW` covers today, derived from the zoom
     limit instead of a fixed number, so those pictures and numbers don't move.
   - The margin is wide enough that a fast pan or a wheel notch of zoom-out, in the time a view takes to reach a
     published snapshot, never shows the edge of what was published.
   - `FOLLOW_HALF_VIEW` and its comment go; the backlog's culling item and the 0002.3 §11 note close.
4. **The measurements**, as one `project_n` command (my call: `measure`):
   - builds and runs the game in releasedbg on the world scene, along a **scripted camera path**: follow, pan, zoom
     in and out, the widest view over the busiest region, and a middle zoom where labels show. The job system is
     pinned to 2 workers by a new game flag (`--workers N`, also usable by `profile`). It records a Tracy capture into
     `build/profiles/`;
   - also runs **the arena's earlier measurements, pinned the same way** (the user, 2026-11-14), as named runs: the
     damage-number burst (`--burst`), 1,000 flying swords (`--cast-swords`) and swords with their seals shown, so
     chunks 3, 4 and 4b's unpinned numbers are re-recorded in the same table as the new baseline (their old numbers kept
     beside them for comparison);
   - measures under **fixed conditions**: fullscreen on the primary display at its native size, an unpaced present mode
     (so the display's refresh doesn't stretch the work), with the size and refresh printed with the numbers (my call:
     a tiling window manager resizes a window, and a paced present at 165 Hz made zones 3 to 5 times slower in chunk
     4b's runs);
   - exports it with `tracy-csvexport`, now a pinned package in pixi.toml's `profiler` feature;
   - prints **frame-time percentiles** (median, 99th, worst) for the whole frame and per thread;
   - prints **hitches**: any frame whose work (its time less the wait to acquire the next image) is over the budget
     outside the designed pauses, with what it was doing (its largest zones); wall-clock frame time is printed beside
     it;
   - prints **input latency**: from a scripted click or key to the first frame showing it;
   - prints **the GPU's share**, by a method this part chooses and records;
   - prints **shimmer**: pictures at several zooms and sub-pixel camera offsets, compared frame to frame with chunk
     3's metric;
   - sets the first **budget numbers** from its first runs, with the reasons, in this spec's Outcome. Each later
     chunk records its numbers there too.
   - It runs on the development machine. It is not a gate, and not part of `check` or CI.

## Non-goals

- The map as data, roads, cities and danger sites, fog of war, time flowing, travel, generated names, click to walk
  (5b).
- The world-and-scene split, saving the world, the enter prompt, the stand-in town (5c).
- The settings screen and rebinding (chunk 11); a whole-map view or a minimap.
- Moving any drawing onto the GPU (the backlog's GPU items); this part only measures.

## Anti-goals (a failure even if it works)

- **Camera jitter or stutter** while following, dragging, edge-panning or zooming.
- **Shimmer** of tiles or walkers at any zoom or sub-pixel offset.
- **Popping:** tiles, walkers or labels appearing at the view's edge from one frame to the next.
- **Hitches** when the camera reaches new ground or many walkers come into view.
- **Other scenes changing:** the arena, `stress`, `painted` and `cells` draw and replay as before; their reference
  pictures and replay hashes don't move.
- **Numbers that can't be trusted:** runs of the same build that disagree by more than the noise the Outcome records,
  or a measurement that changes what it measures without saying so (Tracy's own cost).

## Decisions

The user's:

1. **Chunk 5 is split three ways** (5a, 5b, 5c above; 2026-11-14).
2. **Chunk 5 comes before the sweep-up** (2026-11-14; the sweep-up waits for all of phase 6, as recorded 2026-11-13).
3. **The free camera is outside combat only:** the world and the still scenes; combat scenes keep following
   (2026-11-14).
4. **`measure` re-records chunks 3, 4 and 4b's arena numbers** pinned, beside the world path (2026-11-14).
5. Carried from the chunk spec: **the world map carries the scale** (about 20 screens across, too big to see at once);
   **the camera follows, pans freely, zooms, and a key snaps back** (click to walk is 5b); the measurements are
   **recorded per chunk, not a gate**, on the development machine with the job system pinned to 2 workers.

## Calls made here (not the user's; check them)

- **A new `world` scene;** `cells` stays the mip test, and `stress` and `painted` stay as they are.
- **The game still starts in the arena** until 5c makes the world map the hub (until then the world is a stand-in with
  nowhere to enter).
- **A stand-in crowd** of wandering walkers with placeholder names, until 5b's travellers; stateless (seed and tick),
  so no save format change in 5a.
- **Panning frees the camera until snap back;** walking doesn't re-attach it.
- **Dragging uses the right or middle button,** leaving the left for selecting and click to walk.
- **Fixed measuring conditions:** fullscreen at native size, unpaced present; hitches judged on a frame's work, not its
  wall time.
- **Edge pan on by default,** off in `settings.yaml`.
- **Labels are measured at a middle zoom,** walkers at the widest view (Ruling 25 hides labels there).
- **The command is `measure`;** `--workers N` pins the job system.
- **The zoom range stays 0.25 to 2.**
- **A third camera kind for `world`** (follow until panned, snap back) beside Follow and Pan, so combat scenes and the
  still scenes keep theirs; its name is the builder's.
- **The view is the main thread's news, named by its scene change;** before one arrives, the logic culls as widely as
  today's `FOLLOW_HALF_VIEW`, so harnesses that send none (reference pictures, the headless benchmark) are unchanged.
- **Cleanups placed in later parts:** splitting `tests/unit/game/host_tests.cpp` (the scenes part renamed
  `arena_tests.cpp`; 1,021 lines, the scene list's cases now in `scenes_tests.cpp`) is 5c's first task (5c adds the
  save and scene cases there); the floating-point contraction decision is 5b's, before its route maths.

## Testing

- **Camera maths** (pure, no device): drag moves the world with the pointer; edge pan starts only inside the edge band
  and its speed is the same in screens per second at every zoom; panning frees the camera; snap back follows again;
  zoom stays in range; combat scenes ignore the free-camera inputs and keep following; app takes the camera's kind
  from the snapshot and resets the camera on each scene change, between scenes that share sprites too.
- **Culling:** what is published lies within the view plus the margin; a replay recorded while panning plays back with
  the same hashes; the state hash ignores the view and the replay reader refuses it; with no view yet, and after a
  swap until a view of the new change arrives, the logic culls as widely as today.
- **The world scene:** only tiles in view are published, found without a full scan; `CheckScenes` covers `world`; a
  walker's position is the same for the same seed and tick; a save made in `world` loads back into it.
- **Other scenes:** their reference pictures and replay tests pass unchanged.
- **The measurement command's own logic** (tools self-tests): reading the export, percentiles, finding hitches,
  latency from the log, shimmer between pictures.
- **Pictures:** the world scene at several zooms (reference pictures).
- **The first measurement's numbers** in the Outcome, as the phase's baseline.

## Left to the builder

Tile size and tile art; the crowd's size and how it wanders; the culling margin; pan speeds, the edge band's width and
the default keys and buttons (every action on mouse, keyboard and gamepad); the scripted path and its flag; the GPU
method (SDL_GPU has no timestamp queries; for example, the CPU's wait on the GPU at submit or acquire, or a frame-time
difference with work shown and hidden), recorded with why; the budget numbers; command details; commit split and
order.
