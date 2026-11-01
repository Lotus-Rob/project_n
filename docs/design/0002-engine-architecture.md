# 0002: Engine architecture

| | |
|---|---|
| Status | **Draft**: for review; phase 1 of §16 implemented, the later phases not yet. Decisions marked *proposed* wait on the open questions (§15) and spikes (§14). Claims are checked against their sources (Appendix B) |
| Author | Rob G |
| Created | 2026-10-31 |
| Revised | 2026-11-01, phase 2 of §16 under way: §8.8 records how `shaders` meets SDL_GPU's layout (one file per stage, MSL from remapped SPIR-V, counts from SPIRV-Cross's reflection, binaries embedded with XMake's `utils.bin2c`). Earlier the same day, phase 1 of §16 implemented: §4's Doxygen groups live in `src/<module>/module.dox`; §6 records the measured cost of the precise wait. Earlier the same day, after the third and final review round: ticks while the simulation is frozen defined (prev = cur, a render clock that holds, a state counter for random numbers, replays per wall tick) (§6, §7.4); the adoption rule made exact (§6); a timeline epoch for loads and replays (§5); canonical order kept at saves, with a stable ID on every entity (§7.1); stale IDs from input ignored, not asserted (§12); the calendar-step transition published before the step (§7.3); job priorities between the logic thread and decoding (§7.5); texture layout owned by the manifest (§9). Earlier the same day, after a second review round: a display delay so snapshots switch without judder (§6), stable game IDs across domains, saves and replays (§7.1), separate wall and simulation ticks and one render clock for pictures, sounds and particles (§6), calendar steps as a blocking fork-join behind a transition (§7.3), props and meshes in the one y-sorted stream (§8.5), deterministic maths of our own (§7.4), a scene-loading protocol (§9), crash-safe saves (§7.6), a discrete GPU as the minimum spec after Intel's removal of D3D12 on Haswell (§2, §13), module-private headers for library wrappers (§4). 2026-10-31: world time in calendar steps with three NPC tiers and a fork-join job system from phase 3; before that, a first review round on scale (state mutated in place over EnTT, scheduling, three handover channels, instances interpolated on the GPU); the user's answers on the look, timing, threading, DragonBones, RmlUi, SDL3_mixer, SDL3_image, YAML, map zoom, colour space and source resolution; every claim checked against its source |
| Scope | The engine skeleton of a 2D game: the art direction's technical needs, module layout, threads and their handover, timing, simulation and NPCs, renderer (SDL_GPU), animation, UI, assets, input, audio, errors, memory. Proposes the answer to O2 from 0001 |

---

## 1. Goals and non-goals

### Goals

