# Phase 6, chunk 5b-look, part 2: the travellers walk

Date: 2026-11-23 · Status: **draft** · Chunk spec: `2026-11-10-phase6-chunk5-world-map-design.md` · Builds on:
`2026-11-21-phase6-chunk5b-look-terrain-design.md` (part 1), `2026-11-22-roads-design.md` and
`2026-11-22-player-walk-and-camera-design.md` (their Outcomes) · Followed by: 5b-look part 3 (sharpness)

## In short

- **Figures walk.** Every traveller on the world map and the player's cultivator, in every scene, walk with drawn step
  frames instead of gliding as one still picture.
- **Two facings, as in Tale of Immortal.** One side-on walk per figure, mirrored to face the other way; a figure faces
  the way it moves left or right and keeps its facing while it moves straight up or down or stands.
- **Steps by distance.** Each frame lasts a fixed distance walked, so feet never slide whether a figure strides along a
  road or crawls through forest, and a figure that stands (paused, waiting, at its journey's end) stands still.
- **Walks are data.** A new presentation table, `assets/data/animations.yaml`, says each figure's frames and stride;
  the frames are ordinary atlas sprites.
- **The crowd's names leave the cultivator clear.** A traveller's name that would cover the player's cultivator fades
  out while it would.

## Intent

The user asked on 2026-11-14, of 5a's circling stand-ins: "When do we make the NPCs actually walk properly and not just
shake/jitter around?" The roads branch made travellers follow the road's curve smoothly; they still glide as one still
sprite, and so does the cultivator. This part makes them walk.

On 2026-11-23 the user chose:

- **Who walks:** the world map's travellers and the player's cultivator, in every scene (held walking and click routes);
  arena creatures wait for combat (chunk 7).
- **Two facings,** "similar to ToI". The user's own recording of Tale of Immortal's combat
  (`~/Videos/Recordings/recording_20261123_205102.mp4`) shows the reference: a side-on figure mirrored left and right,
  facing the way it moves (not where it attacks: she runs left while her beam fires right), a slow walk and a bounding
  run, the shadow flat on the ground under the body, almost every frame at 15 fps a new pose.
- **Drawn frames for this slice;** "I want skeletal, but that can be its own future project later".
- **Walks as data** (approach 1 below), "We can always make a pregenerator that turns the yaml into generated code
  later".
- **Labels:** yes to the proposal that a traveller's name covering the cultivator fades out while it does.

**Why:** the world map is where the player watches the world live. People who glide read as tokens on a board; people
who walk read as people going somewhere.

## History

- **No walk exists.** One still `villager` sprite (64x116, foot 32,112) for every traveller and one still `cultivator`
  (72x128, foot 36,122), both drawn front-on by `art/placeholders.py` (`villager()`, `cultivator()`); no step frames,
  no facing, no mirror on a sprite instance. The map's stamps and clouds already mirror (`gfx/stamp_draw.cpp`,
  `widthScale = -1`), so the renderer can draw a flipped sprite.
- **Travellers are published** by `Host` (`game/host.cpp`, the travellers' chunked publish) as a shadow and a `villager`
  instance each, at `core::DrawnWhere()` (the road curve shift, presentation only) for the previous and current tick,
  labelled `LabelPriority::Named` (`Selected` when selected) with `core::TextFog::Sight`. The publish runs in fixed
  chunks and merges in chunk order, so whatever picks a frame must be a pure function of the journey and the clock.
- **Where a traveller is** is `core::Where()` of its journey at the clock (`core/journey.hpp`): legs linear between
  tile centres at each tile's arrive tick (cost-paced for NPCs, distance-paced for the player's route,
  `MakePacedLeg`), stays drifting linearly between daily points round the city's tile centre, other members of a
  journey at fixed lattice offsets from member 0.
- **The cultivator** is an entity with a saved `Look` (`game/components.hpp`: sprite, shadow, layer, pipeline, scale),
  its sprite from `creatures.yaml`'s `sprite: cultivator`. Held movement is `Host::Walk`; a click route is
  `Host::PlayerRoute()` with a presentation-only drawn shift (`Host::DrawnPlayer`), never saved or hashed. That shift is
  the precedent for presentation-only state kept by the host.
- **Pause** (the user, 2026-11-22, [pause-freezes-game-animation]): nothing in the game world animates while paused; walk
  cycles are named there. The world clock stands while paused and while it waits for a month step; the cultivator
  stands while paused.
- **Labels** (`gfx/labels.hpp`): the pass keeps labels off each other (stacks of one name, priority selected > hovered >
  player > named > rest, fades over `LABEL_FADE_SECONDS`, hysteresis `LABEL_HYSTERESIS_PIXELS`) but not off figures.
  Places' names hang under their markers so the cultivator's label on the start city stays clear (`world.cpp`).
- **Presentation tables** (`effects.yaml`, `seals.yaml`, `world_look.yaml`) are not hashed and debug builds reload them
  (`App::WatchData`); they reach the renderer today, not the host.
