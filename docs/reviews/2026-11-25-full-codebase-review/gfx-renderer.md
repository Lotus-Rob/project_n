# Review: gfx-renderer

**Scope:** src/gfx/renderer.{hpp,cpp}, src/gfx/internal/gpu.hpp, src/gfx/internal/frame/*, src/gfx/internal/resources/*, uploads.hpp, image.cpp, camera.hpp. Focus additionally on SDL3 GPU API correctness, resource lifetime, device loss, swapchain/resizing, frame pacing. Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 25 standing (0 critical, 0 high, 1 medium, 12 low, 12 info), 0 raised but refuted on verification.

**Parts:** gfx-renderer.md, gfx-renderer-part2.md, gfx-renderer-part3.md

## Assessment

## gfx-renderer: assessment

**Strengths**
- The SDL_GPU usage is careful and mostly correct. The device is declared first so it is destroyed last. Every GPU object is owned through `Gpu<>` deleters bound to their device. The swapchain rules are followed: a null swapchain texture still gets its command buffer submitted, a command buffer is cancelled only when no swapchain texture was acquired, and only the acquiring thread acquires. Cycling is used where frames in flight read the same buffers (instances, scene target), and deliberately not used for partial atlas and glyph-page writes.
- Upload bookkeeping is honest. Snapshot instances, terrain, the glow table, glyph fields, the ground, rivers, roads and fog all count as held only after their command buffer is submitted (`Submitted()`), so a failed submit retries. The per-buffer overflow logging (`RepeatedFailure`) logs once per run, which suits a game loop.
- Frame pacing is well thought out: acquire first, read alpha after, mailbox or immediate with vsync as the fallback, and one scene-target fit per frame. The scene target and sRGB encoding in the composite keep blending linear.
- Scene loading is incremental (decode on jobs, upload within a budget, mips last), and the sprite array is cleared before first use.
- The camera maths is small, pure and well tested, including the zero-size target.

**Weaknesses**
- **Snapshot identity is unsound.** The renderer skips the instance upload for a snapshot ID `(epoch, wallTick)` it already holds. The logic thread publishes snapshots with an unchanged `wallTick` during scene-change waits, and their content differs because culling follows the view. So new batches and indices can be drawn against an old instance buffer.
- **No device-loss story.** Every GPU failure is logged once, and then the game runs on behind a frozen or black window forever. `Draw()` returns void, so App can't react.
- **The world-map ribbons don't scale with the map.** Rivers and roads are built for the whole map into fixed 8,192-quad buffers and drawn uncull'd every frame. That conflicts with the per-seed procedural, possibly very large, maps the project is aiming for.
- **The Impl is a god object.** It is about 100 members: resources, features, world-map state, text and label budgeting, and 15 failure counters. About 350 lines of label-budget logic live inside it and can only be tested through GPU reference pictures (the size issue is known-deferred).
- **Test coverage of the live frame path is missing.** Every GPU test goes through `Capture()`. `Acquire()`/`Draw()`/`Frame()`, null-swapchain frames, submit-failure retries, resizing, and the snapshot-ID reuse above are never exercised. `uploads_tests` checks only the trivial record/submit cases.
- **Some work is wasted.** `Holds()` is recomputed about 6 times a frame. Texts, labels and the HUD are laid out while a scene loads and only the backdrop draws. A named-label hash map is rebuilt every frame. The whole atlas array's mips are regenerated on every scene load. Every atlas page sits in VRAM from start-up.

**Overall health:** good for the current scale and content. The code is careful about SDL_GPU's rules and about retrying uploads. The main risks are:
1. The snapshot-identity assumption, which causes a visible glitch during scene changes, loads and replays.
2. Silent device loss or persistent failure.
3. Scaling of the map-wide ribbon buffers and the always-resident atlas once worlds are procedural and large.
4. The live frame path is not covered by any test.

**Files read in full:** src/gfx/renderer.hpp, src/gfx/renderer.cpp, src/gfx/internal/gpu.hpp, src/gfx/internal/frame/frame.hpp, src/gfx/internal/frame/frame.cpp, src/gfx/internal/resources/png.hpp, src/gfx/internal/resources/scene_target.hpp, src/gfx/internal/resources/scene_target.cpp, src/gfx/internal/resources/shaders.hpp, src/gfx/internal/resources/shaders.cpp, src/gfx/internal/resources/sprite_array.hpp, src/gfx/internal/resources/sprite_array.cpp, src/gfx/uploads.hpp, src/gfx/image.hpp, src/gfx/image.cpp, src/gfx/camera.hpp, src/gfx/camera.cpp, src/gfx/internal/features/composite.cpp, tests/unit/gfx/uploads_tests.cpp, tests/unit/gfx/present_mode_tests.cpp, tests/unit/gfx/image_tests.cpp, tests/unit/gfx/camera_tests.cpp

## Findings

### GFX-RENDERER-01: Rivers and roads are built for the whole map into fixed 8,192-quad buffers and drawn uncull'd every frame: doesn't scale to large procedural maps

- **Severity:** medium · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/river_draw.hpp:22`

UploadGround() builds every river and road of the map once per SetWorld (DrawRivers/DrawRoads over the whole map) into LineRibbonRenderers sized MAX_RIVER_QUADS = MAX_ROAD_QUADS = 8,192. LineRibbonRenderer::Draw issues the whole buffer every frame, whatever the view. Rivers and roads past the cap are 'left out whole' (logged).

**Evidence:** renderer.cpp:616-628; river_draw.hpp:22 and road_draw.hpp:19 `= 8'192`; line_ribbons.cpp:99-124 draws m_quads with no culling.

**Impact:** Maps are procedural per seed and may be very large. Past a modest size, rivers and roads silently vanish (whole features cut at the cap), and the vertex cost scales with the map, not the view. The ground field and fog texture (a texel per tile) will also hit the 16,384-texel texture limit on large maps.

**Recommendation:** Chunk the ribbons by map region (for example 32×32-tile chunks with their own ranges in a growable buffer) and draw only the chunks the view reaches. Size buffers from the map, or grow them, rather than using a constant. Tile the ground and fog textures, or use texture arrays, for maps past the texture size limit.

**Verification:** The facts hold. MAX_RIVER_QUADS and MAX_ROAD_QUADS are 8'192, DrawRivers and DrawRoads build the whole map in UploadGround on worldVersion change, and rivers past the cap are dropped whole and logged (river_draw.cpp:150). The memory file procedural-world-map.md says maps may be 2x to 50x larger, so this is a real future scaling limit. It is not a current bug, since the test map fits. I found no recorded plan for chunking the ribbons. Caveats: the log is not 'silent', and the 16,384 texture-limit claim is unverified. No documented deliberate decision.

### GFX-RENDERER-02: Snapshot identity (epoch, wallTick) is reused by waiting bursts, so changed instances are not uploaded and new batches draw against a stale buffer

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/logic.cpp:24`

Upload() keys the instance upload on SnapshotId{epoch, wallTick} (`newInstances = !uploaded.Holds(shownId) && Holds(scene)`), relying on uploads.hpp:16's claim that 'wall ticks never repeat within an epoch'. But core::Logic::RunBurst (core/logic.cpp:24-81) still publishes a snapshot when `burst.ticks > 0 && Waiting()`. It calls Wait(), which runs no RunTick and never calls m_counters.Count(), so wall doesn't advance. Each burst during a scene change's Leaving, Loading or Entering stage therefore publishes with the same wallTick. Its content can differ: Simulation::Publish culls to the latest ViewReport, which the camera moves every drawn frame, and debugSight and animations also come from the view. Display::Update adopts each such snapshot as new.

**Evidence:** renderer.cpp:459-460 and :552-555 (Recorded only when newInstances); uploads.hpp:16 doc; logic.cpp:25-27 `if (burst.ticks > 0 && Waiting()) { end = Wait() ? ... }` with no Count, then `snapshot.wallTick = m_counters.wall;` and a full republish; Wait() (logic.cpp:161-201) never counts.

**Impact:** At every scene change (portals, F2, save loads, replay starts), each frame while the logic waits can draw the new snapshot's batches, `selected` and hover indices against the GPU buffer of an earlier snapshot. SpriteRenderer clamps to m_count, so this is no out-of-bounds read, but wrong sprites, positions or outlines flash for a few frames. No test catches it: uploads_tests only checks record/submit, and reference tests capture one fresh snapshot each.

**Recommendation:** Give the Snapshot a publish sequence number that increments on every Publish (never saved or hashed) and key UploadedSnapshot on it. Or make the renderer compare the instance span cheaply (size plus hash). Add a unit test: two publishes from waiting bursts must get distinct IDs.

**Verification:** The mechanism holds. RunBurst publishes after a Wait()-only burst without calling m_counters.Count, so wallTick repeats, and the uploads.hpp:16 doc ('never repeat within an epoch') is false for those bursts. The impact is overstated. While the scene is Loading, Holds(scene) is false, so no upload is attempted at all. After Entering, the first publish with the new scene's instances is the one that gets uploaded. A later waiting burst at the same wall tick differs only by culling or animation from the view. The instances are not stale in kind, and the claimed 'wrong sprites flash' is at most a culling edge for a few frames. The doc claim and the key are still wrong, and a sequence number is a cheap fix. I found no test for it.

### GFX-RENDERER-03: No escalation or recovery on device loss or persistent frame failure: the game runs on behind a frozen or black window

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/renderer.cpp:1319`

Acquire() and Draw() return void. Every failure (no command buffer, no swapchain texture, submit failure, scene target creation) goes through Report(), which logs only the first failure of a run (RepeatedFailure). SDL_GPU has no device-lost event: after a TDR, driver crash or eGPU unplug, every acquire or submit fails. The renderer never tells App, never retries with a new device, and never stops the game.

**Evidence:** renderer.hpp:100 `void Acquire();` 'A failure is logged, and the Draw() after it does nothing'; renderer.cpp:1319-1329 Report() only logs; App::Present (app.cpp:822-890) cannot see a failure.

**Impact:** After a GPU reset the simulation keeps running (combat, travel, months) with nothing on screen and no message. The player can lose progress or face an unexplained frozen window. Only the first error reaches the log.

**Recommendation:** Count consecutive failed frames or failure time. Past a threshold (for example 2 s), return an error from Draw()/Acquire() or expose `Renderer::Failing()`. App can then pause the logic, autosave and either recreate the Renderer (Create again, re-upload resident scenes) or quit with a message box.

**Verification:** Report() only logs. RepeatedFailure logs the first failure and a 'drawn again after N failed' warning on recovery, so more than the first error is reported. No escalation exists: Acquire and Draw return void and App cannot see a failure. I found no device-loss handling or documented decision in docs, AGENTS.md or the memory files. The gap is real but a robustness feature rather than a defect. SDL_GPU offers no device-lost callback, and the game logic is separate from rendering, so 'keeps running blind' is accurate. The claim that the player can lose progress is speculative.

### GFX-RENDERER-04: A frame acquired before a failing LoadScene is never submitted; the renderer is destroyed holding an acquired swapchain image

- **Severity:** low · **Verdict:** confirmed · **Category:** sdl-gpu-correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/renderer.cpp:1567`

core::PresentFrame calls acquire() and then prepare(). When App::LoadScene fails (prepare returns false), Draw() is never called, and App::Run returns 1. m_impl->frame still holds a command buffer with a swapchain texture acquired. SDL forbids cancelling it and requires a submit. ~Impl doesn't submit it before releasing the window and destroying the device.

**Evidence:** frame.hpp PresentFrame: `if (acquired) acquire(); ... if (!prepare(*drawn)) return false;`; app.cpp:331-332 `if (!Present(state)) return 1;`; Renderer::~Renderer() = default; Impl has no handling of `frame`.

**Impact:** On that exit path, the swapchain (and its acquire semaphore) is destroyed with an acquisition pending. That is a Vulkan validation error and possibly a driver complaint or hang at exit. If any caller ever continued after it, the next Acquire() would hit the PN_ASSERT.

**Recommendation:** In ~Impl, or in a Renderer::Abandon() that App calls on failure, submit a held frame (clearing the swapchain to the backdrop) before releasing anything. Alternatively, run prepare before acquire when a scene load is pending.

**Verification:** PresentFrame calls acquire() early when display.Drawn() is non-null, then calls prepare(). If prepare returns false (LoadScene fails), the function returns false before draw. App then returns 1. AcquireFrame uses SDL_WaitAndAcquireGPUSwapchainTexture, so a swapchain texture is held. Impl's destructor is defaulted and nothing submits or cancels the held Frame. The failure is on an error-exit path only, so it is validation noise at worst, which is why I rate it low. No test covers it.

### GFX-RENDERER-05: The live frame path (Acquire/Draw/Frame/Submitted) has no test; all GPU tests go through Capture()

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/gfx/uploads_tests.cpp:10`

Reference tests call only Renderer::Capture. Nothing runs Acquire()+Draw(), a null swapchain texture, a resize (FitSceneTarget recreation), a failed upload followed by a draw, or the submit-failure retry of terrain, glyphs and glow. uploads_tests covers only 'recorded not submitted' and 'recorded and submitted'. It doesn't cover a second Recorded() before Submitted, a failed submit keeping the earlier held snapshot, or ID reuse (finding 1). Camera, image and present-mode tests are good but trivial. The label-budget logic in Renderer::Impl (GatherLabels, TakeTurns, StackOf: about 150 lines of state machine) is reachable only through GPU reference pictures.

**Evidence:** grep: no test calls `.Acquire()` or `renderer.Draw`; reference_tests.cpp:239 uses Capture only.

**Impact:** Regressions in frame submission, resizing and retry, which are exactly the paths that differ from Capture, ship unnoticed. The snapshot-ID bug above was not caught.

**Recommendation:** Add a headless (software Vulkan, as `reference` already uses) test that runs several Acquire/Draw frames with a resize and a repeated snapshot and checks the logged failures and pictures. Extract the label-budget state machine into a GPU-free class with unit tests, as was done for LabelPass and UploadedSnapshot.

**Verification:** Mostly true. The gfx unit tests cover uploads, camera, image and labels (a labels_tests.cpp exists, so the label logic is not entirely GPU-only as claimed) but nothing exercises the live Acquire and Draw path. I did not grep every tests directory for Acquire. Reference tests go through Capture. A headless Acquire/Draw test would add value, but a GPU-free extraction of GatherLabels is a suggestion rather than a defect. Medium is too high for a test-coverage gap.

### GFX-RENDERER-06: Scene load holds every decoded sprite in memory before the first upload

- **Severity:** low · **Verdict:** unverifiable · **Category:** scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/renderer.cpp:1519`

ContinueScene() returns false until `load.decoding.Done()`, that is until every missing sprite has been decoded (RGBA surface plus distance field). Only then do uploads start, one slot freed at a time.

**Evidence:** renderer.cpp:1519-1521 `if (!load.decoding.Done()) return false;`; SceneLoad::decoded holds one std::expected<DecodedSprite> per sprite.

**Impact:** Peak RAM equals all of a scene's decoded sprites plus fields, and load latency is the slowest decode plus every upload. As scene sprite lists grow toward real content (hundreds of large paintings), this becomes hundreds of MB and seconds of load.

**Recommendation:** Upload each slot as soon as it is decoded (an atomic done flag per slot, uploads in order or as available), so memory stays bounded by the decode-ahead window and uploads overlap decoding.

**Verification:** The claim that uploads wait for `load.decoding.Done()` matches the cited design, and sweep-up-backlog.md:27 records asset loading as an open item for heavier scenes (a 2 ms upload budget per frame, a decode timeout missing). Peak RAM and latency at hundreds of large paintings have not been measured. The repo's convention, 'unknowns are measured spikes', means a measurement would decide it. Today's scenes are small.

### GFX-RENDERER-07: Whole atlas allocated in VRAM at start-up, never evicted; every scene load regenerates the mips of every page

- **Severity:** low · **Verdict:** deliberate · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/internal/resources/sprite_array.cpp:286`

CreateSpriteArray allocates all manifest.pages layers (colour RGBA8 plus R8 field, all mips) at Create, and resident sprites 'stay'. GenerateMips calls SDL_GenerateMipmapsForGPUTexture on both whole arrays, every layer, after each scene load, even when only one page received sprites.

**Evidence:** sprite_array.cpp:187-190 (layer_count_or_depth = manifest.pages); :291-292 whole-texture mip generation; renderer.hpp:61-62 'load into the atlas when the game needs them, and stay'.

**Impact:** This is fine at 4 pages of 2048² (about 110 MB). As art grows, VRAM grows with the entire game's art rather than the current scene, and each load's mip blits grow with the atlas.

**Recommendation:** Generate mips only for the layers touched by the load (blit per layer and level, or a compute downsample per page). Plan page residency per scene (allocate or evict pages) before the atlas grows past what low-end GPUs hold.

**Verification:** sweep-up-backlog.md:27 records both points explicitly: 'GenerateMips rebuilds the whole atlas array after each load, not just the new sprites; nothing is ever unloaded.' It is a known deferral that waits for heavier scenes. renderer.hpp:61-62 also states that sprites stay resident by design.

### GFX-RENDERER-08: A failed scene-target fit presents an undrawn swapchain image

- **Severity:** low · **Verdict:** confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/renderer.cpp:1158`

When FitSceneTarget fails (for example out of memory on a huge resize or fullscreen switch), DrawPasses returns before any render pass touches the swapchain texture. The command buffer is then submitted, presenting a swapchain image with undefined contents.

**Evidence:** renderer.cpp:1158-1160 returns the error before DrawWorld/DrawFinal; Frame() then calls SubmitFrame.

**Impact:** Garbage or flashing frames are shown for as long as the failure lasts, and the error is logged only once.

**Recommendation:** On a fit failure, still begin a render pass on the swapchain with LOADOP_CLEAR to the backdrop colour before submitting.

**Verification:** DrawPasses (renderer.cpp:1149-1160) returns on FitSceneTarget failure before DrawFinal, the only pass touching the swapchain. Frame() (1297-1316) still calls SubmitFrame, and the error is logged once via the repeated-failure counter. DONT_CARE load in DrawFinal confirms the image is undefined. It needs an allocation failure, so it is rare.

### GFX-RENDERER-09: Texts, labels and the HUD are laid out every frame while the scene isn't held, though only the backdrop draws

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/renderer.cpp:467`

Upload() runs PlaceHudLines and PlaceTexts (label gathering, the label pass, NumberFaceOf, PlaceNumbers on jobs) and uploads the mesh quads and glyph fields whatever the scene state. DrawWorld returns right after the clear when `!Holds(snapshot.scene)`. The label pass also advances its fades and glides on the frame clock for labels nobody sees.

**Evidence:** renderer.cpp:467-474 (unconditional on Holds) versus :1191-1195 (early return).

**Impact:** CPU and layout budget are wasted during every scene load, the time when the frame budget matters most (the 2 ms upload step), and the HUD vanishes during loads although it was laid out. This goes against the repo's 'avoid wasted work' rule.

**Recommendation:** Skip PlaceTexts and the mesh upload when the scene isn't held, or draw the HUD (and only the HUD) in that case if it should stay visible.

**Verification:** Upload (460-477) runs PlaceHudLines and PlaceTexts whatever Holds() says, while DrawWorld returns after the clear at 1191. Design 0002.3.1 line 178 says only that no instances are uploaded for an unheld scene. It says nothing about texts, so this is not a recorded decision. The wasted layout is bounded to the load window.

### GFX-RENDERER-10: Glyph uploads ignore the D3D12 alignment the sprite upload honours, and create a new transfer buffer every frame

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/renderer.cpp:1091`

UploadGlyphs packs fields tightly at arbitrary offsets and rows of the glyph's width. sprite_array.cpp aligns its field offset to 512 bytes because 'D3D12 wants a texture upload's offset into its buffer to be a multiple of this', and SDL documents a temporary-copy slow path for unaligned D3D12 uploads. A new transfer buffer is also created whenever any glyph is pending.

**Evidence:** renderer.cpp:1091-1124 versus sprite_array.cpp:89-90 and 248.

**Impact:** On Windows D3D12 (older or integrated hardware), every glyph upload takes SDL's copy path, and the first frame uploads every prewarmed glyph this way. The repeated transfer buffer allocation also adds churn.

**Recommendation:** Align each glyph's offset to 512 and its row pitch to 256 (or batch glyphs into an aligned staging layout). Keep a persistent, growable upload transfer buffer cycled per frame, as SpriteRenderer does.

**Verification:** UploadGlyphs (1091-1124) packs fields tightly at offsets with no alignment and creates a transfer buffer per call. sprite_array.cpp:15 defines UPLOAD_ALIGNMENT=512, so the inconsistency is real. The D3D12 slow-path cost is unmeasured, and the transfer buffer is only created when glyphs are pending, not every frame.
