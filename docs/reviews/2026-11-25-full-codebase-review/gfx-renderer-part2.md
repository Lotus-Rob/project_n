# Review: gfx-renderer (part 2)

### GFX-RENDERER-11: Capture assumes a 4-byte RGBA/BGRA swapchain format without checking it

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/renderer.cpp:1689`

The capture texture uses the swapchain's format, and the readback assumes BYTES_PER_TEXEL = 4 and swizzles only B8G8R8A8_UNORM. Any other format (R10G10B10A2 if composition ever changes, or INVALID when no window is claimed) produces wrong pictures or a confusing creation error.

**Evidence:** renderer.cpp:1679-1745.

**Impact:** Reference pictures silently differ if the swapchain format changes. This is unlikely today because SDR composition is guaranteed.

**Recommendation:** Return an error unless the format is R8G8B8A8_UNORM or B8G8R8A8_UNORM, naming the format.

**Verification:** Capture at 1679-1689 takes the swapchain format and assumes 4 bytes per texel with no format check. The comment says the composite pipeline is built for that format, so the choice is deliberate. A guard returning an error for other formats would be cheap, but the swapchain is SDR in practice.

### GFX-RENDERER-12: CreateShader falls back to MSL without checking the device supports it or that MSL bytes exist

- **Severity:** low · **Verdict:** confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/internal/resources/shaders.cpp:10`

MSL is the default whenever the device reports neither SPIR-V nor DXIL, even if it doesn't report MSL either, or the generated stage has empty MSL (macOS is not built).

**Evidence:** shaders.cpp:198-207.

**Impact:** Players get an opaque 'pipeline rejected' error instead of 'this GPU accepts none of our shader formats'.

**Recommendation:** Check `formats & SDL_GPU_SHADERFORMAT_MSL` and `!code.empty()`, and return a clear error naming the formats the device accepts.

**Verification:** CreateShader (lines 7-33) defaults to MSL and only switches on SPIRV or DXIL. It never checks the MSL bit or whether the stage's code is empty. Cited line 198 is wrong; the code is at lines 10-18. The failure would still be an SDL_CreateGPUShader error, but the message is less clear than it could be.

### GFX-RENDERER-13: A visible frame with a null swapchain still runs the label pass at a 0×0 target

- **Severity:** low · **Verdict:** unverifiable · **Category:** correctness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/gfx/renderer.cpp:1591`

Draw() passes acquired.width and acquired.height (0 when SDL hands back a null swapchain, for example while minimising or occluded on some compositors) to Upload. PlaceTexts then runs LabelPass::Place with a 0×0 frame and advances its fades.

**Evidence:** renderer.cpp:1593-1598, 807-808; camera.cpp clamps to 1, so the result is finite but every label falls off-screen.

**Impact:** Labels may fade out during such frames and then fade back in, a visible flicker on restore, and the work is wasted.

**Recommendation:** When `acquired.swapchain == nullptr`, upload only what must keep in step (glyph fields, instances) and skip text placement and the label pass.

**Verification:** Draw() does pass acquired.width and acquired.height straight into Frame (lines 1585-1597). I did not read how Frame and the label pass handle a null swapchain, or whether SDL returns a 0x0 size in that case. Running it under minimise or occlusion would settle it. If it holds, the impact is a possible label fade glitch.

### GFX-RENDERER-14: SetWorld recomputes RoadCurves and rebuilds every map-derived buffer on a look-only change, on the main thread

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/renderer.cpp:1633`

SetWorld always runs core::RoadCurves(*map) and bumps worldVersion. App calls it on every world_look.yaml reload with the same map pointer. Road curves depend only on the map, yet they are recomputed synchronously, and the ground field, rivers and roads are rebuilt on the next frame.

**Evidence:** renderer.cpp:1633-1638; app.cpp:471-472 calls SetWorld(m_options.data.world, ...) after a look reload.