- **Decided elsewhere and kept:** the terrain spec gave part 2 "smooth roads, walk animation and facing, spreading the
  travellers' queues, label placement"; the roads branch did the smooth roads and sent "NPCs leaving roads, and
  spreading the queues" to the game-design review (roads spec, Non-goals). Part 3 owns art scale, offline mips and atlas
  page size. Before 1.0 sprite IDs may change freely, dead sprites are removed and nothing migrates.

## Goals

1. **Walking frames.** Each walking figure draws one frame of its walk clip: `frames` sprites in order, frame `i` shown
   while the distance it has walked, modulo `frames × stride`, lies in `[i × stride, (i + 1) × stride)`. A figure that
   isn't moving draws its clip's standing sprite.
2. **Distance walked for a traveller** is a pure function of its journey, the member and the tick: the length of the
   path `core::Where()` traces from the journey's start to the tick (legs' straight segments, stays' daily segments),
   so the same journey at the same tick always draws the same frame, at any worker count, after a load, in a replay.
   Members other than 0 walk member 0's distance (they keep fixed offsets from it). A traveller is moving at a tick when
   its position at the tick differs from its position at the tick before.
3. **Distance walked for the cultivator** is kept by the host as presentation-only state: it grows by how far the body
   moved each tick (held walking or a route), never saved, never hashed, never read by the simulation, and starts at 0
   on a load or a scene build. The cultivator is moving at a tick when its body moved during that tick.
4. **Facing.** The clip's frames face right; a figure facing left is drawn mirrored about its foot.
   - A traveller faces the sign of its horizontal movement at the tick; while that is zero (a straight north or south
     step, a stand) it keeps the facing of the last horizontal movement before the tick in its journey, and a journey
     that hasn't moved horizontally yet faces the way its first horizontal movement will go (right if it never does).
     Still a pure function of journey, member and tick.
   - The cultivator faces the sign of the last nonzero horizontal movement of its body, kept by the host as
     presentation-only state (right on a load or a scene build). Casting and aiming don't turn it.
5. **The pause holds.** Travellers' frames follow the world clock and the cultivator's its body's movement, so a paused
   world, a clock waiting for the month step and a standing figure all draw unchanged frames.
6. **Walks are data:** `assets/data/animations.yaml`, presentation (not hashed), hot-reloaded in debug builds like the
   other presentation tables. Each entry is keyed by the standing sprite a `Look` names and gives the walk clip:
   ```yaml
   villager:
     walk: { frames: [villager_walk_0, villager_walk_1, ...], stride: 12 }
   cultivator:
     walk: { frames: [cultivator_walk_0, ...], stride: 14 }
   ```
   The reader refuses, naming the line: an unknown key, a standing or frame sprite missing from the manifest, a frame
   missing from any sprite list (`art/scenes.toml`) that holds its standing sprite (so a frame is never drawn from a
   page that isn't resident: the gap backlog's seals item records for seals), no frames or more than a cap, a stride
   not finite or not above 0. A sprite without an entry never animates, as today.
7. **New art, placeholders.** `art/placeholders.py` draws the villager and the cultivator side-on, facing right: a
   standing pose (replacing today's front-on `villager` and `cultivator`, same names, so `creatures.yaml` and saved
   `Look`s stay valid) and 8 walk frames each on the same canvas and foot point, listed in `art/sprites.toml` and
   `art/scenes.toml` wherever the standing sprite is. The final art comes from an artist later into the same slots.
