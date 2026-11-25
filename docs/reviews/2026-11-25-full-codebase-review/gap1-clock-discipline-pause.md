# Review: gap1-clock-discipline-pause

**Scope:** Audit which clock every animated or time-dependent thing reads, across src/gfx (seal_draw spin and appear, cloud_draw bob, glow_table and GlowTimeAt, text and label fades, sprite dissolve, stamp mist, river and ground shader time uniforms, the volley_draw sword plans), src/app (FrameClock, damage_numbers, fog_fade, camera_control and camera_path, target_cursor, cast latency, view hover) and shaders/*.hlsl (time inputs). The candidate clocks are simulation ticks, display_delay's delayed tick, the render tick and wall seconds. Check each one against the user's rule that nothing in the game world animates while paused (only UI may move; a paused frame should upload no new GPU data), against focus-loss pause, against hitches and very large frame deltas, and against replays played back at a different speed. Produce a table: item, clock, does it stop on pause, does it re-upload while paused, is it correct. The sweep-up backlog already lists this as audit item 12 (in /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md): check whether that list is complete and still accurate rather than restating it. Earlier units found that spin_scale stretches appear and fade, that every burst republishes while paused, and refuted a frame-time-extremes finding. Don't repeat those. (Why this unit exists: Pause-freezes-animation is an explicit user rule, but no unit took a clock or time-source angle across gfx, app and the shaders together. The only related findings concern republishing, not which clock each animation reads.)

**Findings:** 11 standing (0 critical, 0 high, 2 medium, 7 low, 2 info), 0 raised but refuted on verification.

**Parts:** gap1-clock-discipline-pause.md, gap1-clock-discipline-pause-part2.md

## Assessment

## Clock discipline and pause audit: gfx, app and shaders

**What I found:** the code has four clocks and keeps them apart well. But the world's P pause does not stop the clock that most animations read. And backlog audit item 12 is partly wrong about what animates while paused and leaves several things out.

### The clocks
- **Simulation tick** (`TickCounters::simulation`). Frozen only by the focus or minimise pause, a calendar step's transition, and scene loading. **The world's P pause does not freeze it** (`Logic::m_advanced = !m_paused && m_calendar == None`; P only stops `WorldState::worldTicks`).
- **Render tick** (`core::RenderTick(simTick, alpha)`, with alpha behind the display delay). Holds exactly while the simulation is frozen: alpha is 1 when `advanced` is false, and both sides of the change line up. **It runs on under P.**
- **World clock** (`worldTicks`). Stops under P and under the focus pause.
- **Frame clock** (`app::FrameClock`, wall time, at most 0.1 s per frame). Never stops while frames are drawn.

### Table

| Item | Clock | Stops on focus pause | Stops on P pause | Re-uploads while paused | Correct per the rule |
|---|---|---|---|---|---|
| Seals: spin, appear, particles (`seal_draw`, `LaySeals`) | render tick (f64 seconds) | yes | **no** (the portal turns) | yes, every frame | no under P (known: audit 12) |
| Glow pulse, charge, progress (`GlowTimeAt` to sprite.vert uniforms) | render tick, split into u32 tick and fraction | yes | no (the world map has only steady glows today) | uniforms only | latent |
| Sprite dissolve, dying, arriving | sim tick, interpolated | yes | no (none on the world map) | instances re-upload every tick | latent |
| Swords and trails (`DrawVolleys`, `SwordAt`) | render tick (alpha passed as f32) | yes | no (no combat on the world map) | overlay and ribbons every frame | latent |
| Flashes (`DrawFlashes`) | render tick | yes | no | overlay every frame | latent |
| Health bars, HUD bars | render tick, alpha | yes | n/a | overlay every frame | UI, fine |
| Damage numbers | render tick | yes | no (none on the world map) | text meshes every frame | latent |
| HUD cooldowns, "Not enough Qi" | render tick | yes (holds) | no | text meshes | UI, fine |
| Labels: fade, cross-fade, glide; caption fades | frame clock | no | no | text meshes every frame | UI by recorded choice (camera.hpp) |
| Text cache eviction | frame clock | no | no | none | fine |
| Cloud bob (`DrawClouds`) | frame clock | **no** | **no** | cloud buffer every frame | no (known) |
| Cloud drift | fog fade's eased explored value, so the frame clock | **no** | **no** | same | no; the backlog says it is the frame clock directly |
| Fog ease (`FogFade::Update`) | frame-clock delta | **no** (finishes an easing in flight, up to 0.5 s) | **no** | fog texture only when its revision changes | no; not in the backlog |
| Travellers' position and walk frame | world clock | yes | yes | instances every tick (keyed by wallTick) | animation correct, uploads not |
| Player walk frame and drawn shift | per-tick body step, world clock | yes | yes (stands) | same | correct |
| Stamps, mist, terrain sprites | none (tile hash), re-laid on view or zoom change | n/a | n/a | only on view change | correct |
| Ground, river, road shaders | **no time input at all** (zoom only) | n/a | n/a | no | correct; the "ripples" the backlog names do not exist |
| Route line | snapshot route, static | n/a | n/a | every frame | uploads every frame |
| Target-cursor tile | wall-time stepping (UI) | **no, and steps while unfocused with the stick held** | UI | every frame | input bug, see findings |
| Camera follow, pan, zoom | wall seconds, clamped to 0.1 s | camera keeps working (UI) | UI | none | fine |
| Camera path, month step at a set second | wall time since the path started (unclamped, by design) | no (logs a warning) | no | none | measurement only |
| Cast and move latency | wall ns; a cast press expires after 1 s; a move press is forgotten on pause | partly | n/a | none | measurement only, minor |

### Strengths
- `RenderTick`, `Alpha` and `advanced` give a render clock that holds on frozen ticks with no jump.
- The animations are pure functions of time: `SealWrap` and `SealSpin` wrap in f64 before narrowing, and `GlowTime` splits the tick to stay exact in 32 bits.
- Every wall-time step is capped (frame clock, camera, zoom, target cursor), so big hitches are handled.
- Travellers and the player's walk are pure functions of the world clock, so they already freeze correctly under both pauses.

### Weaknesses
- The P pause is a world-clock pause, not a simulation pause. Each system must check `world.paused` itself, and `Host::Tick` runs orbits, spins, `Combat::Tick`, respawns and the scene's tick during P whatever the scene. The 5b2 spec promised that the pause "stops the whole world on the world map", and the orbit-walker case (R4a) has already shown this failure once.
- No shared "presentation time" exists. Seals, glows, swords and flashes each work out the render time a different way. The world-presentation items (fog and clouds) use the frame clock, which the code itself treats as a UI clock.
- Nothing is skipped while paused: GPU uploads (instances keyed by wallTick, text meshes, route, cursor tile, clouds, seals, overlay) all repeat on every paused frame. Rendering stays uncapped under mailbox while the window is unfocused.

### Risks
- Chunk 7 will put encounters on the world map. Combat timers, Qi, cooldowns, dashes, volleys and DoTs would then all run through a P pause, and that is a gameplay bug, not a cosmetic one.
- If replay fast-forward or ToI-style time acceleration is added, frame-clock animations will not scale with it.

### Overall
The clock design is sound and well documented. Two things are wrong: P pausing does not freeze the simulation tick, and the backlog item that should fix this misstates its scope. This should be settled in the brainstorm the backlog already asks for ("which clock each animation reads") before chunk 7.

**Files read in full:** src/core/tick_clock.hpp, src/app/app.cpp, src/app/view.hpp, src/app/view.cpp, src/app/fog_fade.hpp, src/app/fog_fade.cpp, src/app/target_cursor.cpp, src/app/target_cursor.hpp, src/app/camera_control.cpp, src/app/cast.cpp, src/app/damage_numbers.cpp, src/app/bindings.cpp, src/gfx/cloud_draw.hpp, src/gfx/cloud_draw.cpp, src/gfx/glow_table.hpp, src/gfx/glow_table.cpp, src/gfx/camera.hpp, src/game/scenes/world.cpp, shaders/river.frag.hlsl, shaders/river.vert.hlsl, src/gfx/renderer.cpp (Upload, UploadTerrain/Ground/Route/Clouds, LaySeals, PlaceTexts: lines 380-830), src/gfx/seal_draw.cpp (time paths), src/gfx/volley_draw.cpp (time paths), src/app/hud.cpp (Play/Build), src/core/logic.cpp (RunBurst, RunTick, TakeInput, pause handling), src/game/host.cpp (Tick, Move, ToggleWorldPause, DemoGlow), shaders/sprite.vert.hlsl (glow age), docs/superpowers/specs/2026-11-15-phase6-chunk5b2-moving-design.md (pause sections), /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md (audit 12), /home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/pause-freezes-game-animation.md

## Findings

### GAP1-CLOCK-DISCIPLINE-PAUSE-01: The world's P pause leaves the simulation tick (and so the render clock and every per-tick system) running

- **Severity:** medium · **Verdict:** confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/logic.cpp:125`

`m_advanced = !m_paused && m_calendar == Calendar::None` uses only the window pause (`m_paused`). `ToggleWorldPause` only flips `WorldState::paused` (src/game/host.cpp:940-948), and `Host::Tick` gates just the world clock and the player's walk on it (host.cpp:861, 1017). Under P everything keyed on the simulation tick keeps running: orbits and `Spin` (host.cpp:880-893), `Combat::Tick` (Arriving, Dying, respawns), `DecideActive`/`WalkActive`, `Running().Tick`, cooldown `readyTick` against `simulationTick`, and `DemoGlow`. The render tick follows, so seals, glows, flashes, swords and damage numbers animate too.

**Evidence:** The 5b2 spec says: "The pause stops the whole world on the world map (clock, travellers, the cultivator), not only the clock". Its Outcome also records R4a, "Walkers orbit while the world is paused": this design has already failed once. Today only the portal seal visibly moves, because the world scene spawns only the player (world.cpp Build).

**Impact:** Each new world-map system has to remember to check `world.paused` by hand. When chunk 7 brings encounters to the world map, P will not pause combat: cooldowns, Qi, DoTs, dying, respawns and volleys will all run on. Every render-tick animation also breaks the user's rule, and paused frames keep changing GPU data.

**Recommendation:** In the backlog's brainstorm, decide whether P should freeze the simulation tick. One way is for the logic to treat a world pause like a frozen tick that still takes UI and map actions (WalkTo, Select, held movement cancelling a route), as frozen ticks already take UI decisions. The other is to publish a presentation tick that holds under P and drive seals, glows, volleys, flashes and numbers from it. Either way, add a test that steps a paused world map with an orbiting or spinning entity and a seal, and checks that nothing moves.

**Verification:** logic.cpp:125 m_advanced ignores WorldState::paused; host.cpp ToggleWorldPause only flips Live().paused and Tick gates clockRuns/Move on it, while orbits/Spin (host.cpp ~881-891) key on counters.simulation. Spec 5b2 line 233 says the pause stops the whole world; R4a (line 476) records only an interim orbit exception for walkers. Latent today (only the player and portal seal exist), so medium rather than high.

### GAP1-CLOCK-DISCIPLINE-PAUSE-02: Rendering stays uncapped (mailbox) while the window is unfocused and the game is paused

- **Severity:** medium · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/platform/window.cpp:314`

`Window::Poll` waits for events only while the window is hidden (`state.visible ? SDL_PollEvent : SDL_WaitEventTimeout`). A visible but unfocused window, which pauses the game (`PausesGame`), runs the main loop at full speed. The default present mode is mailbox (renderer.cpp:1339), which does not block, so the loop redraws, re-lays seals, clouds and text, and re-uploads as fast as the GPU allows, with nothing changing but the cloud bob.

**Evidence:** app.cpp:311-334 has no throttle on `m_paused`; PresentModeOrder PACED = {Mailbox, Immediate}.

**Impact:** An alt-tabbed game burns a CPU core and the GPU (heat, fans, battery on laptops) for a frozen picture. The user's "avoid wasted work" and "paused frames upload nothing" rules both apply.

**Recommendation:** While `m_paused`, limit frames to a low rate (for example the display rate or 30 Hz) with `platform::WaitUntilNs`, as the transition already does with `TRANSITION_FRAME_NS`. Better still, skip the draw when nothing (camera, hover, UI, snapshot content version) has changed since the last frame.

**Verification:** window.cpp:314 waits only when !state.visible, so a visible but unfocused window loops unthrottled. I did not verify the mailbox present mode or whether the swapchain paces the loop. Otherwise the claim is plausible and unrecorded in the backlog. Medium confidence and probably low to medium in practice.

### GAP1-CLOCK-DISCIPLINE-PAUSE-03: Backlog audit item 12 is inaccurate and incomplete about what animates and uploads while paused

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** docs · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `/home/lotus/.claude/projects/-home-lotus-Development-project-n/memory/sweep-up-backlog.md:99`

The item names as offenders "seals, clouds' drift by View::frameClock, ripples, walk cycles, sword and effect times", says to gate "the seal, overlay and route uploads", and says to "scope both pauses". Checked against the code: (a) nothing has ripples: the ground, river and road shaders take no time input (river.frag.hlsl and ground uniforms hold only zoom and look). (b) Walk cycles already freeze under both pauses: travellers follow `core::WalkedAt` of the world clock, and the player's `StepPlayerWalk` adds nothing while `Move` stands it. (c) Cloud drift follows the fog's eased explored value; only the bob reads `frameClock` directly. (d) Swords, flashes, glows and numbers already freeze under the focus pause, because the render tick holds; they would move only under P, where none exist today. (e) Missing from the list: the fog fade easing on frame-clock deltas, `Host::Tick`'s per-tick systems under P, the sprite-instance buffer re-uploaded every tick (keyed by wallTick), text meshes, the target-cursor tile and the clouds uploaded every frame, and uncapped redraws while unfocused.

**Evidence:** renderer.cpp:516-524 calls `meshes.Upload`, `UploadRoute` (target tile included), `UploadClouds`, `overlay.Upload` and `seals.Upload` unconditionally. renderer.cpp:465 has `newInstances = !uploaded.Holds(shownId)` with `SnapshotId{epoch, wallTick}`.

**Impact:** A spec written from this item would chase things that don't exist (ripples), redo walk cycles that already work, and miss the fog ease, the per-tick systems and most of the uploads.

**Recommendation:** Rewrite item 12 using the clock table in this review. Under the focus pause only the frame-clock items move (cloud bob, fog ease, and the cloud drift that follows it). Under P every render-tick item moves, plus the per-tick systems. List all the per-frame uploads.

**Verification:** Backlog item 12 (lines ~99) does name ripples, but shaders/river.frag.hlsl has no ripple or time input, so that part is inaccurate. The omitted uploads and fog ease are real. Item 12 already calls for a brainstorm on which clock each animation reads, so incompleteness is a known deferral. Did not independently verify the walk-cycle and swords claims in detail.

### GAP1-CLOCK-DISCIPLINE-PAUSE-04: Comments claim the render clock holds while the game is paused; under the world pause it does not

- **Severity:** low · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/camera.hpp:53`

`View::frameClock` is documented as "wall time, which runs on while the game is paused, unfocused or loading a scene, when the render clock holds... damage numbers are timed by the render clock ... and freeze with the game." app/view.hpp:46 (FrameClock: "runs on while the game is paused, unlike the render clock") and app/app.cpp:876 ("numbers on the render clock, which holds") say the same. That holds for the focus pause, a calendar transition and loading, but not for the world's P pause, during which the simulation tick, and so the render tick, keeps advancing.

**Evidence:** logic.cpp:125 `m_advanced` ignores `WorldState::paused`; RenderTick = simulationTick - 1 + alpha.

**Impact:** Readers, and anyone writing a new animation, will assume the render clock is pause-safe and use it for world animation that then plays on under P. That is how the portal seal came to break the rule.

**Recommendation:** Say which pause holds which clock: the render tick holds on frozen ticks (focus pause, transition, loading) but not under the world pause. Or fix the P pause as in the first finding and keep the comments as written.

**Verification:** Design 0002 line 173 says the render clock holds on frozen ticks, which are focus pause, transition and loading. World P is not a frozen tick (logic.cpp:125), so comments claiming the render clock holds when paused are incomplete for P. Comment-only issue.

### GAP1-CLOCK-DISCIPLINE-PAUSE-05: Fog easing, and the cloud drift that follows it, keeps animating through both pauses on the frame clock

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** pause-rule · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/app.cpp:878`

`m_fog.Update(drawn, *map, frameSeconds - m_frameSeconds, fadeSeconds)` eases explored and sight values on wall-time deltas. A reveal or sight change in flight when either pause starts finishes over up to `fade_seconds` (0.5 s). Because `PlaceClouds` sets drift to `look.drift * explored * tileSize`, the clouds slide off newly explored tiles while the game is paused. The comment at app.cpp:876 even says the fog is on the frame clock "which runs while the game is paused".

**Evidence:** fog_fade.cpp:128-130 `Ease(seconds / fadeSeconds)`; cloud_draw.cpp:287 drift uses `explored`.

**Impact:** This breaks the pause rule in a small way: the world's fog and clouds visibly move for up to 0.5 s after a pause. It also bumps the fog texture's revision, so a paused frame uploads new fog data.

**Recommendation:** Ease the fog on the same pause-aware presentation clock as the rest of the world (the render tick, once P holds it), or give FogFade a zero step while `drawn.calendar->paused` or the window pause is on. Add a fog_fade unit test asserting no ease while paused.

**Verification:** The app.cpp:876 comment itself says the fog is on the frame clock, which runs while paused, so this is a known choice. The behavior (up to 0.5 s of easing after a pause) is real, but it is tiny and covered by backlog item 12's 'clouds drift by frameClock'. I did not verify the fog texture revision upload claim.

### GAP1-CLOCK-DISCIPLINE-PAUSE-06: Sprite instances are re-uploaded every tick while paused, because the upload key is the wall tick

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/renderer.cpp:465`

`SnapshotId{ .epoch, .wallTick }` decides whether the instance storage buffer needs a new copy. Frozen ticks (focus pause, transition, loading) and world-paused ticks all publish a new wallTick, so every paused tick copies the whole instance array, even when its content is the same as the last.

**Evidence:** uploads.hpp: `Holds(SnapshotId)` compares epoch and wallTick. INSTANCE_BYTES 56 and MAX_INSTANCES 2^17 make up to 7.3 MB per copy, about 440 MB/s at 60 ticks in the stress scene while paused.

**Impact:** A paused game keeps copying data to the GPU, against the user's rule that a paused frame uploads nothing new, and it wastes bandwidth and power. This is the GPU side of the earlier "every burst republishes while paused" finding and may overlap with it.

**Recommendation:** Have the logic stamp each publish with a content version that changes only when the simulation advanced, the culling view changed or a UI decision changed what is drawn. Key `UploadedSnapshot` on that version, or reuse the previous snapshot's instances when a frozen tick's publish is the same.

**Verification:** uploads.hpp Holds compares epoch and wallTick, and renderer.cpp:465 uses it, so every published tick (including paused ones) re-uploads instances. The mechanism holds. Dedup keyed on content would be a real change, but backlog item 12 already says to gate uploads on a version. Duplicate of the earlier republish finding, so medium is overstated. The 440 MB/s figure is a worst case at MAX_INSTANCES.

### GAP1-CLOCK-DISCIPLINE-PAUSE-07: The target cursor keeps stepping while the window is unfocused when the left stick was held at focus loss

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** input · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/app.cpp:727`

`SteerCursor` runs every loop iteration whatever the focus, and steps the cursor from `m_bindings.Move()`. `Bindings::m_stick` changes only on stick events, and SDL sends none to an unfocused window by default. A stick held at alt-tab therefore keeps stepping the tile every `CURSOR_REPEAT_SECONDS` while the game is paused, and the renderer uploads the moving tile. The same stale `m_stick` is sent by `SendHeld`, so on refocus the cultivator walks until the stick next reports.

**Evidence:** app.cpp:565-569 only ends the drag while paused; bindings.cpp:117-121 sets m_stick from events only. This is the same mechanism as known audit item 5 (camera pan stick), applied to Move and the cursor.

**Impact:** A paused map's UI keeps moving by itself, and gamepad players (accessibility-first) get a moved cursor and an unwanted walk after tabbing back.

**Recommendation:** Fix it with audit 5: clear `m_stick` and `m_panStick` (and held gamepad d-pad bits) when focus is lost, or skip `SteerCursor` while `m_paused`. Add a unit test next to audit 5's.

**Verification:** SteerCursor (app.cpp:727) runs every loop iteration and I saw no pause or focus gate in the part I read. The stale m_stick mechanism matches backlog item 5 (camera pan stick). I did not verify bindings.cpp or the SendHeld claim. Fixing item 5 should cover this too.

### GAP1-CLOCK-DISCIPLINE-PAUSE-08: DemoPulse counts its pulse from tick 0, so the shader's f32 limit depends on the session's tick count, not the pulse's age

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** precision · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1747`

`DemoGlow` returns `{ .role = DemoPulse }`, so `startTick` is 0, and sprite.vert.hlsl:179 computes `age = float(int(glowTick - glowStart)) + glowFraction`. For this pulse the age is the absolute simulation tick, which is saved and keeps counting across sessions. The f32 holds whole ticks exactly only up to 2^24 ticks (about 77.7 h of simulated time). After that the pulse's phase quantises: at 2^28 ticks the step is 16 ticks, about 6 steps per 96-tick beat. `int()` turns negative after 2^31 ticks. The comment's "three days after the start" is true only when the start is recent.

**Evidence:** shaders/sprite.vert.hlsl:116-118, 178-179; GlowInput::startTick defaults to 0 (core/glow.hpp:165).

**Impact:** Today it affects only the arena's showcase. Any future always-on pulse built the same way (an aura on a long-lived NPC, a shrine) would start to stutter after long play, and ToI-style campaigns run to hundreds of hours.

**Recommendation:** For pulses, either pass a start tick on the CPU that is a whole number of periods before now (startTick = tick - tick % periodTicks), or compute the phase on the CPU in f64 as the seals do. Document that a pulse's startTick must be recent.

**Verification:** DemoGlow returns { .role = role } with no start tick (host.cpp:1745-1747), so startTick is 0 and the shader age is the absolute tick. f32 precision loss beyond 2^24 ticks is arithmetically right. It affects only the arena demo pulse, so it is a latent hazard for future pulses. I did not open the shader to check the age computation.

### GAP1-CLOCK-DISCIPLINE-PAUSE-09: Profile helpers keep firing on wall time while paused, and only the move latency forgets a press on pause

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** measurement · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/app.cpp:520`

On pause `HandleInput` calls `m_moveLatency.Forget()` but not the equivalent for `m_swordsLatency`. `CastOnTimer` and `RepeatBurst` keep sending casts and bursts on wall time while unfocused; the frozen ticks drop them, and `m_nextCastNs` still moves on 4 to 6 s. A sword press made just before or during a short pause is counted against the first volley drawn after the resume, if that comes within `CAST_LATENCY_LIMIT_NS`.

**Evidence:** cast.hpp: CastLatency forgets a press only after 1 s; app.cpp:354-376 has no `m_paused` check.

**Impact:** Profile and measure runs that lose focus for a moment get a skewed latency sample and lost casts. This affects measurement only.

**Recommendation:** Give CastLatency a `Forget()` and call it next to `m_moveLatency.Forget()`. Skip `CastOnTimer` and `RepeatBurst` while `m_paused`.

**Verification:** HandleInput (app.cpp:520-525) forgets only m_moveLatency on pause. CastLatency (cast.hpp:59-75) has no Forget(), only the 1 s CAST_LATENCY_LIMIT_NS prune. RepeatBurst and CastOnTimer (app.cpp:329-330, 345-376) have no m_paused check, so they send casts and bursts while paused, and m_nextCastNs advances. Logic freezes ticks while paused, so the casts are probably dropped or queued. This only affects the --cast-swords and --burst profile/measure options, and the skew needs a press within 1 s of the resume. I did not trace what the logic does with a cast it receives while paused.

### GAP1-CLOCK-DISCIPLINE-PAUSE-10: The same render time is derived four different ways per frame

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** consistency · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/renderer.cpp:478`

`Upload` computes `renderTick` from f64 alpha for seals and flashes. `DrawVolleys` gets alpha narrowed to f32 and recomputes `RenderTick(simulationTick, f64(alpha_f32))`. `LaySeals` floors the f64 for the whole tick. `GlowTimeAt` splits the u64 tick from the alpha. Each is right on its own, but swords, their flashes and their glows are timed by slightly different numbers. A future clock change (for example a pause-held presentation tick) would have to be made in four places.

**Evidence:** renderer.cpp:477-492, volley_draw.cpp:324, renderer.cpp:725, glow_table.cpp:45-51.

**Impact:** Small drift between effects that should be timed alike, and several places to change when the clock policy changes. The pause fix will touch all of them.

**Recommendation:** Make one `RenderTime { u64 wholeTick; f64 fraction; f64 seconds; }` once per frame (from the snapshot, the view and any pause hold) and pass it to seals, flashes, volleys, glows, numbers and the HUD.

**Verification:** Code matches: renderer.cpp:478-492 computes renderTick from f64 alpha and passes f32 alpha to DrawVolleys, which recomputes RenderTick (volley_draw.cpp:324); renderer.cpp:725 floors for seals; GlowTimeAt (glow_table.cpp:45) splits the u64 tick for precision. The f32 narrowing is small. GlowTimeAt is deliberately integer-exact past 2^53 (its own comment), so a single f64 RenderTime would regress that. This is a refactor suggestion, not a defect, and no pause hold exists yet. Low-to-info.