**Impact:** Each look reload causes a main-thread hitch that grows with map size. When procedural worlds arrive (a new seed each game), the same synchronous path will hitch on world creation.

**Recommendation:** Recompute roadCurves only when the map pointer changes, and track the map version and look version separately. For big maps, build road curves with the map (off the main thread), as core already does for RoadCurveIndex.

**Verification:** SetWorld does recompute core::RoadCurves unconditionally, and app.cpp:471 calls it after a world_look reload with the same map. That is a dev hot-reload path, and the rivers and roads are rebuilt anyway because the look sets their widths. The saved cost is only the curve recomputation. At world creation the cost is paid once and is expected, and the memory file already says per-world derived data is built at world creation. The hitch is minor and arguably not worth a change.

### GFX-RENDERER-15: Holds() walks the scene's sprite list about 6 times a frame, each time with a profile zone

- **Severity:** info · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/renderer.cpp:1331`

Upload, UploadTerrain, UploadGround, UploadRoute, UploadClouds and DrawWorld each call Holds(snapshot.scene), which iterates the scene's sprite IDs checking `resident.at()`, under a PN_PROFILE_ZONE.

**Evidence:** renderer.cpp:460, 477, 587, 611, 674, 702, 1191, 1331-1336.

**Impact:** This is small now, but it is redundant work that grows with scene sprite counts, and the zone overhead (about 0.25 µs each, per src/AGENTS.md) adds up.

**Recommendation:** Compute `held` once per frame in Upload (or cache the set of held scenes, updated in ContinueScene) and pass it down.

**Verification:** Holds() is called at lines 460, 477, 587, 611, 674, 702 and 1191, so about 7 calls per frame, each with a profile zone and an all_of over the scene sprites. The cost is negligible; it is a tidy-up.

### GFX-RENDERER-16: Per-frame rebuild of the named-label stack-width hash map

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/renderer.cpp:900`

GatherLabels calls `measured.assign(strings->text.size(), {})` and `measuredNames.clear()` every frame. measuredNames is a std::unordered_map, so each named label re-inserts a node (a heap allocation) every frame.

**Evidence:** renderer.cpp:900-901 and 968-970.

**Impact:** Allocation churn grows with the number of NPC labels in view on the world map, where crowds of travellers are labelled.

**Recommendation:** Use a flat map (sorted vector or open addressing) that keeps its capacity, or tag entries with a frame number instead of clearing.

**Verification:** Lines 900-901 do assign() and clear() every frame. The unordered_map clear() keeps its buckets, but each insert of a named label allocates a node, so the churn is real. I did not measure it, and the claim that it grows with the crowd is plausible but unproven.

### GFX-RENDERER-17: Renderer::Impl is a god object; label budgeting lives in the renderer

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** architecture · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/gfx/renderer.cpp:199`

Impl holds the device, every feature renderer, the world-map state, text-frame state, label-budget state (measured, measuredNames, waitingLabels, waitingInputs, stackCount ...), 15 RepeatedFailure counters and the upload bookkeeping. Upload() ends in a five-level nested core::Combine pyramid.

**Evidence:** renderer.cpp:321-454 member list; :560-575 Combine nesting; :889-987 label budgeting.

**Impact:** The file is hard to change safely and the text logic can't be unit tested. Every new feature adds members, an Upload branch, a Submitted line and an overflow counter by hand, so it is easy to forget one (Submitted() already omits several features by design).

**Recommendation:** Split it into WorldMapLayer (ground, rivers, roads, terrain, route, cursor, clouds and their keys), TextLayer (label budgeting, PlaceTexts) and a common feature interface with Upload/Submitted/Draw. Use a variadic Combine and an overflow table.

**Verification:** This is an architecture opinion, and the facts hold: Impl is large and Upload has a nested Combine. The status field already says known-deferred. I did not check the sweep-up backlog. It is a maintainability concern, not a defect.

### GFX-RENDERER-18: Capture() mutates persistent renderer state and is not safe between Acquire() and Draw()

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** api-design · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/renderer.cpp:1672`

