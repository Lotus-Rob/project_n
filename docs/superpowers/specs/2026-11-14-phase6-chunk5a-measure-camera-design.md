# Phase 6, chunk 5a: the measurements, the world scene and the camera

Date: 2026-11-14 · Status: **implemented 2026-11-15** (see Outcome; approved 2026-11-14, re-checked against the scenes part as merged:
`2026-11-14-phase6-chunk5-scenes-design.md`, its Outcome) · Plan: `2026-11-14-phase6-chunk5a.md` · Chunk spec: `2026-11-10-phase6-chunk5-world-map-design.md` · Phase spec: `2026-11-10-phase6-design.md`

## In short

- **The phase's measurements, built first:** one `project_n` command runs the game along a scripted camera path with the
  job system pinned to 2 workers and prints frame-time percentiles, hitches, input latency, the GPU's share and shimmer.
  Every later piece of phase 6 records its numbers against it.
- **A world scene, about 20 screens across,** with placeholder terrain tiles and a stand-in crowd of walkers, so the
  camera and culling are proven on a map too big to see at once before anything real lives on it.
- **The camera:** follows the cultivator, pans freely (drag, screen edge, keys, stick), zooms as today, and a key snaps
  it back.
- **Portals in place of F2:** a glowing portal in each walkable scene takes the cultivator to the next one.
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