1. **A 2D game on one renderer for every preset.** Linux, Windows and macOS draw through SDL_GPU (the user's call, 2026-10-31), so the renderer differs by backend, not by code.
2. **The look of Tale of Immortal** (§2): ink-wash painting on paper, at a 1920×1080 reference resolution, scaled to any window; full-screen paintings and portraits are painted at up to 2× so they stay sharp at 1440p and 4K (§9).
3. **Scale:** tens of thousands of NPCs in a living world, and tens of thousands of sprites on screen (up to 100,000 culled instances a frame) in combat, without the frame rate or the tick rate collapsing, on the minimum spec of §13.
4. **Game logic that tests without a window or a GPU.** Systems, scheduling, input mapping, culling, sorting and camera maths are plain C++ that doctest runs in CI.
5. **Deterministic simulation within one binary.** The same executable, given the same saved state and inputs, produces the same ticks on any machine (§7.4): replays, reproducible bug reports, testable ticks. Not across compilers or platforms (the user's call, 2026-10-31).
6. **Rendering as fast as the machine allows,** decoupled from the tick rate and smooth through interpolation (the user's call, 2026-10-31).
7. **Two threads, plus a job system from phase 3** (§5, §7.5).
8. **Dependencies that point one way.** Game code never includes SDL; only the platform, renderer, UI and audio modules do, behind our own types.

### Non-goals (for this document)

- **Gameplay content:** which components and systems the game has, the world map's rules, combat rules, which needs and actions NPCs have, the screens built with the UI. A later doc, once the first mechanics exist. This doc fixes the machinery they live in.
- Networking, scripting and modding, localisation beyond rendering Chinese and English text.
- Mobile, web and consoles. Nothing here rules them out, but nothing is shaped for them.
- 3D.

---

## 2. Art direction: what it asks of the engine

What follows is what Tale of Immortal (鬼谷八荒) shows in all 15 of its Steam screenshots (1920×1080; two are the same image) and six gameplay images the user supplied (read 2026-10-31; Appendix B), not a description from memory. Its store text says only "based on Chinese mythology and cultivation"; its tags include "2D" and "Hand-drawn". The store's screenshots are mostly menus, dialogue and mini-games; the main gameplay is **real-time combat in top-down arenas** and **a grid world map**, and those set the engine's load.

**Combat** (the heaviest scenes):

| Seen | Technical consequence |
|---|---|
| A top-down, three-quarter view over painted ground textures, with rocks, trees, shrines and ice pillars that characters walk behind and in front of | **Depth by y:** inside the world layer, sprites sort by their foot position, so the sort key is layer, then y, then pipeline and texture (§8.5). Because y-order interleaves textures, units and props share **atlases or texture arrays** (`SDL_GPU_TEXTURETYPE_2D_ARRAY`) so a y-sorted run still batches. |
| Dozens of identical enemies at once (a pack of foxes), projectiles fired in rings and spirals, a boss many times the player's size | **Thousands of sprites a frame** through one instance stream, and **many entities a tick:** collision between circles through a spatial hash grid (pure `core` code, tested), not a physics library. |
| Bright additive effects that glow (orbs, lightning, a tornado, sword arcs, hearts), smoke and ink clouds | **Particles and trail meshes** drawn among the instances with additive blending, and a **bloom pass** (threshold, blur, add) on the offscreen scene. |
| Red afterimages trailing a moving character | **Afterimages:** earlier poses redrawn with fading alpha; the snapshot carries the poses. |
| Ice rings, water ripples and cracks on the ground; soft shadows under every unit | A **ground-decal layer** between the ground and the units; blob shadows as sprites. |
| Floating damage numbers in several colours, "Neutralize -5" and kill messages, health bars over every unit, a boss bar | **Lots of short-lived text:** numbers are instances from a pre-rendered digit strip per font (§8.6), not text objects; health bars are plain quads. |
| An ink-dark fog creeping in from the screen's edge | A **vignette overlay** (texture or shader). |
| Controller prompts on skills (R, ZR, B, X) next to keyboard ones ("Press Q to cast") | **Button glyphs per controller type** (`SDL_GetGamepadType`) and the keyboard's own (§10). |

**World map:**

| Seen | Technical consequence |
|---|---|
| A square grid of cells, each with painted props (trees, mountains, cities) over paper, a cloud border, a mounted character token moving cell to cell, a minimap | **A tile grid of layered sprites** scrolled by the camera, which also **zooms over a small range** (the user, 2026-10-31); the minimap is the map drawn a second time into a small target. Zooming out draws everything smaller than its source, so **every map texture has mipmaps** and is sampled with trilinear filtering. |
| Explored cells bright, unexplored ones greyed and darker | **Fog of war per cell:** a tint per cell in the instance colour, or a small visibility texture sampled across the map for soft edges. |
| Lightning, a flying ship and glowing arcs over the map | The same particle, trail and bloom machinery as combat. |

**Menus, dialogue and mini-games** (most of the store's screenshots):

| Seen | Technical consequence |
|---|---|
| Ink-wash painting on a paper texture, muted palette; large painted backgrounds, character illustrations and ink-drawn event scenes | **Smooth art, not pixel art:** linear filtering, art authored at a 1920×1080 reference resolution and scaled to the window (A11). **Large textures:** BC7 compression once texture memory adds up (§8.7); PNG through SDL3_image at first. |
| Dialogue and modal panels over a darkened, blurred scene; scrolls and panels with ink-brush edges; a relationship tree, card grids, a cultivation wheel | A **blur pass** on the offscreen scene (shared with bloom), and a large UI in RmlUi (§8.14). |
| A talisman mini-game where the player traces strokes with the mouse | **Geometry made at runtime:** brush strokes are meshes drawn among the instances (§8.5), not only quads. |
| Dense English and Chinese text, vertical Chinese calligraphy (top to bottom), text revealed letter by letter | **CJK text with vertical layout:** SDL3_ttf with HarfBuzz (`TTF_DIRECTION_TTB`) and a CJK font such as Noto Sans/Serif CJK (SIL OFL 1.1). Tale of Immortal ships English, Simplified and Traditional Chinese and Korean; ours renders English and Chinese (§1). |

Not visible and not stated: how characters and bosses are animated (stills can't show it). The map zooms a little (the user's call; AQ4). Animation uses DragonBones (the user's call, §8.13).

Tale of Immortal itself is Windows-only and asks for DirectX 11 and an Intel HD 4400 at minimum. That GPU can't be ours: Intel disabled D3D12 on Haswell graphics, the HD 4400 included, from driver 15.40.44.5107 (November 2021) as a security mitigation, and Haswell never had a Windows Vulkan driver (Intel's covers 6th-generation chips and newer), so SDL_GPU has no backend on it with current drivers. The minimum spec is a **discrete GPU of the GTX 960 or RX 470 class** (the user's call, 2026-11-01; §13).

---

## 3. Decisions at a glance

| # | Decision | Rationale |
|---|---|---|
| A1 | **Modules are XMake static libraries:** `core`, `data`, `platform`, `gfx`, `ui`, `audio`, `game`, plus the `project_n` executable (§4). A module links only the modules below it; `game` links only `core`. | The build enforces the dependency direction, and game tests link no SDL. |
| A2 | **Libraries stay inside their module:** SDL in `platform`, `gfx` and `audio`; SDL_GPU, SDL3_image, SDL3_ttf and DragonBones in `gfx`; RmlUi in `ui`; SDL3_mixer in `audio`; yaml-cpp in `data`. Public headers are third-party-free; a module's own `internal/` headers may include its libraries, and nothing outside the module includes them (§4; the user's call). `core` and `game` see libraries only through `core`'s facade headers (log, profile, ECS, jobs). | Game logic stays testable and portable; each library stays replaceable in one module; shared wrappers (like SDL deleters) have a home. |
| A3 | **Ticks at a fixed 60 a second; rendering uncapped.** Each snapshot carries every drawn item's previous and current transform; the main thread adopts it after a small display delay and the GPU interpolates (§6; the user's call). | Game speed never depends on the GPU; motion is smooth at any refresh rate and doesn't judder with varying tick times. |
| A4 | **Two threads, after "poor man's threading"** ([Evan Todd, 2016](https://etodd.io/2016/01/12/poor-mans-threading-architecture/)): the main thread owns the window, events, the GPU, the UI and audio; a logic thread owns the game state and runs the ticks. Besides immutable data loaded at start (the asset manifest) and the job system's worker pool (§7.5), they share only a handover object with **three channels**: newest-wins state, an accumulating event queue, an accumulating input queue (§5). | One lock per exchange instead of locks everywhere. State may be skipped; events and input never are. |
| A5 | **State is mutated in place by systems** in a fixed order, over EnTT, partitioned into domains; everything that crosses a domain, a save, input or a replay uses **stable game IDs**, never EnTT handles (§7.1). | Copying a 25–50 MB world every tick would be 1.5–3 GB/s for nothing; EnTT handles are per registry and per run. |
| A6 | **The simulation is scheduled:** per-system rates, per-entity time slicing, three NPC tiers. **World time advances in calendar steps** (days, months) when the player acts, travels or skips, as in Tale of Immortal; a step runs as one blocking fork-join over the world behind a short transition, and only combat and nearby NPCs run in real time (§7.3, §7.7; the user's call). | Tens of thousands of NPCs become affordable; every real-time tick stays inside its budget; determinism holds. |
| A7 | **A small fork-join job system from phase 3** (§7.5; the user's call): a worker pool and a parallel-for over fixed-size chunks; the logic thread forks a system's chunks, works on them itself, and joins. Game code follows its rules from phase 1. | One thread fits the first budgets with no headroom (about 4–9 ms of an 8 ms budget per 16.7 ms tick); results stay identical however many workers run. |
| A8 | **SDL_GPU replaces SDL_Renderer.** Vulkan on Linux, D3D12 on Windows, Metal on macOS. | One API with a modern backend per platform and custom shaders. It also ends SDL_Renderer's backend varying between builds and sessions (0001 §13.1 recorded OpenGL with GCC on Wayland, OpenGL or Vulkan with Clang on Wayland, Vulkan on X11): SDL_GPU's backends are Vulkan, D3D12 and Metal only. |
| A9 | **Sprites are instances culled, sorted and packed once per published tick** by the logic thread into the snapshot; the main thread uploads them once and draws them every frame; the vertex shader expands and interpolates them. Props, units, meshes and particles share one y-sorted stream (§8.5). | The main thread's per-frame work stays roughly constant at 100,000 sprites; everything in the world sorts by depth together. |
| A10 | **Shaders are HLSL, compiled ahead of time into committed binaries** for SPIR-V, DXIL and MSL (§8.8, S8). *Proposed:* DXC and SPIRV-Cross from conda-forge on Linux and Windows; macOS uses the committed binaries. | One source language for three backends; no shader compiler in the game, and none needed on macOS, where conda-forge has no DXC or SPIRV-Cross. |
| A11 | **Art is placed in 1920×1080 reference coordinates** and scaled to the window, keeping its aspect ratio (§8.10). | One coordinate space whatever the window. |
| A12 | **Skeletal animation through DragonBones for heroes, dialogue characters and bosses; baked sprite sheets or meshes for hordes** (§8.13; the user's calls). Poses are evaluated on the main thread at the render clock. | MIT, renderer-agnostic core; hundreds of armatures would cost 5–25 ms; Spine's licence conflicts with an AGPL game (Appendix A). |
| A13 | **UI through RmlUi**, fed by **view-model subscriptions**: the UI subscribes to the views it shows, the logic fills only those into the snapshot (§8.14; RmlUi the user's call). | The UI can show any part of a large simulation without the snapshot carrying the whole state. |
| A14 | **An immutable asset manifest with stable IDs**, built before the logic thread starts; scenes load through a protocol with decoding on workers and budgeted uploads behind a loading screen (§9). Images through SDL3_image. | The logic reads asset metadata without sharing mutable state; IDs survive saves and replays; loading never freezes the window. |
| A15 | **Input is read as actions, not keys**; the main thread picks entities against the frame it displayed (§10). | Rebinding and gamepads need no game changes; clicks hit what the player saw. |
| A16 | **Audio through SDL3_mixer**, driven by the event queue at the render clock (§11; the user's call). | Every sound a tick starts plays once, in step with the picture. |
| A17 | **Settings and data files are YAML** (§9; the user's call). *Proposed:* yaml-cpp in the `data` module, whose exceptions are caught at our parse functions. | yaml-cpp is in xmake-repo; rapidyaml isn't. |
| A18 | **Fallible operations return `std::expected<T, Error>`; broken invariants hit `PN_ASSERT`** (§12). | Errors are visible in signatures and cheap. |
| A19 | *Proposed while this doc is a draft:* **Linux: bundle everything XMake builds; take only drivers and desktop libraries from the system** (§12.1, answers O2). | Matches what the S4 build already does, and what players' systems must provide anyway. |
| A20 | **The minimum spec is a discrete GPU of the GTX 960 or RX 470 class** (§13; the user's call). | Tale of Immortal's HD 4400 has no SDL_GPU backend on Windows with current drivers (§2). |

---

## 4. Module layout

```
src/
  core/      types, log, profile, ecs and jobs facades, deterministic maths, assert, RNG,     EnTT, enkiTS (facades only)
             tick clock, handover, spatial hash grid, the frame contract (instances,
             sort keys, packing, events, actions, view models, stable IDs, asset IDs,
             manifest, settings and game-data types)
  data/      parses YAML settings and game data into core's structs                            yaml-cpp
  platform/  window, events -> input, time, paths                                               SDL
  gfx/       GPU device, swapchain, textures, shaders, instance and mesh renderers, text,      SDL_GPU, SDL3_image, SDL3_ttf,
             camera, post passes, DragonBones posing, the drawing API the UI renders through   DragonBones
  ui/        RmlUi context, its render interface and font engine (both on gfx's API), view      RmlUi
             models
  audio/     sound and music playback                                                          SDL3_mixer
  game/      components, systems, the schedule, NPC tiers, publishing                          EnTT (through core)
  main.cpp   loads data and the manifest, starts the logic thread, runs the main thread's loop
shaders/     HLSL sources and their committed compiled binaries (A10)
assets/      runtime assets and their manifest (storage: O5)
```

| Module | May use | Exposes |
|---|---|---|
| `core` | the standard library, the Quill, Tracy, EnTT and job-system facades | value types, pure functions, the handover (§5), **the frame contract**: plain-data types every other module speaks |
| `data` | `core`, yaml-cpp | parse functions filling `core`'s settings and game-data structs, returning `std::expected` |
| `platform` | `core`, SDL | `Window`, `InputState`, `Clock`, `Paths` |
| `gfx` | `core`, `platform` (for the window), SDL, SDL_GPU, SDL3_image, SDL3_ttf, DragonBones | `Renderer`, and a small API for the UI (compiled geometry, textures, scissor, transform, text shaping and glyph atlases) |
| `ui` | `core`, `gfx`, `platform`, RmlUi | `Ui` (load documents, feed input, bind view models, record UI draw data for `gfx`) |
| `audio` | `core`, SDL, SDL3_mixer | `Audio` |
| `game` | `core` only | `Simulation` (state, schedule, `Tick`, `Publish`, `Save`, `Load`) |

- **The frame contract lives in `core`**: sprite instances, sort keys and packing, batch ranges, events, actions, view models, stable and asset IDs. `game` writes it, `gfx`, `ui` and `audio` read it, and `game` links nothing but `core`, so game tests link no SDL. Settings and game-data types live there too, so `data` fills them and `game` reads them without linking `data`. Today's `src/core/sdl_log.cpp`, which includes SDL, moves to `platform` in phase 1.
- **Headers** (the user's call, 2026-11-01): public headers include only the standard library and our own headers, as src/AGENTS.md requires today. Headers under `src/<module>/internal/` may include the module's libraries (SDL in `platform` and `gfx`, RmlUi in `ui`, ...), so wrappers shared by several of the module's `.cpp` files have a home; `lint` fails when anything outside the module includes them. Facades (`core/ecs.hpp`, `core/jobs.hpp`, the existing log and profile ones) stay the only public headers that include a library (`FACADES`, 0001 §7.1 rule 27). 0001 §7.1 rule 27, src/AGENTS.md and `lint` change with phase 1.
- Each module is an XMake `static` target with `add_deps` on the ones below it. A wrong header-only include would still compile and link (there is one `src/` include root), so `lint`'s include-direction check is the guard, and it names the rule when one is broken.
- `pch.hpp` stays one per target kind (game, tests); the module libraries share the game's.
- Each module gets a commit scope (`core`, `data`, `platform`, `gfx`, `ui`, `audio`, `game`) in `SCOPES`.
- Each module defines its Doxygen group in `src/<module>/module.dox`, and each of its headers joins it with `@ingroup` (0001 §7.1 rule 25).
- Tests mirror modules: `tests/unit/core/` (handover, tick clock, RNG, maths, sort keys, packing), `tests/unit/gfx/` (camera maths, pass plan; no device), `tests/unit/game/` (systems, scheduling, tiers, save and load over a real EnTT registry), and a headless benchmark target that ticks a large world without a window (§16).

## 5. Threads and the handover

| Thread | Owns | Loop |
|---|---|---|
| Main | the window, SDL events, the GPU device and every GPU call, the UI, audio calls, the camera, DragonBones posing | poll events, send input, take the newest snapshot, adopt it when due, upload it, play due events, draw, present; as fast as possible |
| Logic | the game state (the EnTT registries), the schedule, the stable-ID allocator | take input, run the ticks due, publish; 60 ticks a second |
| Workers | nothing permanently; chunks the logic thread forks (high priority), scene decoding the main thread requests and save writing (low priority) | the job system's pool, from phase 3 |
| Quill's backend | log formatting and writing | already exists |
| SDL's own | audio mixing (SDL3_mixer), others SDL starts | not ours |

**Why the main thread renders:** SDL requires events on the main thread (strictly on macOS), and SDL_GPU acquires the swapchain only "from the thread that created the window" (SDL_gpu.h). The article renders on the main thread for the same reason ("If you touch OpenGL or anything within a mile of the windowing system from another thread, the universe explodes"). §8.12 explains why no separate render thread, render command queue or deferred rendering.

**The handover** is one object in `core` with three channels and one mutex, held only for pointer swaps:

| Channel | Direction | Semantics | Carries |
|---|---|---|---|
| State | logic → main | **Newest wins.** Skipping a snapshot is harmless, because each one is complete. | Sprite instances (previous and current transforms) and their stable IDs (CPU-side, for picking), batch ranges, mesh and particle placeholders, armature clips, the camera's follow target and cull rectangle, view models, the static layers, the manifest version, the wall tick, the simulation tick, whether the simulation advanced, the timeline epoch and the scheduled time |
| Events | logic → main | **Accumulating.** Each tick appends, each event stamped with its simulation tick and timeline epoch; the main thread swaps the whole queue out every loop iteration, drawing or not, and holds each event until the render clock reaches its tick (§6). The logic caps cosmetic events per tick (at most a few sounds of one kind; 10,000 bullet hits are not 10,000 sounds) and coalesces them if the main thread stops draining, so the queue can't grow without bound. | Sounds and music requests, visual-particle bursts, damage numbers, messages, camera shake, asset residency hints |
| Input | main → logic | **Accumulating.** The main thread appends; a burst of ticks takes presses and releases once and repeats held state for every tick (§10). | Actions with arguments (including picked stable IDs and UI decisions), view subscriptions, the camera's view rectangle on the world map, "scene resident" reports, hot-reload requests |

- **Buffers:** the state channel has four snapshot buffers: one the logic writes, one finished and waiting, one the main thread has taken but not yet adopted (§6), one it is drawing. With tick run times below D, at most one snapshot is ever taken but not yet due, so four suffice; a buffer the main thread stops drawing goes back to the handover's free slot. They keep their capacity between ticks (clearing resets sizes, as the article does with its command lists), so the steady state allocates nothing. At 100,000 instances of 40 bytes, a buffer's instance array is about 4 MB.
- **Data that rarely changes** (view models, the static layers, the manifest) is held in the snapshot as `std::shared_ptr<const T>`: an unchanged part reuses the same pointer in every buffer, and a new one is allocated only when it changes. Rotating buffers can't hold "rewritten only on change" data any other way, since the buffer being written is several publishes old.
- **Only the logic thread touches the state, only the main thread touches the GPU, UI and audio.** The handover has a threaded test (one writer, one reader, many ticks; every event delivered once and in order; snapshots in order) run under ThreadSanitizer by `check --sanitizers`.
- **The UI reads the simulation only through view models** (§8.14) and acts on it only through input; a decision's effect is visible about 2–3 ticks later, which UI interactions tolerate.
- **The timeline epoch** changes whenever the simulation's timeline restarts (a save loaded, a new game, a replay started). When it adopts a snapshot of a new epoch, the main thread drops pending events and live particle bursts of the old one. Events of a newer epoch than the drawn snapshot's wait until a snapshot of that epoch is adopted, and ticks are only ever compared within one epoch, so nothing from either timeline plays early, late, or with an elapsed time measured on the other's clock.

## 6. Main loop and timing

**Counters:** the **wall tick** counts every tick the schedule runs. The **simulation tick** advances only when the game isn't paused, and is what real-time systems, rates, time slicing, animation clips and particle spawns key on. The **state counter** advances on every tick that changes the state (simulation ticks, calendar steps, UI decisions applied while paused) and is what random numbers key on (§7.4), so two calendar steps with no simulation tick between them still draw different numbers. The game calendar is state, advanced by calendar steps (§7.7). All three are saved.

**Frozen ticks:** while the simulation tick is frozen (paused, a calendar step's transition, scene loading), wall ticks keep running, but only input, command apply, UI decisions and calendar steps do work; systems keyed on the simulation tick are skipped. Such a tick publishes with previous = current for every instance and "advanced = false", and the render clock holds at the simulation tick instead of sawing back a tick every 16.7 ms. Gameplay presses that arrive while frozen (including during a calendar step) wait in the handover, are taken by the next frozen tick and dropped there on purpose (the UI consumes what it needs); held state applies once the simulation resumes. Replays record this like any other tick's input.

**Logic thread:**

```
next = start
loop until stop requested:                         // std::jthread; the stop token is checked in the wait
  wait until next                                  // SDL_DelayNS until ~1 ms before, then SDL_DelayPrecise
  behind = ticks between next and now, plus 1
  if behind > MAX_CATCH_UP (3): next = now − 2 × TICK; behind = 3     // drop wall time: the game slows briefly
  repeat behind times:
    input = handover.TakeInput()                   // presses and releases once; held state and axes every tick
    record the tick's effective input for replay
    simulation.Tick(input, events)                 // in place; events stamped with the simulation tick
  simulation.Publish(snapshot)                     // only the last tick of the burst (§7.2)
  handover.Publish(snapshot, events)
  next += behind × TICK
```

**Main thread:**

```
loop:
  poll events (SDL_WaitEventTimeout(TICK) while hidden or minimised)
    -> ui first, then actions into handover input (with picked stable IDs, §10)
  take handover events into the pending list (dropped if from an older epoch than the drawn snapshot's)
  if held exists and due(held): adopt it          // adopt before replacing
  if nothing held and a newer snapshot is waiting: take it (held); if due, adopt it
  if visible: acquire the swapchain texture (§8.3)  // may block; time is sampled after it
  alpha = drawn.advanced ? clamp((now − drawn.scheduledTime − D) / TICK, 0, 1) : 1
  renderTick = drawn.simTick − 1 + alpha        // one clock for pictures, poses, particles and sounds
  of the pending events in the drawn snapshot's epoch: play those whose tick <= renderTick,
    drop cosmetic ones more than 30 ticks behind it; events of a newer epoch wait for its first snapshot
  if hidden or minimised: continue              // no acquire, upload or draw; the clock and events still advance
  if drawn hasn't been uploaded: upload its instances
  draw at alpha; submit
where due(s) = now >= s.scheduledTime + D       // unclamped
```

- **Ticks are fixed** (A3) at 1/60 s; `game` never reads a clock.
- **Interpolation needs one snapshot.** Every entity that draws keeps its previous transform, copied at the start of each tick; spawning or teleporting sets previous to current. The snapshot carries both, so the main thread never matches objects between snapshots, and skipped snapshots can't distort motion. Rotation interpolates along the shorter arc.
- **The display delay D** (*proposed:* 10 ms, a little above the tick budget, then set from the measured 99th percentile of tick run time plus publish) is what keeps motion smooth while tick run times vary. A snapshot's `alpha` runs from 0 to 1 over the tick that starts D after its scheduled time; the main thread **adopts a new snapshot only when it is due** (`now` at least its scheduled time plus D, tested unclamped), i.e. exactly when the previous one reaches alpha 1, so the picture moves continuously from one snapshot into the next. A held snapshot that is due is adopted before a newer one is taken, so a newer, not-yet-due snapshot never displaces a due one. Late adoptions (a snapshot arriving after it was due) are counted in the statistics. Without D, the old snapshot would freeze at alpha 1 for as long as the next tick took to run, and the new one would then jump ahead by the same amount: a judder of up to half a tick, every tick. The picture is therefore TICK + D (about 27 ms) behind the simulation: the accepted price of smooth motion (the user accepted one tick on 2026-10-31; D is the reviews' correction).
- **Scheduled time** is a tick's place on the schedule (`start + k × TICK`, moved by rebases), on a clock both threads share, so a tick's own run-time jitter never reaches the picture. If a tick takes longer than D, alpha holds at 1 until the next snapshot arrives and then jumps: visible only on overload, which the budget (§7.3) prevents. `now` for alpha is read after the swapchain texture is acquired, since that call blocks for a variable time.
- **Overload policy:** after a hitch, at most 3 ticks run back to back; beyond that, wall time is dropped and the game slows down briefly instead of fast-forwarding. Dropping wall time drops no ticks, so determinism holds. Only the burst's last tick is published, so motion visibly jumps by the ticks caught up: inherent when the simulation is late. A Warning log reports sustained overload (ticks late for more than a second); a calendar step's transition tells the clock its stall is expected (§7.3).
- **One render clock:** `renderTick` (the drawn snapshot's simulation tick − 1 + alpha) drives DragonBones poses, GPU particles' elapsed time, and when events play. Events are stamped with their tick and held until the render clock reaches it, so sounds, bursts and damage numbers happen with the picture, not a tick and a half ahead of it.
- **While hidden or minimised** the main thread waits on `SDL_WaitEventTimeout(TICK)` (not an indefinite wait), so events keep draining and snapshots keep being adopted without drawing; cosmetic events more than 30 ticks behind the render clock are dropped instead of played in a burst later, while residency hints are coalesced, never dropped. When the main thread can't run at all (Windows' modal resize loop, a debugger, an acquire blocking on an occluded window), the logic's per-tick caps and coalescing (§5) bound what accumulates.
- **Waiting:** `SDL_DelayPrecise` waits "busy waiting if necessary" (SDL_timer.h), so the logic thread sleeps with `SDL_DelayNS` until about a millisecond before the tick and lets `SDL_DelayPrecise` finish. Measured in phase 1 (release build, Linux, Wayland, 2026-11-01, per-thread CPU time from `/proc/<pid>/task/<tid>/schedstat` over 10 s): the logic thread used 2.9 ms of CPU, 0.03 % of a core or about 5 µs a tick, so the precise wait costs next to nothing; the uncapped main thread, clearing through SDL_Renderer, used 85 % of a core (an input to S10).
- **Rendering is uncapped.** SDL_gpu.h: VSYNC "will always be supported"; IMMEDIATE ("lowest latency option, but tearing may occur") and MAILBOX ("no tearing is possible ... the pending image is replaced by the new image") "may not be supported on certain systems", so the renderer asks `SDL_WindowSupportsGPUPresentMode` and takes MAILBOX, then IMMEDIATE, then VSYNC. Vsync and a frame cap become player settings, off by default. S10 measures whether uncapped rendering takes enough CPU power to slow the logic thread; if it does, a default cap is reconsidered.
- **Pause:** pausing stops the simulation tick; the game pauses itself when the window loses focus or is minimised (a setting).
- **The timing logic is pure `core` code** (`core::TickClock`: given the schedule and the time, the ticks due, rebases, alpha with D, adoption and the render clock), tested without threads or a clock.

## 7. Simulation: state, systems and scheduling

### 7.1 Entities, identity and state

- **EnTT** (MIT, in xmake-repo at our locked commit; the version chosen when added, add-dependency skill) holds the game state: components as plain structs, views and groups for tight iteration (the user's call, 2026-10-31). `core/ecs.hpp` is its facade.
- **State is partitioned into domains**, each its own registry: the active region (the arena or the player's surroundings) and the world (NPCs, sects, relationships, the map), plus smaller ones as needed.
- **Stable game IDs:** every entity in the state carries a `StableId` component: a `u64` from a counter in the state, never reused, saved with it (bullets included: random numbers and time slicing key on it). Each domain keeps a map from stable ID to its EnTT entity, and the state keeps a map from stable ID to domain, so commands and relationships can reach an entity in another domain. **EnTT handles never leave a domain or a tick:** saves, input (picks), view models, relationships, commands, random-number keys and time-slice hashes all use stable IDs. An NPC moving between domains keeps its stable ID and gets a new EnTT entity, and nothing that refers to it notices.
- **No pointers in state:** components refer to other entities by stable ID and to assets by asset ID (§9).
- **Canonical order:** EnTT's iteration order is deterministic for an identical history of operations, but not a function of the state alone (destroying an entity moves the last one into its place). So whenever the state is saved, or a recording starts, the live domains are sorted by stable ID in place (a save's sort is part of the deterministic timeline: save requests arrive as input, autosaves are triggered by the state, such as every few calendar steps, and a replay performs the same sort at the same tick without writing a file), and loading builds them in that same order: the game that continues after a save and the game loaded from it iterate identically. Between those moments order follows history, which replays repeat exactly. Nothing orders by EnTT entity value or by address.
- **Structural changes are deferred** (EnTT's registry isn't safe for concurrent structural changes): entities are created and destroyed in the apply step after each system, never inside a chunk.

### 7.2 Systems and the tick

- A **system** is a function over explicit data: `void MoveProjectiles(View<const Velocity, Position>, const TickContext&, Commands&)` (illustrative). Its signature says what it reads and writes; it reaches nothing else (no globals, no singletons, no thread-local state).
- `Simulation::Tick(const Input&, Events&)` runs the systems in **one fixed order**, mutating state in place. Purity lives at the system level, where it matters for tests and jobs; a test builds a state, ticks it and asserts.
- **Cross-entity writes are commands:** a system that changes another entity (damage, opinion, relationship) emits a command instead of writing directly; the apply step after the system merges all commands sorted by (target, source, emission order), single-threaded, then performs deferred creations and destructions.
- **Per-tick memory** comes from arenas reset each tick (one per worker); hot systems don't touch the global heap.
- **Publishing** is a separate step after the burst's last tick: cull, sort and pack instances (§8.5), fill subscribed view models (§8.14). The logic keeps a placeholder entity for each live normal-blended particle burst it started (at the origin's foot y, for the burst's lifetime), so the burst sorts into the world layer as one item. The cull rectangle comes from the logic itself in combat (the camera follows an entity whose position the logic knows) and from the main thread's view rectangle on the world map, extended to the zoomed-out extent so a zoom within one frame never shows a gap.

### 7.3 Scheduling

- **Rates:** each system registers with a rate (every k simulation ticks) and an offset. Combat movement and collision run every tick; decisions of active-tier NPCs run less often.
- **Time slicing per entity:** a system at rate k processes the entities whose `(simTick + hash(stableId)) mod k` is 0, spreading the work evenly and deterministically.
- **Calendar steps are a blocking fork-join behind a transition.** When world time advances (the player travels, acts or skips), the logic thread freezes the simulation tick and **publishes the transition first** (a view model); at the start of the next tick it runs the step over the world domain on the job system, working on chunks itself and checking the stop token between chunks; the clock is told the stall is expected, so no overload is logged and no catch-up follows. Input arriving meanwhile waits in the handover and is handled as on any frozen tick (§6); the main thread caps its frame rate during transitions and loading, leaving the cores to the step. The window stays responsive throughout (the main thread keeps drawing the last snapshot and the transition), as in Tale of Immortal's "Skip Month". Because the player is waiting anyway, nothing has to run against a world that is being changed.
- **Background jobs that overlap ticks** (borrowing a domain while the game runs) aren't part of the skeleton. If one is needed later (say, long pathfinding), its contract comes with it: a const copy of its inputs at the start, results applied as commands at a fixed tick, commands aimed at what it owns queued until then, saves waiting for it.
- **Budget:** a real-time tick may use **8 ms of its 16.7 ms** on the minimum-spec CPU (§13), leaving headroom for hitches and for sharing the CPU with the main thread. The headless benchmark (§16, phase 4) fails if the combat case exceeds it.

### 7.4 Determinism (same binary, any machine)

Within one executable, the same state and inputs give the same ticks on any machine (the user's call, 2026-10-31):

- fixed system order; commands merged by (target, source, emission order); canonical storage order (§7.1); no iteration over unordered containers in the simulation; time only from tick counts;
- **counter-based random numbers:** `random(seed, stateCounter, stableId, purpose, drawIndex)` (§6) (a hash such as SquirrelNoise or Philox), with the seed in the state; never `std::*_distribution`, whose results differ between standard libraries, and never one shared stream, which parallel chunks would draw from in scheduling order;
- **our own maths for transcendental functions:** `core::math` provides `sin`, `cos`, `exp`, `pow`, `atan2` and the like, computed the same way everywhere. The platform maths libraries make the same binary diverge between machines: glibc's libm on Linux is linked dynamically and changes between versions and with the CPU features it detects at runtime; the UCRT on Windows is linked statically (`/MT`, 0001 rule 13) but still picks code paths by CPU (FMA3); macOS's libsystem_m is dynamic and changes with the OS version. `sqrt` and the basic operations are exact under IEEE 754 and stay. `lint` fails on `std::sin` and the like in `core` and `game`, `core::math`'s own implementation excepted;
- no fast-math; the floating-point environment (rounding, flush-to-zero and denormals-are-zero) is set and checked on the logic thread and every worker at start, since it is per thread and a library can change it;
- **replays:** a recording starts from a saved state, canonical at that moment (§7.1), and logs the effective input of every wall tick (frozen ticks included, since UI decisions change state while paused) plus a hash of the state every 60 wall ticks, so a divergence is caught at the second it starts. Starting a replay is a new timeline epoch (§5).

Across compilers and platforms results may differ (code generation); cross-platform replays or lockstep multiplayer would need fixed point or more of our own maths, which nobody has asked for.

### 7.5 The job system

**When and what** (the user's call, 2026-10-31): a small fork-join job system arrives in phase 3, with the first hotspot (publishing 100,000 instances) and a benchmark to prove the gain. Workers number the hardware threads minus two, at least one; the logic thread works on chunks too. It offers a **parallel-for over fixed-size chunks**: the logic thread forks a system's chunks and joins them before the next system. No task graph, no fibers, no work running across ticks (§7.3) until profiling or a feature asks. *Proposed:* enkiTS (zlib, in xmake-repo at v1.12: task sets with a parallel-for), behind `core/jobs.hpp`, or our own of a few hundred lines if enkiTS's chunking can't be fixed independently of the worker count; phase 3 decides.

**Where it is used:** publishing (cull, sort, pack, by region), combat movement and collision (by grid region), calendar steps (by NPC chunk, §7.7), and, for the main thread, decoding a scene's assets during loading (§9); saves are written by a worker (§7.6). **Priorities:** the logic thread's chunks run at high priority and its joins wait only on work of that priority, so a waiting logic thread never picks up an 80 ms image decode mid-combat; decoding and saving run at low priority (enkiTS has task priorities).

**Its rules, which game code follows from phase 1 so nothing is rewritten when it arrives:**

1. Systems declare reads and writes in their signatures.
2. One system's work splits into **fixed-size chunks** (independent of the worker count), so floating-point sums happen in the same order whatever the machine.
3. Cross-entity writes go through commands merged in a fixed order.
4. Random numbers depend on (seed, state counter, stable ID, purpose, draw index), never on scheduling.
5. Per-worker arenas; no global allocation in hot systems; no structural changes inside a chunk.

Results are therefore identical with 1 worker or 32, which a test checks (the same world ticked with the pool at 1 and at N workers, state hashes compared).

### 7.6 Saving and loading

- A save is the state's domains (components keyed by stable ID), the stable-ID counter, the seed, the calendar, and the wall tick, simulation tick and state counter (§6), in a **versioned format** (a version per domain, migrations for old saves).
- **Saving:** `entt::registry` isn't copyable, so the logic thread sorts the domains into canonical order (§7.1) and serialises their storages into a memory buffer, then a worker compresses and writes it. For a large world that is tens of milliseconds, so saves (manual and automatic) happen on a frozen tick, as an expected stall like a calendar step's.
- **Saves can't be corrupted by a crash:** the worker writes a temporary file, flushes it, then renames it over the old save. Shutdown waits for a save in progress.
- **Loading** goes through the scene protocol (§9) behind a loading screen: it rebuilds each domain from the save in canonical order, maps stable IDs to new EnTT entities, and starts a new timeline epoch (§5).
- **Hot reload** of data files in debug builds: the main thread (or a worker) parses the new files and builds a new manifest and data, the logic swaps them in at a tick (input), and the snapshot carries the new manifest version for the main thread to switch at adoption; the replay log records the data's hash, so a replay can tell it needs the same files.

### 7.7 NPCs

How a living world of NPCs is simulated (world time in calendar steps, the user's call, 2026-10-31):

| Tier | Who | Simulated |
|---|---|---|
| **Active** | NPCs in the active region: the arena, the world map cells around the player, a scene the player is in | Every tick by real-time systems (movement, combat, animation); their decisions time-sliced (about 10 a second each) |
| **World** | Every other individual NPC | Only at **calendar steps**: each NPC takes its actions for the day or month that passed |
| **Aggregate** | Distant sects, regions and populations the player hasn't met | As groups with summary numbers (strength, members, resources), at calendar steps; individuals are created from a group when the player comes near |

- **A calendar step** (§7.3) runs over the world domain in chunks: each NPC scores the actions available (utility AI over actions defined in YAML data, so new behaviour is data, not code), picks one, applies its effects on itself, and emits commands for effects on others (relationships, sects, items), merged deterministically after the chunks join. Rough scale: 50,000 NPCs at about 20 µs each is about 1 s of CPU, about 0.5 s on the minimum spec's 2 workers plus the logic thread, with the main thread's frame rate capped during the transition.
- **Moving between tiers** happens at a tick: the NPC's components move between domains under its stable ID, and its real-time state is derived from its world state (and back), so the tiers never disagree. **NPCs the player has met never fold back into an aggregate**, so relationships, memories and view models that name them never dangle; only individuals the player has never met can.
- **Data layout:** hot components (position, tier, current action) are separate from cold ones (biography, inventory, history), so a step reads only what it needs; **relationships are one edge table** (source, target, kind, values, by stable ID), kept sorted by source so one NPC's relationships are contiguous; NPCs on the world map are indexed by cell.
- **History doesn't grow forever:** each NPC keeps recent events in full and older ones as summaries, so a 100-year world stays inside §13's budget.
- **What this doc fixes and what it leaves:** the tiers, the step mechanics, identity, determinism and data layout are engine machinery decided here; which needs, goals and actions NPCs have is game design for the gameplay doc.

## 8. Renderer (SDL_GPU)

Every SDL_GPU behaviour cited here is from `SDL_gpu.h` of SDL 3.4.16, the version we pin.

### 8.1 What it must handle, and budgets

| Load (from §2 and §6) | Design answer |
|---|---|
| Combat: tens of thousands of y-sorted sprites, up to 100,000 culled instances | Instances culled, sorted and packed per published tick; uploaded once per adopted snapshot; expanded and interpolated on the GPU (§8.5) |
| Rendering uncapped | The main thread's per-frame sprite work is a handful of draw calls with a uniform each: constant, whatever the sprite count |
| Glow, blur, vignette, distortion | A short fixed chain of post passes on an offscreen scene target (§8.4) |
| Much text, a large UI | SDL3_ttf's atlases; RmlUi with retained geometry in its own pass (§8.6, §8.14) |
| Tests without a GPU | Everything up to the GPU calls is pure functions over plain data (§8.2, §8.9) |

*Proposed budgets* for the minimum spec (§13), measured from phase 3 with the stress arena and adjusted then (all estimates from typical costs until then):

| Budget | Target |
|---|---|
| Logic: cull, sort, pack per published tick | 2–3 ms at 100,000 instances on one thread (a radix sort of 100,000 key/index pairs is about 0.5–1 ms); less on the job system |
| Main thread: sprite work per frame | under 0.5 ms, independent of the sprite count |
| Upload | one instance array per adopted snapshot: about 4 MB at 100,000 instances, about 240 MB/s at 60 a second, independent of the frame rate |
| Draw calls in combat | under 100 a frame, including meshes splitting batches (consecutive mesh items merge; additive ones never split) |
| Overdraw | at most about 15× the screen at 1080p (about 31 million blended pixels a frame, a few milliseconds of fill on a GTX 960-class GPU); 100,000 sprites of 32×32 pixels would be about 50×, so crowds that large are mostly small sprites (8–16 px bullets) and the stress test says how many fit |
| Frame rate | 60 at 1080p at high settings on the minimum spec; uncapped above that on anything faster |

### 8.2 Layers inside `gfx`

```
Renderer::Draw(...)              the only entry point: the drawn snapshot, alpha, the render clock, UI draw data, the camera
  features/                      instances, meshes (DragonBones, strokes, trails), text, GPU particles, post, UI drawing
    each: Upload (when its data changed) -> Draw (render pass)
  frame/                         command buffer, pass plan, submission, statistics
  resources/                     residency of textures, atlases, arrays, fonts; pipelines; the upload queue
  internal/gpu                   thin owners of SDL_GPU objects; the only code calling SDL_GPU
```

- **Owners of SDL objects** live in `gfx/internal/` (§4): `std::unique_ptr` with custom deleters. SDL_GPU objects are released through their device (`SDL_ReleaseGPUTexture(device, texture)` and the like), so their deleter holds the device: `struct GpuDeleter { SDL_GPUDevice* device; void operator()(SDL_GPUTexture*) const; ... };`, and the device outlives everything it made. Other SDL objects (the window, fonts, the text engine) use a stateless deleter, one template for all: `template <auto Destroy> struct Deleter { void operator()(auto* p) const { Destroy(p); } };`, e.g. `std::unique_ptr<SDL_Window, Deleter<SDL_DestroyWindow>>`. SDL "frees the given texture as soon as it is safe to do so", so there is no deferred-deletion queue of ours.
- The expensive preparation of sprites (cull, sort, pack) happens on the logic thread at publish; its pure pieces (sort keys, packing, batch ranges) live in `core`'s frame contract with tests.
- The UI draws through `gfx`'s small UI API (§8.14); nothing outside `gfx` calls SDL_GPU.

### 8.3 A frame, step by step

1. **Acquire** a command buffer (`SDL_AcquireGPUCommandBuffer`), then the swapchain texture with `SDL_WaitAndAcquireGPUSwapchainTexture`. It blocks only when the allowed frames in flight are all pending ("the default value ... is 2"; 1 to 3 allowed), so with MAILBOX or IMMEDIATE the GPU's throughput is the frame rate's only limit. The header recommends this call over `SDL_AcquireGPUSwapchainTexture`, whose non-blocking use "will cause memory usage to grow". Frames in flight stay at 2.
2. If the swapchain texture is NULL, **submit the command buffer empty**: cancelling isn't allowed once a swapchain texture has been acquired ("It is an error to call this function after a swapchain texture has been acquired").
3. **Copy pass, only for what changed:** the renderer remembers which snapshot it last uploaded; if the drawn one differs, its instance array goes into a cycled transfer buffer and on into a cycled storage buffer, capped at the buffer's maximum (overflow is logged and the rest culled). Main-thread meshes and this frame's share of texture uploads go the same way.
4. **Render passes** in the order of the pass plan (§8.4), with `alpha` and the render clock pushed as uniforms.
5. **Submit** (`SDL_SubmitGPUCommandBuffer`); presentation follows automatically.

### 8.4 Pass plan

A fixed list of passes, assembled each frame from what the screen needs, not a general render graph: the passes are few and known.

| Pass | Target | Reads | Notes |
|---|---|---|---|
| World | scene colour | instances, textures | Layers in order: ground, ground decals, world (y-sorted), effects (additive), overhead (health bars, damage numbers). |
| Distortion | scene | a scene copy | Only while a distorting effect is on screen. The world pass ends before the overhead layer, the scene is copied (`SDL_BlitGPUTexture`, which "must not be called inside of any pass"), distortion sprites sample the copy at offset coordinates, then the overhead layer draws undistorted. |
| Minimap | minimap target | the minimap texture, tokens | World map screens only: a texture of the whole map rendered once when the map changes (cells, props, cities), plus the tokens the snapshot lists, not instances culled to the camera; the UI samples the result. |
| Bloom | bloom chain | scene | Threshold into half size, down to 1/32 and back up, adding at each step: fragment passes, no compute. Off on low settings. |
| Backdrop blur | blur chain | scene | Only while a modal UI is open over the scene; reuses the bloom chain's downsampling. Blur over other UI panels is RmlUi's own (§8.14). |
| Final | swapchain | scene, bloom, blur, UI | **One render pass** ("Beginning a new render pass is relatively expensive"): composite (bloom, vignette, mapping the scene's range to the display's, upscaling below render scale 1), then the UI, then the debug overlay. |

- **The scene target** is `R16G16B16A16_FLOAT`, so stacked additive effects exceed 1.0 and bloom has headroom; it is on SDL's list of universally supported colour-target formats, and a GTX 960-class GPU has the bandwidth for it at 1080p. Low settings use `R8G8B8A8_UNORM` (also universal) and skip bloom. Linear blending in 8 bits bands in dark ink washes, so the scene format and the colour-space comparison (below) are decided together.
- **Sizes** follow the swapchain's pixel size (`SDL_GetWindowSizeInPixels`) times a **render-scale** setting (default 1.0). Targets are recreated on `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`, not every frame.
- **Colour space:** decided by comparing both side by side on the same painted test scene in phase 3 (the user's call, 2026-10-31). Gamma space (`SDL_GPU_SWAPCHAINCOMPOSITION_SDR`, blending the sRGB-encoded values) matches the painting tools the art is made with; linear space (sRGB texture formats, `SDR_LINEAR`) is physically correct but changes how semi-transparent ink, mist and glows look. The choice lives in one place (texture and target formats, the swapchain composition) so the comparison is a switch; gamma is the interim default.

### 8.5 Sprite instances

- **What the logic publishes:** after the burst's last tick, the publish step culls entities to the cull rectangle plus a margin, computes each one's sort key, sorts (key, index) pairs with a stable radix sort, and writes a packed **instance array** in that order, a parallel array of their stable IDs (CPU-side only, for picking), and **batch ranges** (runs with the same pipeline and texture binding).
- **Instance layout, 40 bytes:** previous and current position (4 × `f32`), size (2 × `f16`), previous and current rotation (2 × `f16`), texture region (4 × `u16`, normalised), texture-array layer and flags (2 × `u16`), premultiplied colour (4 × `u8`). Scale, colour and region snap at the tick; position and rotation interpolate. Shaders read the 16-bit fields as `uint` and unpack them (`f16tof32`), never as native 16-bit types, which SDL_GPU's baseline doesn't guarantee.
- **One y-sorted stream for everything in the world:** units, props (trees, rocks, shrines, map buildings), the world map's player token, baked horde animation, and normal-blended GPU particles (smoke, ink) all sort by foot y in the world layer, so anything can stand behind anything. A DragonBones mesh, a trail or a normal-blended particle burst is a **placeholder item** in the stream with its sort key; the main thread ends the batch there, draws it, and continues. Consecutive placeholders merge into one draw, and additive meshes (most trails) draw in the effects layer, where order doesn't matter, so they never split the world layer.
- **Texture layout is fixed offline:** the manifest assigns each sprite its scene, texture-array page, layer and region (§9), so the logic packs instances from the manifest alone. World-layer sprites are always resident while their scene is (the scene protocol, §9); placeholders apply only to the UI and the mesh path.
- **Static layers** are only the layers below the world layer, which never interleave with what moves: the ground, ground decals, the world map's cells and cloud border. They are packed when the scene or map changes, held in the snapshot as a shared pointer (§5), and uploaded once.
- **Sort key:** layer, then depth (in the world layer the foot y, quantised relative to the view; elsewhere submission order), then pipeline, then texture. The radix sort is stable, so equal keys keep their order and overlapping sprites never flicker.
- **Drawing:** the vertex shader reads instances from a storage buffer (`SDL_BindGPUVertexStorageBuffers`, created with `SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ`), builds the quad's corners from the vertex index, and interpolates by `alpha`. Each batch is one `SDL_DrawGPUPrimitives` with `first_vertex` and `first_instance` at 0 and the batch's first instance pushed as a uniform, because SDL warns that those parameters "are NOT compatible with built-in vertex/instance ID variables in shaders". No vertex or index buffer exists for sprites.
- **Meshes** (posed DragonBones armatures, brush strokes, trails, text) are written by the main thread when they change, with a static `u32` index buffer for quads built once.

### 8.6 Text

SDL3_ttf's GPU text engine (`TTF_CreateGPUTextEngine`, in the 3.2.2 release xmake-repo offers) rasterises glyphs into atlas textures on first use and returns, per atlas, vertices and indices (`TTF_GetGPUTextDrawData`) for the mesh path. HarfBuzz is built in (the xmake package's `harfbuzz` config, off by default, turned on) for shaping and vertical layout (`TTF_DIRECTION_TTB`). Text from the logic is data, not strings to shape: (font, string ID or number, colour, position). **Damage numbers** don't create text objects: each font has a pre-rendered strip of digits and signs, and numbers become instances, so hundreds a frame cost what sprites cost.

### 8.7 Resources and memory

- **Per-frame buffers** (transfer buffers, the instance storage buffer, mesh vertex buffers) are written with `cycle = true`: SDL's buffers "effectively function as ring buffers on internal resources", rotating to an unbound one when a frame in flight still uses the current one, so the renderer needs no fences of its own. Render targets use the colour target's `cycle` flag only where the pass doesn't load the previous contents (it applies when "load_op is not LOAD"); bloom's upsampling and the final pass load.
- **Textures are cycled whole** ("when cycling a texture, the entire texture will be cycled"), so uploads into atlas pages and texture arrays use `cycle = false` and happen at scene load, behind the loading screen (§9).
- **Atlases come from an offline asset step:** packed with gutters (about 2^mips pixels, so neighbours don't bleed in at low mip levels), mips baked, compressed to BC7 (`SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM`) where it pays. Mip generation on the GPU (`SDL_GenerateMipmapsForGPUTexture`) is for uncompressed textures made at runtime; whether it works on BC formats is checked in S9 rather than assumed. Until the asset step exists, PNGs through SDL3_image with mips generated at load.
- **Texture arrays per scene:** the sprites and props of one scene go into the pages of one `SDL_GPU_TEXTURETYPE_2D_ARRAY` texture built at scene load, so a y-sorted run of different sprites stays one batch. Every page has the same size and format and is resident while the array is: sixteen 4096² pages with full mip chains are about 1.33 GiB uncompressed, about 341 MiB as BC7, so page count and size come from the memory budget (§13).
- **BC7 on macOS:** Apple's feature tables list BC support for all Apple7 and Apple8 macOS devices (M1, M2) and say every Metal 3 and 4 device has at least the Apple7 column's texture capabilities; the renderer still asks `SDL_GPUTextureSupportsFormat` and falls back to uncompressed.
- **Residency** belongs to the main thread: scene textures load through the scene protocol (§9) and are resident whenever their scene is; UI and mesh-path assets referenced by snapshots and residency hints load with a per-frame upload budget and draw as placeholders until ready.

### 8.8 Pipelines and shaders

- **Few pipelines, made up front:** instances and meshes per blend mode (normal, additive, multiply), text, UI, GPU particles, and the post passes. SDL_GPU pipelines are immutable objects (`SDL_CreateGPUGraphicsPipeline`), so all are created at start-up for the formats in use: no compile stalls mid-game.
- **Blend states for premultiplied alpha:** normal = `ONE`, `ONE_MINUS_SRC_ALPHA`; additive = `ONE`, `ONE`; multiply = `DST_COLOR`, `ONE_MINUS_SRC_ALPHA`.
- **Uniforms:** the camera's matrix, `alpha` and the render clock per pass, the batch's first instance per draw (`SDL_PushGPUVertexUniformData`), post parameters through the fragment equivalent.
- **Binding layout:** SDL requires a fixed order per stage. HLSL declares vertex-stage resources in `space0` and uniform buffers in `space1`, fragment-stage ones in `space2` and `space3`, which is SDL's order for DXIL; DXC maps register spaces to SPIR-V descriptor sets of the same number, which is SDL's order for SPIR-V (sets 0 to 3). SDL's SPIR-V layout lists sampled textures without separate samplers, so textures and samplers must come out as **combined image samplers** for Vulkan (`[[vk::combinedImageSampler]]` with explicit `[[vk::binding]]`); MSL from SPIRV-Cross must come out with `[[texture]]`, `[[sampler]]` and `[[buffer]]` indices in SDL's order. Both were S8's risks: DXC maps each register to the binding of its number, so numbering `t` registers in SDL's order (sampled textures, storage textures, storage buffers) gives Vulkan's and D3D12's layouts at once; Metal numbers `[[buffer]]`s uniform buffers first, so `shaders` compiles a second SPIR-V with every register remapped (`-fvk-bind-register`) and SPIRV-Cross takes those bindings as Metal's indices (`--msl-decoration-binding`). Vertex inputs use `TEXCOORD0`, `TEXCOORD1`, ... as SDL's D3D12 backend assumes.
- **Resource counts** per shader come from SPIRV-Cross's reflection of the SPIR-V (`--reflect`), which `shaders` also checks against the layout above, written by the shaders command into a generated header beside the binaries, never by hand.
- **Toolchain (A10):** `shaders/*.hlsl` compiled by a new shaders command in `tools/project_n.py` with DXC (`directx-shader-compiler` on conda-forge: linux-64 and win-64, no osx-arm64) to SPIR-V and DXIL (shader model 6.0, which SDL's D3D12 backend takes alongside DXBC 5.1), then SPIRV-Cross (`spirv-cross`: likewise no osx-arm64) from SPIR-V to MSL. Each stage is a file of its own, `shaders/<name>.<stage>.hlsl` (shared code in `.hlsli`), as DXC wants every declared resource remapped. The binaries are committed in `shaders/bin/` and embedded in the executable by XMake's `utils.bin2c` through the generated header; a manifest hashes every source and output, and `lint` fails when a source changed and its binaries didn't. SDL_shadercross does the same in one tool but has no release and no conda-forge package (S8 decides).

### 8.9 Device, debugging and testing

- **Device:** `SDL_CreateGPUDeviceWithProperties` with the shader formats we ship (SPIR-V, DXIL, MSL) and debug mode in debug builds, letting SDL pick the backend; the log reports which (`SDL_GetGPUDeviceDriver`) at Info. The `SDL_HINT_GPU_DRIVER` hint overrides it for debugging.
- **What the backends require of players:** **Vulkan** 1.0 with `VK_KHR_swapchain`, `VK_KHR_maintenance1` and the features `independentBlend`, `imageCubeArray`, `depthClamp`, `shaderClipDistance`, `drawIndirectFirstInstance`, `sampleRateShading`; **D3D12** on Windows 10 or newer with feature level 11_0 and resource binding tier 2; **Metal** on macOS 10.14 or newer with Apple Silicon or an Intel Mac2-family GPU. The minimum spec (§13) meets all of them; our macOS minimum is already 13.
- **Debugging:** every pass and feature is wrapped in `SDL_PushGPUDebugGroup` and every SDL_GPU object gets a name (the `..._CREATE_NAME_STRING` properties), so RenderDoc, PIX and Xcode captures read as our passes. SDL_GPU has no GPU timestamp queries, so GPU time comes from those tools; CPU time from Tracy zones.
- **Statistics** per frame and per tick (instances published and culled, batches, draw calls, uploaded bytes, texture and target memory, time per system, overflow) go to Tracy as plots through the profile facade (a plot macro beside `PN_PROFILE_ZONE`) and to the debug overlay.
- **Tests:** culling, sort keys, packing, batch ranges, interpolation maths, the render clock, camera maths and pass-plan assembly are doctest cases without a device. With Mesa's software Vulkan in CI (S11), fixed scenes render offscreen, are read back (`SDL_DownloadFromGPUTexture`) and compared with committed reference images within a tolerance. D3D12 and Metal are checked by hand (S9).

### 8.10 Reference resolution and camera

- Art is placed in 1920×1080 coordinates. The camera maps world coordinates to the scene target with an orthographic projection, scaling by the smaller of the two axis ratios so the aspect ratio holds; a wider window shows more of the scene, and the UI anchors to the window's edges.
- **The main thread owns the camera.** On the world map it pans and zooms at the frame rate (zoom clamped to a small range, *proposed* 0.75× to 1.5×; the user's call) and sends its view rectangle through input; in combat it follows the interpolated position of the entity the snapshot names, and the logic culls around that entity itself (§7.2).
- The projection maths is pure and tested.

### 8.11 Particles and trails

Gameplay-relevant things (projectiles, hazards) are entities in the simulation and draw as instances. Purely visual particles are **stateless on the GPU:** a burst event carries (spawn tick, origin, velocities, lifetime, curve), uploaded once when the render clock reaches it, and the vertex shader evaluates each particle's position and fade from the render clock; nothing is simulated per frame on the CPU, so the cost doesn't grow with the frame rate, and no compute is needed. Additive particles draw in the effects layer; normal-blended ones (smoke, ink) sort into the world layer (§8.5). Trails are meshes the main thread extends from interpolated positions.

### 8.12 No render thread, command queue or deferred rendering

Asked 2026-10-31 whether these would help or are needed for this look:

- **Deferred rendering** (a G-buffer, then a lighting pass) pays off when many dynamic lights fall on lit surfaces. Nothing in §2 is lit that way: the art is painted with its light in it, and every glow is additive blending plus bloom. If the game later wants dynamic light (a lightning flash lighting the arena, darkness with torches), the forward answer is a light-accumulation target that light sprites draw into and the composite multiplies in: one more pass, no G-buffer.
- **A separate render thread** can't own presentation: SDL_GPU acquires the swapchain only on the thread that created the window, and SDL needs that to be the main thread for events. The main thread already is the render thread, since the logic runs elsewhere. Other threads may record command buffers of their own ("you should only access a command buffer on the thread you acquired it from"), which the job system can use for uploads.
- **A render command queue** in the article's sense (low-level GPU operations for a render thread to replay) would sit beneath what we need. The snapshot is already a command list at the level the renderer sorts, batches and interpolates, and SDL_GPU's command buffers already defer the GPU work until submission.
- **An audio thread of our own** wouldn't help either: SDL3_mixer already mixes on SDL's audio thread, and the lost-sound problem was the handover's, solved by the event channel (§5).

### 8.13 Skeletal animation (DragonBones) and hordes

- **Who uses what** (the user's call, 2026-10-31): DragonBones for heroes, characters in dialogue and bosses, a few dozen at a time; **hordes use baked animation:** sprite sheets or meshes baked offline from the source animation, played as instances whose texture region changes per frame of the clip.
- **Runtime:** DragonBonesCPP (MIT). Its core (`DragonBones/src/dragonBones`: armature, animation, model, parser, ...) has no renderer includes; engines plug in through adapters (the repository has Cocos2d-x, SFML and an Android one). Ours turns an armature's slots into meshes for `gfx`, taking the SFML adapter as the model. It comes in as our own package in `xmake/repo/packages/d/dragonbones/`, pinned to a commit (add-dependency skill).
- **Threading:** the logic carries only (clip ID, start simulation tick, speed) per animated entity; the main thread evaluates the pose at the render clock and skins it. DragonBones therefore runs only on the main thread, which also sidesteps any global state the runtime keeps (S14 checks whether it has static pools). Gameplay timings (hit frames, clip lengths) come from the asset manifest, never from the runtime, so the simulation doesn't depend on `gfx`.
- **Status:** the core's last substantive changes date from 2018 (the runtime reports data version `5.6.300`); a JNI renderer was added in July 2025. The editor's status couldn't be confirmed: the C++ README recommends "DragonBones Pro" at dragonbones.com, which didn't answer; the newer JS runtime recommends "LoongBones", apparently the editor's successor. S14 checks that today's editor exports data the 5.6 runtime reads.

### 8.14 UI (RmlUi) and view models

- **Library:** RmlUi 6.3 (MIT, in xmake-repo at our locked commit), documents in HTML/CSS-like RML and RCSS, on the main thread.
- **View models:** the UI subscribes, through input, to the views it shows ("NPC 1234's details", "sect roster, page 3", "the relationship tree around NPC 1234", by stable ID). At publish, the logic fills only the subscribed views into the snapshot as plain structs behind shared pointers (§5), each with a version, replaced only when its data changed. RmlUi's data models bind to those structs. Sorting, filtering and scrolling a 5,000-row list happen on the main thread over the view data; only decisions ("recruit NPC 1234") go back, as actions. View models are part of the frame contract in `core`.
- **Rendering:** RmlUi's render interface is retained (geometry compiled once, then drawn by handle). `ui` implements it on `gfx`'s small UI API (compiled geometry in static GPU buffers, textures, scissor rectangles, transforms), drawn **in submission order**, outside the sprite sort key. RmlUi compiles geometry and creates textures while rendering, and SDL_GPU can't upload inside a render pass, so `ui` records UI draw data (including new geometry and textures) and `gfx` uploads it in the frame's copy pass before drawing it. RmlUi ships an SDL_GPU backend (new in 6.2: "Currently supports basic rendering and transforms"; no clip masks, filters or shaders), our reference. Clip masks need a stencil attachment, and SDL guarantees only "Either (but not necessarily both!) D24_UNORM_S8_UINT or D32_FLOAT_S8_UINT", so the format is queried. RmlUi 6's layers and filters (e.g. a blurred backdrop behind a panel over other panels) are implemented later on our post passes.
- **Fonts:** RmlUi's default font engine is FreeType, replaceable by a custom one (`RMLUI_FONT_ENGINE` `none`); its documentation doesn't mention CJK or shaping. *Proposed:* a font engine of our own in `ui`, on `gfx`'s text API (SDL3_ttf stays in `gfx`, fonts are loaded once), so UI text and game text shape alike, vertical Chinese included (S13 tries FreeType first).
- **Build:** the xmake package's `rtti` config stays on, as our build already has RTTI and exceptions; RmlUi 6.3's changelog says RTTI is now used only for debug asserts and type names.

## 9. Assets and data

- **The manifest:** before the logic thread starts, `main` builds an **immutable manifest** of every asset: a stable asset ID (an index fixed by the manifest file, so saves and replays survive new assets) and the metadata the simulation needs (sprite sizes, hitboxes, scene contents, each sprite's texture-array page, layer and region as the offline asset step packed them, animation clip lengths and hit frames). The logic only reads it; it changes only by hot reload (§7.6), as a new manifest with a new version.
- **Loading a scene** (entering an arena, a region, the world map):
  1. the logic asks for scene X (an event) and pauses the simulation tick behind a loading view model;
  2. the main thread has the scene's files decoded on workers (SDL3_image, off the main thread);
  3. uploads (texture arrays, static layers) run on the main thread under a per-frame budget behind a loading screen, so the window never freezes;
  4. the main thread reports "X resident" through input, and the logic resumes.
- **Smaller assets** (a portrait, a sound) referenced by snapshots or residency hints load on demand with a per-frame budget and show placeholders until ready.
- **Source resolution** (the user's call, 2026-10-31): sprites and props are painted for 1080p; full-screen paintings and portraits at up to 2×, which mips scale down at lower resolutions. That costs up to 4× the memory for those textures, inside §13's budget.
- **Location:** `SDL_GetBasePath()` + `assets/`; `artifact` copies the folder next to the binary (into `Contents/Resources` in the macOS bundle).
- **Images:** SDL3_image 3.4.0 (zlib, in xmake-repo; the user's call), in `gfx`'s `.cpp` files and its internal headers only; PNG until the offline asset step (§8.7) produces packed, compressed atlases.
- **YAML (A17):** settings in the per-user data folder; game data (sects, techniques, items, regions, NPC actions) in `assets/`, parsed by the `data` module into plain structs once at load. yaml-cpp 0.9.0 (MIT, in xmake-repo) reports parse errors by throwing (`YAML::ParserException`), so the `data` module's parse functions catch them and return `std::expected`; nothing else sees an exception. rapidyaml (MIT, "exception-agnostic", error callbacks) would avoid that but isn't in xmake-repo.
- **Storage** of large binary assets in Git is 0001's O5, still open; with painted art it matters early.

## 10. Input, camera and picking

- `platform` reads keyboard, mouse and gamepads (`SDL_Gamepad`, which maps controllers to one standard layout; Tale of Immortal lists full controller support and shows controller prompts beside keyboard ones) on the main thread. Prompts show the glyphs of the device used last, chosen by `SDL_GetGamepadType`. Events go to the UI first; what it doesn't consume becomes actions.
- A **binding table** maps physical inputs to actions defined by `game` (an `enum class Action`, in the frame contract). **Every tick** gets the held state and axes as of the latest input; **presses and releases** are latched and delivered once, to the first tick that runs (a press and release between two ticks arrive as both). In a catch-up burst, the later ticks keep the held state, so movement doesn't stop. Each tick's effective input is what the replay log records.
- **Picking:** a click is resolved on the main thread against the frame it displayed (the drawn snapshot's instances at that alpha, using the parallel array of stable IDs; meshes by their bounds), and the action carries the stable ID, so a click on a moving enemy hits the one the player saw.
- **Stroke tracing** (§2) sends the mouse path in reference coordinates as action arguments.
- Default bindings are code; rebinding is saved to the YAML settings file later. Tests construct actions directly; no SDL event in a game test.

## 11. Audio

SDL3_mixer 3.2.4 (zlib, in xmake-repo; the user's call) in the `audio` module, called on the main thread, which plays each sound and music request from the event channel when the render clock reaches its tick (§6): once each, in step with the picture, whatever the frame rate. While the window is hidden, events still drain; cosmetic ones older than 30 ticks are dropped rather than played in a burst on return.

## 12. Errors and ownership

- **Expected failures** (a missing file, a device that can't be created, a shader that won't load, a YAML file that doesn't parse) return `std::expected<T, core::Error>`; the caller decides, logs once at the right level, and the log names the cause.
- **Stale IDs from outside the state are expected:** a pick or a UI decision may name an entity that died since the frame the player saw; the logic validates IDs arriving through input and ignores stale ones. Only a dangling ID inside the state is a bug.
- **Broken invariants** (a dangling stable ID inside the state, a structural change inside a chunk) hit `PN_ASSERT(condition, message)`: in debug and releasedbg it logs Critical and aborts so the debugger stops there; in release it compiles away unless the check is cheap and the alternative is memory corruption.
- **Exceptions:** our code throws none. The standard library's (`std::bad_alloc`) and yaml-cpp's (caught in `data`) are the only ones; anything that escapes ends at `main`'s handlers, or on the logic thread at the thread's entry, which logs Critical and asks the main thread to shut down.
- **Ownership:** SDL and library objects are owned by `std::unique_ptr` with custom deleters in the module's `internal/` headers (§8.2); public headers expose our own types.
- **Shutdown:** the main thread hides the window, requests a stop through the logic thread's `std::jthread` stop token (checked in its wait and between a calendar step's chunks), joins it, waits for a save in progress (§7.6), stops the job system, then destroys ui, gfx (the GPU device last within it), audio and platform in that order.

### 12.1 Linux libraries (answers O2; proposed while this doc is a draft)

| Comes from | Libraries |
|---|---|
| Our binary, statically | SDL3, SDL3_image, SDL3_mixer, SDL3_ttf, HarfBuzz, FreeType, RmlUi, DragonBones, EnTT and enkiTS (header-only or static), yaml-cpp and every other XMake package; libstdc++ or libc++ (0001 D3) |
| The player's system, loaded by SDL at runtime | GPU drivers and the Vulkan loader (`libvulkan.so.1`), Wayland and X11 client libraries, xkbcommon, libdecor, audio (PipeWire, PulseAudio or ALSA) |
| The player's system, linked | glibc at the baseline set in 0001 (O1, §5.2); the simulation doesn't depend on its maths library (§7.4) |

- If `libvulkan.so.1` or a Vulkan driver is missing, the game stops with a Critical log and a message box saying so, not a crash.
- **Wayland over XWayland** stays preferred (`main.cpp`'s hint, now in `platform`). Uncapped MAILBOX or IMMEDIATE presentation sidesteps the `fifo-v1` problem while the window is visible; S10 checks the rest.
- `artifact`'s runtime-dependency check keeps failing a build that links a library from the Pixi environment. SDL3_image, SDL3_mixer and SDL3_ttf must not pull a codec or font library from the system (S12).

## 13. Minimum spec and memory budgets

**Minimum spec** (the user's call, 2026-11-01; *proposed* details):

| Part | Minimum |
|---|---|
| GPU | GTX 960 or RX 470 class: D3D12 resource binding tier 2 and Vulkan on Windows, Vulkan on Linux; 2 GB of video memory |
| CPU | 4 hardware threads (so the job system has 2 workers besides the main and logic threads), Skylake-era performance |
| Memory | 8 GB |
| OS | Windows 10, a Linux with glibc at 0001's baseline (O1), macOS 13 on Apple Silicon |

Intel integrated graphics below 6th generation are out (§2); newer integrated GPUs may run at low settings (8-bit scene, no bloom, render scale below 1), not promised.

**Memory budgets**, measured and adjusted from phase 3:

| What | Budget |
|---|---|
| Process total | 4 GB |
| GPU textures and targets | 1.5 GB (texture arrays and atlases sized from this, §8.7) |
| Game state | 50 MB for a large world (detailed NPCs at about 2 KB, tens of thousands more as compact world-tier records and aggregates, relationships, sects, the map), plus one serialised copy while saving |
| Snapshot buffers | 4 × about 4 MB at 100,000 instances, plus shared view models and static layers |
| Per-tick arenas | 16 MB |

The statistics (§8.9) report each, so a broken budget shows up in Tracy before it shows up as a crash.

## 14. Spikes

| # | Spike | Passes when |
|---|---|---|
| S8 | **Shader toolchain** | DXC and SPIRV-Cross, pinned in `pixi.toml` for linux-64 and win-64, compile one HLSL shader (instances from a storage buffer, a sampled texture, `f16` unpacking) to SPIR-V with combined image samplers, DXIL and MSL that SDL_GPU accepts on all three backends; the binaries are committed and reproducible. Alternative if SPIRV-Cross's MSL doesn't fit SDL_GPU's resource layout: SDL_shadercross built from a pinned commit through an xmake package of our own. |
| S9 | **SDL_GPU on every target** | A sample drawing instances from a vertex-stage storage buffer runs on Linux (Vulkan, Wayland and X11), native Windows (D3D12 on minimum-spec hardware if available), macOS (Metal), and the Windows build under Wine or Proton. Records the present modes each offers, and whether mip generation works on BC7 textures. |
| S10 | **Presentation** | MAILBOX or IMMEDIATE renders uncapped on each backend without starving the logic thread; on a compositor without `fifo-v1` (niri) the sample neither freezes nor spins while hidden; vsync as a setting works too. |
| S11 | **GPU in CI** | Mesa's software Vulkan driver (`mesa-lavapipe` 26.2.6 on conda-forge, with `libvulkan-loader`, both for linux-64, win-64 and osx-arm64) lets CI run rendering tests headless, in a Pixi feature the game never ships with. |
| S12 | **SDL3 satellite libraries** | xmake-repo's libsdl3_image 3.4.0, libsdl3_mixer 3.2.4 and libsdl3_ttf 3.2.2 (with `harfbuzz`) build statically for all four presets from the Pixi environment with no codec or font library from the host (`check --container`), and the licences go into `THIRD_PARTY_NOTICES.md`. |
| S13 | **RmlUi** | RmlUi 6.3 builds for all four presets; a document with Chinese and English text renders through our render interface; a 5,000-row list bound to a view model scrolls smoothly; decides between its FreeType engine and our SDL3_ttf one. |
| S14 | **DragonBones** | Our package builds DragonBonesCPP for all four presets; an armature exported by today's editor (LoongBones or DragonBones Pro) loads in the 5.6 runtime and animates through our adapter; records whether the runtime keeps global state; one clip bakes to a sprite sheet for hordes. |

## 15. Open questions

| # | Question | Default if unanswered |
|---|---|---|
| O5 | From 0001: where large binary assets are stored (Git LFS or elsewhere). Painted art makes this early. | decide before the first real asset |
| O6 | From 0001: code signing before the first public build (Apple notarisation, a Windows code-signing certificate or Azure Trusted Signing). A release question rather than an engine one, carried here because 0001 deferred it to this doc. | releases unsigned, with instructions in their notes (`publish`; the user's call, 2026-10-31) |

Resolved (the user): Tale of Immortal's look; ticks at 60 a second with rendering uncapped (confirmed after the first review, against lockstep at 60 or 120); the threading model; DragonBones for heroes and bosses with baked hordes (was AQ1); RmlUi (was AQ2); the map zooms over a small range (was AQ4); gamma or linear blending decided side by side in a test scene (was AQ5); source art at 1080p for sprites and props and up to 2× for full-screen paintings and portraits (was AQ6); EnTT; determinism within one binary; world time in calendar steps; a fork-join job system from phase 3; SDL3_mixer; SDL3_image; YAML (all 2026-10-31); a discrete GPU of the GTX 960 or RX 470 class as the minimum spec, and module-private headers for library wrappers (2026-11-01).

## 16. Implementation phases (after go-ahead)

1. **Skeleton and threads:** the modules (A1) with the frame contract in `core`, and `sdl_log.cpp` moved to `platform`; module-private `internal/` headers with the `lint` rule, 0001 §7.1 rule 27 and src/AGENTS.md updated; EnTT behind `core/ecs.hpp` with stable IDs and canonical ordering; `core::TickClock` with wall ticks, simulation ticks, the state counter, frozen ticks, the overload policy, the display delay, adoption and the render clock; the timeline epoch; the handover with its three channels and its threaded test under ThreadSanitizer; the logic thread on a `std::jthread`; counter-based RNG keyed on the state counter; `core::math` and the floating-point environment check; the headless benchmark target; window and events in `platform`; SDL_Renderer still draws the clear colour, now uncapped.
2. **SDL_GPU:** S8, then device, swapchain and the clear-colour frame on SDL_GPU; SDL_Renderer goes. S9, S10 and S11 run on this build.
3. **Instanced sprites at scale, and the job system:** S12 for SDL3_image; a minimal manifest with the offline atlas step's page and layer assignments; the fork-join job system (§7.5) with priorities and its 1-versus-N-workers determinism test; cull, sort and pack at publish on the job system; the storage-buffer instance path with interpolation in the shader; props and meshes in the one y-sorted stream; texture arrays; the stress arena of **100,000 instances and a 10,000-bullet pattern**, profiled in releasedbg against §8.1 and §13; gamma and linear side by side for the user; a zoomable grid of map cells for mipmapping; reference-image tests in CI.
4. **Simulation and NPCs:** rates, time slicing, the three NPC tiers and moving between them under stable IDs; calendar steps as a blocking fork-join behind a transition; the headless benchmark's two gates: the **combat tick** (a horde, 10,000 bullets, publishing 100,000 instances) inside 8 ms, and a **month step for 50,000 world-tier NPCs** inside 0.5 s on the minimum spec's 2 workers plus the logic thread; save and load round trips (canonical order at save, crash-safe writing, loading through the scene protocol); replay of an input log, frozen ticks included, reproducing the same state hashes.
5. **Input, camera, assets:** bindings and actions with latching across bursts, gamepads and prompts, picking against the displayed frame, the camera on the main thread; the manifest; the scene-loading protocol with decoding on workers; `assets/` in `artifact` and the macOS bundle; the `data` module with YAML settings and one data table; hot reload of data in debug builds.
6. **UI and text:** S13; RmlUi with view-model subscriptions and a 5,000-row NPC list; SDL3_ttf's GPU text engine with a CJK font; digit strips for damage numbers; the backdrop blur.
7. **Animation, particles and audio:** S14; heroes on DragonBones posed at the render clock; a baked horde; GPU particles from burst events; SDL3_mixer playing from the event channel at the render clock.

Each phase is a branch through the normal review and pull request, and `check --container` passes at each step.

## Appendix A: alternatives considered

| Alternative | Why not |
|---|---|
| Lockstep at 60, as in the article | Simplest, but the frame rate is capped at 60 and a slow GPU slows the game itself down. |
| Lockstep at 120 | Doubles the cost of everything ticked each frame, still slows the game on slow GPUs, and 120 doesn't divide common refresh rates such as 144 Hz. |
| `Tick(const State&, const Input&) -> State` (the first draft) | Copies a 25–50 MB world every tick; determinism comes from fixed order and inputs, not from returning a new state. |
| Events in the snapshot (the first draft) | Lost when a snapshot is skipped, repeated when one is drawn several times. |
| Interpolating between two snapshots matched by object ID (the first draft) | Wrong across skipped snapshots, and a hash lookup per item per frame (5–10 ms at 100,000). |
| Building and uploading vertices every frame (the first draft) | 20–30 ms of main-thread work a frame at 100,000 sprites, and gigabytes a second of uploads at high frame rates. |
| Static props in a separate layer (the second draft) | Props couldn't interleave with moving units by depth. |
| EnTT handles as the state's IDs (the second draft) | Per registry and per run: they break across domains, saves and replays. |
| The system maths library in the simulation | Varies between glibc and UCRT versions and, on Linux, with the CPU, so the same binary diverges between machines. |
| Long jobs borrowing a domain across ticks for calendar steps (the second draft) | Needs a contract for everything that touches the borrowed domain meanwhile; the player waits behind a transition anyway, so a blocking fork-join is simpler and enough. |
| Interpolating without a display delay, or adopting snapshots as they arrive (the second draft) | The picture freezes for each tick's run time and then jumps: up to half a tick of judder on every tick. |
| Keying random numbers on the simulation tick (the second draft) | Repeats while the simulation tick is frozen: two calendar steps in a row would draw the same numbers. |
| Sorting storages after every tier move (the second draft) | A full sort per move for nothing: order is deterministic for an identical history, so sorting at saves and recordings is enough. |
| Our own entity tables | Possible, but EnTT already provides generational IDs, sparse-set storage and views; the user chose EnTT. |
| Cross-platform determinism | Needs fixed point or much more of our own maths; no feature needs it yet. |
| DragonBones for every enemy | Hundreds of armatures at 10–50 µs each would cost 5–25 ms. |
| The HD 4400 as the minimum spec (Tale of Immortal's) | No SDL_GPU backend on Windows with current drivers (§2). |
| SDL_Renderer (today's) | Picks OpenGL or Vulkan per system, no custom shaders worth the name, and its batching is outside our control. |
| Vulkan directly | Most control, most code; macOS needs MoltenVK bundled. |
| bgfx or sokol_gfx | Mature, but one more dependency and build to own, when SDL already ships SDL_GPU. |
| OpenGL | Deprecated on macOS at 4.1; weaker debugging tools. |
| SDL app callbacks (`SDL_MAIN_USE_CALLBACKS`) | For web and mobile event models; a non-goal, and they don't fit a separate logic thread. |
| Deferred rendering, a separate render thread, a low-level render command queue, an audio thread of ours | Not needed for this look, and the render thread is impossible with SDL_GPU (§8.12). |
| A general render graph | The passes are few and known; a fixed plan is enough (§8.4). |
| A job system in phase 1 | Nothing to parallelise yet; its rules apply from phase 1, the pool arrives with the first hotspot in phase 3 (§7.5). |
| A full task graph from the start | The most scalable, but the most complexity and debugging up front; fork-join per system covers the hotspots, and a graph can come later on the same rules. |
| Continuous real-time world simulation | The whole world would cost CPU every tick; calendar steps make tens of thousands of NPCs affordable (the user's call). |
| Variable timestep simulation | Behaviour depends on frame rate; no replay, harder tests. |
| Pixel art at a low virtual resolution | Not this look. |
| Spine | Its runtimes licence requires that "each user of the Products must obtain their own Spine Editor license" (or the developer's editor licence, under which anyone who modifies the runtime needs one too): a further restriction an AGPL-3.0-or-later game can't pass on to recipients of its source. |
| SDL_shadercross | Does what A10 needs in one tool but has no tagged release and no conda-forge package; kept as S8's fallback. |
| rapidyaml | Exception-free, but not in xmake-repo; yaml-cpp is, and catching at the parse functions covers its exceptions. |

## Appendix B: claims checked (2026-10-31 to 2026-11-01)

| Claim | Source | Result |
|---|---|---|
| Tale of Immortal's look (§2) | Steam store page and all 15 store screenshots (`store.steampowered.com/app/1468810`, `api/appdetails`; two identical), plus six combat and world-map images from the user | Combat arenas, crowds, effects, decals, combat text, controller prompts, the grid map with fog of war, blurred modals, vertical calligraphy, stroke tracing: seen. Animation technique and map zoom: not shown. The fandom wiki refused automated access (HTTP 402). |
| Tale of Immortal's requirements and languages | Steam store page | Windows 7/8/10 64-bit, DirectX 11, Intel HD 4400; English, Simplified and Traditional Chinese, Korean; full controller support. |
| SDL_GPU backends, requirements, present modes, mipmaps, BC7 format, driver hint | `SDL_gpu.h` and `SDL_hints.h` of SDL 3.4.16, the version we pin | Confirmed as quoted in §6 and §8. Corrected: D3D12 needs feature level 11_0 plus resource binding tier 2 (the draft said only "Windows 10"). |
| BC compression on Apple Silicon Macs | Apple's Metal feature set tables (PDF) | "All Apple7 and Apple8 macOS devices have support"; Metal 3 and 4 devices have at least the Apple7 texture capabilities. |
| SDL3_ttf GPU text engine, on-demand atlas, HarfBuzz, vertical text | SDL_ttf `release-3.2.2` header; `main`'s `SDL_gpu_textengine.c` and CMake | Confirmed; HarfBuzz optional (off in the xmake package by default). The draft's own glyph atlas was unnecessary. |
| Noto CJK licence | `notofonts/noto-cjk` `Sans/LICENSE`, `Serif/LICENSE` | SIL OFL 1.1, Simplified Chinese included. |
| Package availability and versions | xmake-repo at our locked commit `cf09fff02b4e` | libsdl3_image 3.4.0, libsdl3_mixer 3.2.4, libsdl3_ttf 3.2.2, rmlui 6.3, yaml-cpp 0.9.0, harfbuzz present; rapidyaml and DragonBones absent (corrected: the draft assumed rapidyaml). |
| Shader tools on conda-forge | prefix.dev and anaconda.org; `libsdl-org/SDL_shadercross` | DXC 1.10.2605.37 and SPIRV-Cross 1.4.363.0: linux-64, win-64, no osx-arm64. SDL_shadercross: not packaged, no releases (corrected: the draft assumed a pinned shadercross). |
| Software Vulkan for CI | prefix.dev; `conda-forge/mesalib-feedstock` recipe | `mesa-lavapipe` 26.2.6 (`-Dvulkan-drivers=swrast`) and `libvulkan-loader` for all three platforms. |
| RmlUi | `mikke89/RmlUi` README, Backends folder, changelog, CMake docs | MIT; 6.3 latest; SDL_GPU backend since 6.2, basic; FreeType default, replaceable; no CJK or shaping statement. |
| YAML libraries | `jbeder/yaml-cpp` (README, `exceptions.h`); `biojppm/rapidyaml` README | Both MIT; yaml-cpp throws `ParserException`; rapidyaml is exception-agnostic with error callbacks. |
| Spine licence | `esotericsoftware.com/spine-runtimes-license`, `spine-editor-license`, `spine-runtimes` 4.2 `LICENSE` | As quoted in Appendix A. Not legal advice. |
| DragonBones | `DragonBones/DragonBonesCPP` (LICENSE, commits, core sources), the DragonBones organisation | MIT; renderer-agnostic core with Cocos2d-x, SFML and Android adapters; data version `5.6.300`; last commits July 2025 (JNI), core 2018. Editor status unconfirmed: dragonbones.com refused connections; the JS runtime points to LoongBones. |
| Poor man's threading | `etodd.io/2016/01/12/poor-mans-threading-architecture/` | Two copies, one lock per frame at the swap, lockstep, rendering on the main thread, the physics thread; quotes in §5. |
| SDL timing and GPU details used after the reviews | `SDL_timer.h`, `SDL_gpu.h` of SDL 3.4.16 | `SDL_DelayPrecise` busy-waits "if necessary"; `R16G16B16A16_FLOAT` and `R8G8B8A8_UNORM` are universal colour targets, `R11G11B10_UFLOAT` only a universal sampler format; depth-stencil guarantees D16 plus one of D24S8 or D32FS8; `first_vertex`/`first_instance` don't reach built-in shader IDs; whole textures cycle; a colour target cycles only when not loaded. Corrected: the draft's 8-bit fallback for the scene could never trigger. |
| EnTT | xmake-repo at `cf09fff02b4e` | Present, MIT. |
| Scale review | A fresh reviewer's report and the author's own review, 2026-10-31 | Estimates (costs per item, bandwidth, fill rate) are from typical figures, not benchmarks; phases 3 and 4 measure them. |
| Intel Haswell graphics and SDL_GPU (2026-11-01) | Tom's Hardware, TechSpot and PC Gamer on Intel's November 2021 driver (`tomshardware.com/news/intel-disables-directx-12-on-haswell`); Intel's community forum and eTeknix on Windows Vulkan support | D3D12 disabled on Haswell graphics (HD 4400 included) from driver 15.40.44.5107; Intel's Windows Vulkan covers 6th-generation chips and newer. Sources from 2016–2021; nothing newer found that reverses either. Corrected: the draft's minimum spec (HD 4400) had no backend. |
| SDL_GPU shader formats and the D3D12 tier-1 property | `SDL_gpu.h` of SDL 3.4.16 | DXBC (shader model 5.1) and DXIL (6.0) for D3D12; tier 1 only "if (and only if) the application uses 8 or fewer storage" resources; SPIR-V's layout lists sampled textures without separate samplers. |