Capture resets the live LabelPass and CaptionFades, overrides and then hard-resets the layout budget to LAYOUT_BUDGET_NS, resizes the shared scene target to the capture size, and runs Upload/Submitted against the same bookkeeping as live frames. These resets happen even when it fails early (texture or transfer creation). Nothing guards against a call while a frame is acquired.

**Evidence:** renderer.cpp:1672-1673, 1699-1701, 1710 (DrawPasses fits sceneTarget to width × height).

**Impact:** Using it from a debug screenshot key in the real game would reset label fades, cost a scene-target reallocation on the next frame, and interleave with an acquired frame's uploads. It is a test-only API on the production class.

**Recommendation:** Either assert `!m_impl->frame` and document Capture as harness-only, or give Capture its own label and caption state and scene target. Consider moving it behind a test-support interface.

**Verification:** Capture (1670-1710) does reset the LabelPass and CaptionFades, set the layout budget to max, and fit the shared sceneTarget through DrawPasses. Design 0002.4.1 section 4 says Capture deliberately uses the same scene target and passes so that reference pictures judge the shipped path. Capture is harness-only by design, so the shared state is deliberate. A missing assert for a call while a frame is acquired is a valid but minor point.

### GFX-RENDERER-19: Frames in flight rely on SDL's default; the logs lack the GPU adapter name and driver version

- **Severity:** info · **Verdict:** confirmed · **Category:** frame-pacing · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/renderer.cpp:1393`

Acquire()'s documentation promises the GPU has 'its two frames in flight', but SDL_SetGPUAllowedFramesInFlight is never called, so that depends on SDL's default. The start-up log names only the backend (vulkan, d3d12). SDL 3.4's SDL_GetGPUDeviceProperties gives the device name and driver version, which bug reports need.

**Evidence:** renderer.hpp:97-98; renderer.cpp:1393-1401.

**Impact:** Pacing and latency can change silently with an SDL bump, and user logs can't tell which GPU or driver was used.

**Recommendation:** Call SDL_SetGPUAllowedFramesInFlight(device, 2) explicitly, and log SDL_PROP_GPU_DEVICE_NAME_STRING and the driver name and version properties.

**Verification:** grep finds no SDL_SetGPUAllowedFramesInFlight anywhere under src/gfx. renderer.hpp:97 says 'its two frames in flight', which relies on SDL's default. The start-up log prints only SDL_GetGPUDeviceDriver and the present mode. Adapter name and driver version are not logged. The finding is valid but minor.

### GFX-RENDERER-20: No player-facing present-mode or frame-cap choice

- **Severity:** info · **Verdict:** deliberate · **Category:** design · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `src/gfx/renderer.cpp:1377`

The present mode is fixed at Create (mailbox, then immediate, then vsync), with only `--unpaced` to reorder. There is no settings option for vsync or a frame-rate cap, and no runtime change.

**Evidence:** renderer.cpp:1377-1395; PresentMode::Vsync is never produced by PresentModeOrder (a dead enumerator).

**Impact:** Laptops and handhelds run the GPU flat out on menus and the paused world map (heat, battery, coil whine). Mailbox also renders many frames that are never shown. Players expect a vsync toggle.

**Recommendation:** Keep uncapped as the default, but add a settings option (vsync or a cap) applied through SDL_SetGPUSwapchainParameters at runtime. Drop or use the Vsync enumerator.

**Verification:** docs/design/0002-engine-architecture.md:224 says 'Vsync and a frame cap become player settings, off by default', and S10 (line 547) measures it. So the settings option is a recorded plan, not a contradiction. renderer.hpp:45 documents that Vsync is not listed in PresentModeOrder because it is the fallback. The enumerator is used for naming and fallback, so it is not dead. Whether the settings work has been done is not checked here.