8. **Names keep off the cultivator's body.** In the label pass, a `Named` label (a traveller's) that overlaps the
   player's cultivator's drawn body on screen (its sprite's box) fades out as a label giving way to a higher priority
   does, with the same hysteresis; the player's own label and a `Selected` label never give way to it. `Rest` labels
   (the arena's foxes) and the health bars that take their opacity from them (`BarOpacity`) are untouched: in a fight
   they crowd the cultivator exactly when they matter.
9. **Nothing else moves.** Routes, pace, the simulation, saves, `StateHash` and `WorldHash`, replays and the travel
   journals are unchanged; only what is drawn changes.

## Non-goals

- **Skeletal animation** (the user, 2026-11-23: its own future project): a rig runtime, a new art format and an artist's
  tool. Recorded in the sweep-up backlog. This part's clips are drawn frames, which skeletal tools can export.
- **A run gait** and any speed-dependent gait: the cultivator moves at one speed and travellers differ only by the land;
  gaits belong with combat's dodges and dashes.
- **Arena creatures** (foxes, the crowd) walking: chunk 7. A creature whose sprite has no entry in `animations.yaml`
  draws as today.
- **Four or eight facings.**
- **Spreading queues and NPCs leaving roads** (game-design review), **sprite sharpness** (part 3).
- **Code generated from `animations.yaml`:** later, if wanted (the user, 2026-11-23); never the only way a walk reaches
  the game.

## Anti-goals

- A frame or facing that differs between worker counts, between a run and its replay, or across a save and load of the
  same tick (travellers), or that changes any hash.
- Anything in the game world moving while paused.
- Feet visibly sliding: a stride that doesn't match the frames' painted step (judged at map zoom; see Decision 7).
- A measurable rise in the logic's publish or the frame on `measure --runs world` beyond noise (the roads branch measured
  ~10% run to run): the distance walked is computed per traveller in view each publish.

## Approaches

1. **Walk clips as data (chosen by the user).** `animations.yaml` describes each walk; frames are atlas sprites; the
   host picks each figure's frame from its distance walked and sets a mirror flag. New walks and creatures are data
   edits, hot-reloadable and moddable; skeletal clips can later be another clip kind. Cost: one more table, its reader,
   refusals and tests.
2. **Walks written into the code.** The same picking with frame names and stride as constants: quickest, but it is the
   hard-coded content the data-driven sweep removes, and every walk is a code change.
3. **The GPU picks the frame.** The instance carries a step count and the shader picks from a contiguous atlas strip:
   saves one division per figure, but ties frames to strips and needs a shader and C++ mirror; worth it only with the
   backlog's GPU figures work.

## Decisions (the controller's calls, for the user to check)

1. **The host picks the frame,** at publish, as `MakeInstance` is given the frame's sprite: the renderer stays unaware of
   animation, and the snapshot gains only the mirror flag. So `animations.yaml` reaches the host, not only the renderer:
   the app hands the read table to the logic thread at start and on each debug reload (as `creatures.yaml`'s reload
   reaches `Host::ApplyData`), kept outside the world's state and hashes.
2. **Mirroring is an instance flag** (`core::INSTANCE_MIRROR`, a new bit of `Instance::flags`): the sprite shader flips
   the quad's U and places the quad about the mirrored foot (`width − footX`), so a mirrored figure stands on the same
   foot point. The shadow isn't mirrored (it is symmetric).
3. **The standing sprite is the clip's key,** the sprite `Look` already names: no change to `Look`, its save or
   `creatures.yaml`.
4. **Labels:** the player's body box travels to the label pass with the labels; the overlap test against it reuses the
   pass's hysteresis and fade.
5. **Who a clip applies to:** the travellers' publish and the player's cultivator only. Other entities naming a sprite
   with an entry (the arena's and stress scene's `Wander` crowd, `Look{ .sprite = Villager }`) draw its standing sprite,
   unmirrored, as today: they are dev scenes' crowds and their walk is chunk 7's. They become side-on and face right.
6. **The dash** (the arena's movement-slot cast, ~29 units a tick) adds nothing to the distance walked (the frame holds
   through it rather than blurring two frames a tick) but turns the cultivator to the dash's horizontal direction, so it
   never slides backwards. Other casts and aiming don't turn it.
7. **Distance on the body's path, not the drawn curve:** distance and facing follow `core::Where()` (and the cultivator's
   body), not `core::DrawnWhere()`. On a road the drawn curve smooths the tile path and is a few percent shorter, a
   slide too small to see at map zoom; measuring the drawn curve would put the road index into every traveller's
   distance and cost more per publish. If a slide shows by eye on the roads, it comes back as a ruling.
8. **Small zoom:** at zoom 0.25 a villager is about 16 pixels tall. Step frames there are meant animation, not shimmer
   (the reference shimmer cases hold the clock still). Judged by eye at 0.25, 0.5 and 1 (look-at-the-game); if the steps
   read as flicker, a minimum on-screen height below which the standing sprite shows comes back as a ruling.

## Left to the builder

- The exact frame count and stride per figure (8 frames proposed; the stride matched to the painted step) and the
  placeholder poses.
- How the distance walked is computed per traveller (prefix lengths per leg, or a walk over the parts up to the tick),
  provided the cost goal holds; how the host keeps the clips; the cap on frames.
- How the body box reaches the label pass.
- Which reference pictures change and refreshing them (`reference --update`, every changed one viewed).

## Tests (what each must show)

- Distance walked: a leg, a stay, a part boundary, a paused span (same tick, same distance), member 0 and another member
  alike; the frame index at stride boundaries; a standing figure draws its standing sprite.
- Facing: left and right legs, a north-south step keeping the last facing, a journey whose first movement is vertical,
  the cultivator's held walk turning and keeping, a cast not turning it, a dash turning it and holding its frame.
- Determinism: the travellers' publish gives the same frames and flags on 1 and 4 workers and after a save and load at
  the same tick; hashes unchanged.
- The reader's refusals, each naming its line; a hot reload swapping a clip.
- Mirroring: a mirrored instance's quad and foot (a pure placement test; a reference picture shows both facings).
- Labels: a `Named` label over the cultivator's body fades out, comes back once clear, holds at the hysteresis edge; a
  `Selected` one, the player's own and a `Rest` one (with its health bar) stay.
- Reference pictures of a walking crowd and the cultivator at the capital.