5. **Portals, in place of F2 for now** (the user, 2026-11-14; the real connections between places are 5c's):
   - One portal in each scene with a cultivator to walk (the arena, `stress`, `world`), away from where it starts.
   - They lead round the walkable scenes in F2's order (arena → `stress` → `world` → arena), skipping `painted` and
     `cells`, which have no character; F2 stays as the debug key that visits every scene.
   - Stepping onto one starts the scene change at once, with no prompt (the prompt is 5c's, for real entering). The
     host checks the cultivator's foot against it in the tick, so it is recorded and replayed as any walk; the
     cultivator arrives at the next scene's usual start, off its portal, so it never bounces straight back.
   - It looks like a glowing ground seal from the seals system (a `portal` look in `seals.yaml`, hot-reloaded, never
     hidden by the showcase key), with a "Portal" label over it.
   - In the arena it stands in clear ground away from the fight and the showcase, with a footprint test (showcased
     items never overlap); the portal is scene content, not an entity, so no state hash or save changes.

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
- **Other scenes changing:** the arena, `stress`, `painted` and `cells` draw and replay as before; their replay hashes
  don't move, and their reference pictures move only where the portal shows, refreshed on purpose.
- **Numbers that can't be trusted:** runs of the same build that disagree by more than the noise the Outcome records,
  or a measurement that changes what it measures without saying so (Tracy's own cost).

## Decisions

The user's:

1. **Chunk 5 is split three ways** (5a, 5b, 5c above; 2026-11-14).
2. **Chunk 5 comes before the sweep-up** (2026-11-14; the sweep-up waits for all of phase 6, as recorded 2026-11-13).
3. **The free camera is outside combat only:** the world and the still scenes; combat scenes keep following
   (2026-11-14).
4. **`measure` re-records chunks 3, 4 and 4b's arena numbers** pinned, beside the world path (2026-11-14).
5. **The world gets terrain tiles of its own** (2026-11-14): seamless placeholder tiles generated for it, more kinds
   than the `cells` mip test's bordered cells, so the map reads as terrain rather than a grid.
6. **Portals replace F2 for now** (2026-11-14), as Goal 5 says; real connections between places are 5c's.
7. Carried from the chunk spec: **the world map carries the scale** (about 20 screens across, too big to see at once);
   **the camera follows, pans freely, zooms, and a key snaps back** (click to walk is 5b); the measurements are
   **recorded per chunk, not a gate**, on the development machine with the job system pinned to 2 workers.

## Calls made here (not the user's; check them)

- **A new `world` scene;** `cells` stays the mip test, and `stress` and `painted` stay as they are.
- **Ten terrain kinds, two variants each:** meadow, grassland, forest, hills, rock, snow, sand, marsh, shallows and deep
  water, 256 world units square, each seamless with itself and its other variant; kinds meet at hard edges (placeholders
  until 5b's map as data), laid out in regions by low-frequency noise over the map.
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
- **Other scenes:** their replay tests pass unchanged; their reference pictures change only where the portal shows.
- **Portals:** stepping onto one asks for the next walkable scene and a step beside it doesn't; the cycle goes arena →
  `stress` → `world` → arena; a replay recorded through a portal plays back with the same hashes; the arena's portal
  overlaps no showcase footprint or fight space.
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

## Outcome

### Measurements (2026-11-15)

The phase's first measurement, taken by plan Task 9 on the build of "build(tools): add measure, the phase's smoothness
and cost numbers": `pixi run project_n measure` three times in a row (every run: world, burst, swords, seals), with the
display free and nothing else running. All three exited 0 and were trusted (no pause, no camera path warning, every path
finished), with no hitches and every row marked `paced`. They are the baseline every later chunk of phase 6 records
against.

**The machine.**

| Part | What it is |
|---|---|
| CPU | Intel Core i9-13900K (24 cores, 32 threads, up to 5.8 GHz); `powersave` governor, energy preference `balance_performance`, power profile `balanced` |
| GPU | NVIDIA GeForce RTX 4090 (24 GiB), driver 620.70.06, Vulkan |
| Memory | 62 GiB |
| OS | CachyOS, kernel 7.3.4-1-cachyos |
| Desktop | niri 26.04 (Wayland); the game on SDL 3.4.16's Wayland driver, presenting with mailbox (`--unpaced`: the driver offers no immediate) |
| Displays | Three. Measured on DP-3, Dell S2721DGF, 2560×1440 at 165.08 Hz, variable refresh off (focused in every run; the game logged `display: 2560x1440 at 165 Hz` each time). The other two are 60 Hz: DP-1, Dell U2722DE, 2560×1440 at 59.95 Hz, and DP-2, another Dell U2722DE at 59.95 Hz, turned to portrait (1440×2560) |

**How to read the tables.** Times are in ms; each cell is run 1 / run 2 / run 3. *Frame* is the main thread's `Run`
zone, start to start; *work* is the frame less its wait for the next swapchain image (`WaitForGpu`). The main thread's
own work equals *work* within 0.01 ms in every cell, so it isn't repeated. p99 is `—` below 100 frames (each segment's
first 0.5 s: 82 frames). The rest of each camera segment has 577 to 579 frames (the widest view 908 or 909); the arena
runs leave their first 2 s out and have 2,970 to 2,972 frames (burst), 11,225 or 11,226 (swords) and 4,622 or 4,623
(seals).

| Run | Segment | Frame median | Frame p99 | Frame worst | Work median | Work p99 | Work worst | GPU share |
|---|---|---|---|---|---|---|---|---|
| world | follow: first 0.5 s | 6.05 / 6.06 / 6.03 | — | 7.43 / 7.28 / 7.24 | 0.62 / 0.56 / 0.58 | — | 1.21 / 1.16 / 0.94 | 89 / 90 / 90 % |
| world | follow: rest | 6.06 / 6.04 / 6.05 | 8.11 / 8.08 / 7.67 | 8.57 / 9.59 / 9.88 | 0.74 / 0.75 / 0.67 | 2.99 / 3.13 / 1.25 | 3.53 / 3.67 / 1.61 | 86 / 87 / 89 % |
| world | pan: first 0.5 s | 6.04 / 6.07 / 6.06 | — | 8.80 / 8.44 / 9.52 | 0.98 / 1.00 / 0.61 | — | 3.35 / 3.32 / 4.04 | 81 / 81 / 86 % |
| world | pan: rest | 6.11 / 6.04 / 6.08 | 8.61 / 9.06 / 9.43 | 8.97 / 10.18 / 10.89 | 1.11 / 1.03 / 1.01 | 3.79 / 3.85 / 4.12 | 4.05 / 4.00 / 5.83 | 78 / 79 / 81 % |
| world | zoom in and out: first 0.5 s | 6.08 / 6.11 / 6.06 | — | 9.29 / 8.84 / 9.55 | 0.96 / 1.00 / 0.95 | — | 4.36 / 3.50 / 4.05 | 79 / 79 / 83 % |
| world | zoom in and out: rest | 6.07 / 6.09 / 6.03 | 9.01 / 8.93 / 9.22 | 9.62 / 9.36 / 13.06 | 1.07 / 1.06 / 1.06 | 3.94 / 3.92 / 4.17 | 4.21 / 4.24 / 7.94 | 79 / 78 / 78 % |
| world | widest view: first 0.5 s | 6.14 / 6.16 / 6.04 | — | 10.15 / 9.38 / 9.12 | 1.58 / 1.46 / 1.44 | — | 4.95 / 4.71 / 4.80 | 69 / 71 / 74 % |
| world | widest view: rest | 6.06 / 6.06 / 5.98 | 9.34 / 9.08 / 9.62 | 12.39 / 10.48 / 12.99 | 1.44 / 1.53 / 1.47 | 4.50 / 4.49 / 4.64 | 8.04 / 5.77 / 7.40 | 73 / 71 / 71 % |
| world | labels: first 0.5 s | 6.14 / 6.12 / 5.96 | — | 10.33 / 10.18 / 12.66 | 1.22 / 1.28 / 1.27 | — | 5.91 / 4.23 / 4.07 | 76 / 76 / 75 % |
| world | labels: rest | 6.09 / 6.09 / 6.02 | 9.05 / 9.40 / 9.21 | 11.05 / 10.78 / 10.89 | 1.21 / 1.29 / 1.23 | 3.82 / 4.13 / 4.07 | 5.93 / 4.78 / 5.06 | 76 / 76 / 77 % |
| burst | whole run | 6.04 / 6.05 / 6.08 | 9.10 / 9.14 / 8.94 | 10.71 / 13.68 / 13.38 | 2.62 / 2.54 / 2.67 | 5.68 / 5.31 / 5.67 | 7.75 / 7.23 / 8.26 | 56 / 58 / 55 % |
| swords | whole run | 6.05 / 6.05 / 6.05 | 9.19 / 9.27 / 9.26 | 12.79 / 13.50 / 12.02 | 2.56 / 2.51 / 2.59 | 6.08 / 6.18 / 6.15 | 11.05 / 8.94 / 8.28 | 56 / 57 / 56 % |
| seals | whole run | 6.06 / 6.05 / 6.04 | 9.00 / 9.02 / 9.21 | 11.18 / 10.56 / 12.25 | 2.55 / 2.64 / 2.56 | 6.15 / 5.81 / 5.89 | 7.97 / 7.37 / 10.24 | 57 / 57 / 57 % |

The other threads' work per frame. A zone counts in the frame it starts in, and the logic ticks at 60 Hz against 165
frames a second, so most frames hold none of its work and its median is mostly 0:

| Run | Segment | Logic median | Logic p99 | Logic worst | Worker 3 median | Worker 3 p99 | Worker 3 worst | Worker 4 median | Worker 4 p99 | Worker 4 worst |
|---|---|---|---|---|---|---|---|---|---|---|
| world | follow: first 0.5 s | 0.00 / 0.00 / 0.15 | — | 2.29 / 2.27 / 2.21 | 0.00 / 0.00 / 0.00 | — | 0.97 / 0.92 / 0.92 | 0.00 / 0.00 / 0.00 | — | 0.90 / 0.86 / 0.93 |
| world | follow: rest | 0.00 / 0.00 / 0.00 | 2.29 / 2.24 / 2.30 | 2.46 / 2.40 / 2.58 | 0.00 / 0.00 / 0.00 | 0.91 / 0.93 / 1.04 | 1.05 / 1.47 / 1.39 | 0.00 / 0.00 / 0.00 | 0.90 / 0.90 / 0.90 | 1.13 / 1.20 / 0.98 |
| world | pan: first 0.5 s | 0.00 / 0.00 / 0.00 | — | 2.26 / 2.50 / 2.08 | 0.00 / 0.00 / 0.00 | — | 0.93 / 0.86 / 0.87 | 0.00 / 0.00 / 0.00 | — | 0.96 / 0.87 / 0.20 |
| world | pan: rest | 0.00 / 0.00 / 0.00 | 2.32 / 2.29 / 2.28 | 2.50 / 2.45 / 2.61 | 0.00 / 0.00 / 0.00 | 0.96 / 0.88 / 0.94 | 1.12 / 1.03 / 1.07 | 0.00 / 0.00 / 0.00 | 0.94 / 0.94 / 0.91 | 1.08 / 1.17 / 1.14 |
| world | zoom in and out: first 0.5 s | 0.00 / 0.15 / 0.00 | — | 2.23 / 2.25 / 2.53 | 0.00 / 0.00 / 0.00 | — | 0.92 / 1.06 / 0.86 | 0.00 / 0.00 / 0.00 | — | 1.00 / 0.89 / 0.95 |
| world | zoom in and out: rest | 0.12 / 0.00 / 0.00 | 2.50 / 2.44 / 2.41 | 3.14 / 2.56 / 2.69 | 0.00 / 0.00 / 0.00 | 0.93 / 0.91 / 0.98 | 1.12 / 1.11 / 1.49 | 0.00 / 0.00 / 0.00 | 0.94 / 0.94 / 0.90 | 1.11 / 1.05 / 1.15 |
| world | widest view: first 0.5 s | 0.43 / 0.70 / 0.00 | — | 3.37 / 3.83 / 3.89 | 0.00 / 0.00 / 0.00 | — | 1.03 / 0.97 / 1.11 | 0.00 / 0.00 / 0.00 | — | 1.28 / 1.24 / 1.03 |
| world | widest view: rest | 0.00 / 0.27 / 0.14 | 3.25 / 3.11 / 3.08 | 3.45 / 3.58 / 3.27 | 0.00 / 0.00 / 0.00 | 1.15 / 1.01 / 1.01 | 1.83 / 1.33 / 1.18 | 0.00 / 0.00 / 0.00 | 1.06 / 1.02 / 1.06 | 1.52 / 1.52 / 1.51 |
| world | labels: first 0.5 s | 0.00 / 0.36 / 0.23 | — | 3.01 / 2.72 / 2.63 | 0.00 / 0.00 / 0.00 | — | 0.94 / 1.12 / 0.93 | 0.00 / 0.00 / 0.00 | — | 0.92 / 1.02 / 1.04 |
| world | labels: rest | 0.00 / 0.00 / 0.00 | 2.70 / 2.72 / 2.73 | 2.82 / 2.85 / 3.14 | 0.00 / 0.00 / 0.00 | 1.00 / 1.01 / 1.10 | 1.26 / 1.26 / 1.31 | 0.00 / 0.00 / 0.00 | 1.03 / 0.96 / 0.94 | 1.08 / 1.20 / 1.10 |
| burst | whole run | 0.00 / 0.00 / 0.00 | 0.22 / 0.25 / 0.23 | 0.61 / 0.69 / 0.66 | 0.35 / 0.35 / 0.36 | 0.46 / 0.45 / 0.46 | 0.69 / 0.58 / 0.73 | 0.35 / 0.35 / 0.36 | 0.45 / 0.44 / 0.46 | 0.75 / 0.66 / 0.63 |
| swords | whole run | 0.00 / 0.00 / 0.00 | 0.19 / 0.19 / 0.19 | 1.26 / 0.50 / 0.48 | 0.24 / 0.23 / 0.23 | 2.28 / 2.25 / 2.25 | 2.58 / 2.53 / 2.49 | 0.24 / 0.23 / 0.24 | 2.25 / 2.25 / 2.25 | 2.58 / 2.51 / 2.47 |
| seals | whole run | 0.00 / 0.00 / 0.00 | 0.20 / 0.20 / 0.19 | 0.48 / 0.48 / 0.55 | 0.25 / 0.25 / 0.26 | 2.27 / 2.24 / 2.24 | 2.48 / 2.41 / 2.47 | 0.26 / 0.27 / 0.28 | 2.24 / 2.22 / 2.23 | 2.59 / 2.48 / 2.43 |

Also measured: **input latency** (a Move press to its first drawn frame) 23.8 / 23.7 / 18.5 ms; **world shimmer**
0.0309 in all three (the reference pictures' world case, drawn on software Vulkan, so the same every run); **hitches**
none. Where the main thread's work goes (read from run 1's Tracy captures, not from `measure`'s table; kept locally in
`build/measure/20261115-104554/`, not committed): `SubmitFrame` (handing the frame to the driver) is its
largest zone, a mean of 0.59 ms a frame in the world, 0.71 in burst and 0.98 in swords, and at most 7.4 to 9.0 ms. In
the arena runs the job system's chunks come next (`PlaceNumberChunk` in burst, the swords' `ExecuteRange`), now shared
by the main thread and just two workers.

**The noise between the runs.** Frame medians agree within ±0.09 ms, work medians within ±0.07 ms and the workers'
medians within ±0.01 ms; the logic's medians, mostly 0 because few frames start a tick, jump between 0 and 0.7 ms and
mean nothing. Work p99s agree within ±0.2 ms, the logic's and workers' within ±0.09 ms, and frame p99s within ±0.41 ms.
Two cells fall outside: the 82-frame `pan: first 0.5 s` work median (0.61 against 1.00) and `follow: rest`'s work p99 in
run 3 (1.25 against 2.99 and 3.13). Worst frames move by ±0.1 to ±1.9 ms (they are single frames), the GPU share by up
to ±2.5 points and the input latency by ±2.7 ms. So, on a segment of 100 frames or more, a later change counts as real
when a work median moves by more than 0.15 ms (the widest span within this sitting is burst's 0.13 ms) or a work or
thread p99 by more than 0.5 ms (the widest span is burst's 0.37 ms, leaving out the outlier). Because one run can
still move a p99 far (`follow: rest` by 1.74 ms in run 3), a single run past either line counts only once a second run
confirms it. Worst frames are read only as hitch or no hitch.

Runs on different days disagree by more than that. Task 8's single world run, on the same game code a day earlier and
also paced at 165 Hz, had alike medians (0.48 to 0.78 ms of work) but rest-segment work p99s of 1.7 to 1.9 ms and logic
p99s of 0.6 to 0.8 ms; here they are 3.8 to 4.6 ms (pan to labels) and 2.2 to 3.3 ms. The likely cause is the CPU's
clocks: under `powersave` and the `balanced` profile a mostly idle CPU clocks down between paced frames, and how far
depends on what else the machine is doing. This is unproven. So **each later chunk measures main's head and its own
branch in one sitting**, and judges its numbers against that pair (with the rule above) as well as against this table,
for the world and the arena runs alike.

**Beside the earlier, unpinned numbers.**

| Measurement | Earlier (unpinned) | Now (pinned, the three runs' range) |
|---|---|---|
| Chunk 3, damage-number burst, main thread | 0.43 ms median, 2.6 ms worst | the main thread's own work (its row, not *work*): 2.54 to 2.67 ms median, 7.22 to 8.26 ms worst |
| Chunk 4, 1,000 flying swords, whole frame at a volley's peak | 1.31 / 2.31 / 2.60 ms (median / p99 / worst) | work over the whole run 2.51 to 2.59 / 6.08 to 6.18 / 8.28 to 11.05 ms; frame 6.05 / 9.19 to 9.27 / 12.02 to 13.50 ms |
| Chunk 4b, swords with their seals, frame | ~685 µs median | work 2.55 to 2.64 ms median; frame 6.04 to 6.06 ms |

They aren't a regression check. The earlier numbers ran in a 720p window (tiled to whatever niri gave it) on 30
workers, and chunk 4's at a volley's peak. These run fullscreen at 2560×1440, four times the pixels, on 2 workers,
which moves the job system's chunks onto fewer threads (the main thread among them). They are also paced at 165 Hz,
which src/AGENTS.md's Gotchas found makes every zone three to five times slower than unthrottled. From here on, the
pinned numbers are the ones compared.

**The GPU's share: method and limits.** SDL_GPU has no timestamp queries, so the share is the CPU's wait on the GPU:
the main thread's `WaitForGpu` zone round `SDL_WaitAndAcquireGPUSwapchainTexture` (it returns once an earlier frame is
done and an image is free), summed over a segment and divided by its frame time. Its limits, as measured here:

- This host's Vulkan on Wayland (NVIDIA under niri) offers no immediate present, so `--unpaced` falls back to mailbox,
  and the compositor still paces every frame to the display: the frame medians sit at 5.96 to 6.16 ms, one 165 Hz
  interval, in every row. On a paced host the share is the wait to present, not GPU time (hence `paced` on every row):
  55 to 58 % in the arena runs and 69 to 90 % in the world say only how much of a 6.06 ms interval the CPU leaves idle.
- *Work*, hitches and the per-thread numbers leave the wait out, so they hold whatever the pacing. *Work* still
  includes `SubmitFrame`, the driver's own time at submit, and some of that may be waiting too.
- Frames over the interval (frame p99 7.7 to 9.6 ms, while work p99 stays under 6.2 ms) miss a refresh on the GPU's or
  the compositor's side; with no GPU timing this method can't say which.
- A GPU time proper needs an unpaced present (for example XWayland's immediate, `SDL_VIDEO_DRIVER=x11`) or a
  frame-time difference with the work shown and hidden, as chunk 4b did. Until one is measured, the GPU share is no
  basis for moving work onto the GPU (the backlog's GPU items).

**The 60 Hz lock seen during Task 8.** Two earlier runs, taken while the desktop was in use, locked at ~16.7 ms frames
though the log said 165 Hz; Task 9's runs, with the 165 Hz monitor focused, locked at 6.06 ms. The cause, proven at
finishing: the game read its display once, right after creating its window, and niri then placed the window on the
focused output. A three-second windowed run logged `display: 2560x1440 at 165.1 Hz` (DP-3) at creation, then the window
landed on the focused 59.95 Hz DP-2. Fixed on this branch: the window keeps the display it is on, reads it again when it
is shown, goes fullscreen, moves to another display or that display's mode changes, and logs it again, with the
display's name, whenever it differs (`platform::MoveToDisplay`, tested with a window moving between a 165 Hz display
and two 60 Hz ones); `measure` takes the log's last display line, so its `paced` verdict uses the display the window
ended on. Task 9's single 165 Hz lines were right: its frames sat at one 165 Hz interval.

**The first budgets.** Each one gives its reason; a later chunk that breaks one says so in its Outcome, with why. The
budgets are ceilings that catch large regressions; modest ones are caught by measuring main's head and the branch in
one sitting (the rule under the noise). Since runs on different days disagree, a branch breaks a budget only when main,
measured in the same sitting, keeps it: when both break it, the machine moved, not the branch.

| Budget | Value | Measured here (the highest of the three runs) | Why |
|---|---|---|---|
| Hitches | none: no frame's work over 16.7 ms, the first 0.5 s of each segment included | none (the most work in a frame: 11.05 ms, swords) | the spec's anti-goal; 16.7 ms is one refresh of a 60 Hz display (`HITCH_MS`) |
| World, work median per segment | ≤ 2.5 ms | 1.58 ms (widest view, first 0.5 s) | the map's steady cost with nothing living on it yet; leaves 5b about 1 ms over the busiest view for fog, names and labels. A ceiling: a modest regression shows only in the same-sitting pair |
| World, work p99 per segment | ≤ 5.5 ms | 4.64 ms (widest view) | a third of a 60 Hz frame, so a machine three times slower than this one still keeps 60 Hz at the 99th percentile; also inside one 165 Hz interval here. A ceiling: a modest regression shows only in the same-sitting pair |
| World, logic p99 per frame | ≤ 5.5 ms | 3.25 ms (widest view) | a third of the logic's 60 Hz tick, the same three-times-slower margin, with room for 5b's travellers and time |
| Arena runs (burst, swords, seals) | work median ≤ 3.0 ms, work p99 ≤ 7.5 ms, logic p99 ≤ 0.5 ms | 2.67, 6.18 and 0.25 ms | the arena's content doesn't grow in phase 6, so these guard it against shared code (renderer, culling, jobs). Each sits above this sitting's spread with room for a day's drift (the world's medians held from one day to the next while its p99s moved, hence 3.0 ms and 7.5 ms, a tenth and a fifth over the highest); as ceilings they catch large regressions, and the same-sitting pair catches modest ones. The p99 is already above a third of a 60 Hz frame, which is what the backlog's GPU items (volley instancing) are for |
| Input latency | ≤ 50 ms | 23.8 ms | three 60 Hz frames: the press waits up to one logic tick, then a publish, a draw and a present; Task 8's runs (36.4 ms, and 49.4 ms at 60 Hz) spread wider than these three |
| World shimmer | ≤ 0.031 | 0.0309 | no worse than today; the 0.02 the labels and seals hold is the world map look's job (Task 5's ruling) |
| Frame time, worst frame, GPU share | no budget | — | paced, the frame time is the display's interval and the GPU share the wait to present; worst frames are single frames, ±1.9 ms between runs |

### Rulings made while building

The plan ledger's calls, with what stood behind them. Each one names what it costs if wrong.

**The world and its tiles.**

- **The world's tile shimmer is a regression guard for now (Task 5).** Its tiles shimmer at 0.0309 against the 0.02 the
  labels and seals hold: the hard, un-anti-aliased quad edges where two terrain kinds meet step a whole pixel at a time
  (the dark forest over the dark backdrop weighs most). The fix is the sweep-up backlog's world map look (blending
  between kinds, chunk 5b), which replaces those edges, so anti-aliasing today's edges here would be redrawn there. The
  reference case is named as a guard (bound 0.06, about twice the measure, as the seals' 0.02 is of their 0.011) and
  prints `world shimmer:` for `measure`; the backlog item carries the acceptance check "world shimmer ≤ 0.02". **Open
  for the user's review**; if wrong, a 5a task anti-aliases the tile edges (a shader or geometry change and new
  pictures).
- **Ground tiles and the painted ground skip the per-instance cull** (`DrawList::AddWhole`): culled by centre, a tile or
  the 1920×1080 ground overlapping the view's edge with its centre outside would pop. `TilesIn` already picks the
  tiles, so a publish holds exactly the tiles in view. At finishing the world stopped drawing the painted ground under
  its tiles: `Scene::PublishGround` draws it by default and the world's override draws only tiles.
- **The atlas packs in list order on aligned regions** (`art/sprites.toml` order; regions aligned to 2^(mips-1)
  texels). Every picture moved once, looked at, in that commit; the tiles commit then moved none. A list-order packer is
  stable under appends, and aligned regions make a sprite's mips independent of where it lands.
- **The crowd (the user, after wave B: "too many wanderers").** 20,000 walkers became 5,625: 1,600 spread about 4 a
  screen, the busiest gathering 2,900 (about 123 a screen round `WORLD_BUSIEST`), three towns of 375. The start is quiet
  country (5 walkers on its screen; no gathering within the widest view of it, `WIDEST_HALF_VIEW` read as half-extents,
  the strict reading), the busiest window at the widest view holds 2,025, and no screen holds over 146.

**The camera.**

- **The view culls by the scene shown** (`core::ShownChange`): while leaving, App still draws the old scene under the
  new change's number, so naming `sceneChange.change` would let the old scene's view cull the new one after the swap.
- **Pan and zoom hold while a calendar step's transition draws**; zoom applies at most one wheel notch a frame, the rest
  over the next frames, which keeps a notch inside `CULL_MARGIN` (about a quarter view per publish interval).
- **Edge pan arms only once the cursor has been seen inside the window clear of the edge band** since it last arrived
  or the window regained the focus: a cursor resting at an edge, or crossing the band on its way in, never moves the
  camera unasked (accessibility first). Cost if wrong: a player entering at the edge moves inward once.
- **The right stick's pan keeps the radial dead zone** that `Move()` and the device-in-use check use.
- **The scripted Move is held through the follow segment**, and the input latency is that press's first drawn movement.
  The cut to `WORLD_BUSIEST` for the zoom segments stays (panning there would muddle the zoom), and each segment's first
  0.5 s (`NEW_GROUND_SECONDS`) is its own row, the new-ground hitch measured rather than hidden.

**The measurements.**

- **A run whose log shows a pause, a camera path warning or an unfinished path is untrusted**: its row says why, a
  warning prints, and `measure` exits 1 (the table still prints). Never a gate, but a scripted run can't miss it.
- **The GPU share is annotated, not redefined**: it stays `WaitForGpu` over the frame time, marked `paced` where the
  frame median is within 3 % of the display's interval (then it is the wait to present).
- **Both Tracy exports use the unit separator** (`tracy-csvexport -s`): names, zone texts and messages may hold commas
  (MSVC's `__FUNCTION__` keeps template arguments), and the exports aren't quoted. `measure` starts the game through
  `xmake run` as `profile` does.
- **Main's head and the branch are measured in one sitting** (the noise above): Task 8's world run a day before Task 9's
  had p99s 2 to 4 times lower on the same code.
- **`measure` keeps its captures in `build/measure/<time>/`, not `build/profiles/`** as Goal 4 says: `profile` keeps the
  user's own recordings there, while `measure`'s captures, exports and logs are a run's working files, removed unless
  `--keep`, a run fails or a run is untrusted (its game log says why).
- **The world's shimmer is measured at the smallest zoom only** (Goal 4 asks for several): that is where the most tile
  edges sit near pixel size, over the busiest region whole, so it is the worst case for crawling edges; the labels' and
  seals' shimmer cases cover the other zooms for text and seals. The world map look's acceptance check measures it at
  several zooms (the sweep-up backlog).

**Process.**

- **A fix to this branch's own unmerged commit is folded into that commit before the PR**: main never had the bug, so a
  `fix:` line would describe nothing a player saw. A test-only correction stays its own `test:` commit.
- **A portal's step area matches its drawn seal**: an ellipse `PORTAL_RADIUS` across, flattened by `PORTAL_DEPTH_RATIO`
  (the shipped seals' view ratio, which a test ties it to), checked over the foot's whole path each tick so a dash can't
  jump the shallow ellipse. A circle fired 12 units outside the drawn rim above and below. Walking through each portal
  is covered by host and replay tests, as the game has no input injection.

### Known limits

- **A fast drag flick or a snap back from far** shows the view's edge uncovered for one or two publishes (60 Hz): a
  drag is 1:1 and can't be bounded without hurting its feel, and a glide would add motion and still outrun the margin.
- **The stress arena's crowd covers its portal seal** (its caption shows); moving the crowd would move every hash.
- **The portal's caption can draw over a walker's label**: captions have no overlap rules by design (chunk 3's ruling
  21, as the tablet's caption), and a walker crossing it is brief; none was seen (the closest label came 197 units from
  the caption over two minutes). **Open for the user's review**; if wrong, the label pass takes captions as fixed
  obstacles, a change to the labels' design.
- **A window that grows by more than `CULL_MARGIN` at once** (maximised, or made fullscreen) shows its new edges empty
  until the next publish culls to the new view, as a drag flick does.
- **The GPU share on this host is the wait to present** (no immediate present on NVIDIA under niri): no GPU time proper
  until one is measured unpaced (above).

### Fixed at finishing

- The display line named the wrong monitor (above), with `measure` reading the last one.
- Every reference picture's renderer draws seals in `seals.yaml`'s looks, as the game does: the arena and stress
  pictures logged the portal's seal key as missing in 23 cases, and the HUD and glow pictures now show the portal in the
  arena's corner.
- A snap back pressed as a scene change arrives is now tested where it would show (the change arriving while the camera
  holds still); App ends a drag after a paused frame's presses, not before them.

The final review (Opus, whole branch, with GLM alongside) found no Critical issue and one Important: Goal 2's "every
camera action by mouse, keyboard and gamepad" wasn't met, as zoom had no key and snap back no mouse button. Now `-` and
`=` zoom (by their place on the keyboard) and a middle click that drags nothing (it never strays past
`CLICK_SLOP_PIXELS`, 4) snaps back. Its Minors, all fixed: wheel notches during a calendar step's transition now wait for
its end as snap back does (App dropped them, against `CameraInput::still`'s promise); the world no longer loads the
painted ground it stopped drawing; the default `Scene::PublishGround` has its profile zone; an untrusted run's folder is
kept; `SceneTraits::view` says what reads it, and a test checks every scene that follows nothing starts its camera at
that view's centre; the two rulings and the known limit above.

GLM (glm-5.3-flash) found three more, all confirmed and fixed. Medium: wheel notches drained one a frame, so at 165 Hz
a flick zoomed nearly three notches inside one publish interval while a snapshot culled to the old view still showed
(up to about 43 ms), past `CULL_MARGIN`; zoom now runs at most `ZOOM_NOTCHES_PER_SECOND` (30, one every two publish
intervals) and one notch a frame, and the margin's comment reckons the real 43 ms (1.3 notches and a full-speed pan:
0.22 of the view each side, under the quarter). Low: the terrain test compared each tile's kind with itself; it now
pins four tiles' kinds and variants. Low: the input codes' bit guard still named `KeyC` after `-` and `=` were added
after it.
