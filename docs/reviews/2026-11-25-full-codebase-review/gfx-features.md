# Review: gfx-features

**Scope:** src/gfx/internal/features/* (sprites, seals, meshes, ground, ribbons, line_ribbons, composite, ...). Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 19 standing (0 critical, 0 high, 1 medium, 13 low, 5 info), 0 raised but refuted on verification.

**Parts:** gfx-features.md, gfx-features-part2.md

## Assessment

## gfx-features (src/gfx/internal/features/*): assessment

**Overall health: good, with a lot of duplication and a weak safety net between C++ and the shaders.** These seven feature files are thin SDL_GPU wrappers: pipelines, buffers and upload/draw calls. The geometry and policy are pushed into pure, unit-tested modules (`seal_draw`, `river_draw`, `road_draw`, `ground_field`, `fog`, `mesh_vertex`). That split is the right one. Most of the code is short, readable and consistent.

### Strengths
- **Error handling is disciplined.** Every create, map and upload failure returns `std::expected` with SDL's error text. Overflows are counted and returned rather than silently truncated. Partial-failure paths reset the draw runs, so stale or undefined data is not drawn.
- **Uploads count only once submitted.** A recorded-but-unsubmitted copy is not treated as held (`m_held`/`m_recorded`), and the renderer calls `Submitted()` on every path, including a cancelled capture. `LineRibbonRenderer` applies this even to its static index buffer, which fixes there the known-deferred audit item 1.
- **Buffer cycling is used correctly everywhere.** Comments explain why (frames in flight).
- **Uniform layouts carry `static_assert`s.** The sprite, seal and light uniforms have size asserts, and the shared enums and flags (glow roles, drives, curves, fog flags, seal kinds) are pinned to C++ constants.
- **The shipped content is well covered by reference pictures** (world map, fog, seals, text, glows), so a visual regression on the happy path is caught.

### Weaknesses
1. **C++ and HLSL are kept in step by hand.** Every `SHADER_*` constant and every cbuffer struct is a hand copy of the shader. The `static_assert`s only compare C++ with C++, so a change made only on the shader side still compiles. Reflection already exists in `shaders.hpp` (`Stage` resource counts) and could be extended to carry cbuffer sizes and constants.
2. **Duplication.**
   - Seven near-identical pipeline builders, and the premultiplied/additive blend state written out about eight times.
   - Four clamp-sampler builders and about six transfer-buffer builders.
   - `MeshRenderer` and `RibbonRenderer` are the same class.
   - The held-once-submitted pattern is written out by hand five times, although a tested `gfx::UploadedSnapshot` already does exactly this.
   - The index-upload fix exists in only one of the four places that need it.
3. **Wasted work.**
   - Six `SpriteRenderer`s each build their own identical pair of pipelines: 12 pipelines and 24 shader objects where 2 and 2 would do.
   - Every river and road quad on the whole map is drawn every frame, whatever is in view.
   - A failed submit re-runs `GroundTexels` and the river/road layouts even though their results are kept.
4. **Scaling.** The design assumes small maps, but maps are procedural and may be very large. River and road buffers are fixed at 8,192 quads and never grow, so rivers and roads past that disappear. Ground and fog use one texel per tile, so they are bound by the GPU's maximum texture size (4,096 is the Vulkan guaranteed minimum). The ground field is rebuilt single-threaded on the main thread for the whole map on every `SetWorld`, look reloads included.
5. **Failure modes are not uniform.** If the fog texture can't be made, the frame draws without fog and reveals the whole map; the ground instead draws nothing. Seals never sample the fog at all.
6. **Tests.** No unit test touches `internal/features`. The reference tests drive only the successful capture path. None of the resubmit, cancel, map-failure, resize or overflow logic is exercised, and that is exactly where the known-deferred index bug lives.

### Risks
- **Highest:** a silent mismatch between C++ and HLSL after a shader edit; the known-deferred permanent index-buffer corruption after one failed submit; and losing rivers, roads, ground or fog on large procedural maps.
- None of these threatens the simulation's determinism: everything here is presentation only.

### Recommended direction
- Factor out a `SubmitTracked<Key>` helper, shared index uploads, and one pipeline/blend/sampler factory.
- Share the sprite pipelines between the sprite renderers.
- Generate the shader constants and cbuffer sizes into `gfx/generated/shaders.hpp`.
- Make the river/road buffers grow, as `SealRenderer::Reserve` does, or chunk and cull them by region.
- Add fault-injection tests for the upload state machines.

**Files read in full:** src/gfx/internal/features/sprites.hpp, src/gfx/internal/features/sprites.cpp, src/gfx/internal/features/seals.hpp, src/gfx/internal/features/seals.cpp, src/gfx/internal/features/meshes.hpp, src/gfx/internal/features/meshes.cpp, src/gfx/internal/features/ribbons.hpp, src/gfx/internal/features/ribbons.cpp, src/gfx/internal/features/line_ribbons.hpp, src/gfx/internal/features/line_ribbons.cpp, src/gfx/internal/features/ground.hpp, src/gfx/internal/features/ground.cpp, src/gfx/internal/features/composite.hpp, src/gfx/internal/features/composite.cpp, src/gfx/internal/gpu.hpp, src/gfx/mesh_vertex.hpp, src/gfx/ribbon.hpp, src/gfx/uploads.hpp, tests/unit/gfx/meshes_tests.cpp, src/AGENTS.md, src/gfx/renderer.cpp (lines 440-740, 1080-1330, 1690-1751), src/gfx/seal_draw.hpp (parts), src/gfx/ground_field.cpp, src/gfx/fog.cpp (FogUniformsOf), src/app/fog_fade.cpp (parts), src/gfx/internal/resources/shaders.cpp, shaders/seal_light.vert.hlsl (parts), shaders/*.hlsl cbuffer declarations

## Findings

### GFX-FEATURES-01: Quad index buffers are marked uploaded when recorded, not when submitted (meshes, ribbons, seals)

- **Severity:** medium · **Verdict:** confirmed · **Category:** correctness · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/gfx/internal/features/meshes.cpp:155`

MeshRenderer::Upload (meshes.cpp:155-160), RibbonRenderer::Upload (ribbons.cpp:111-117) and SealRenderer::Upload (seals.cpp:271-276) set m_indicesUploaded = true as soon as UploadQuadIndices records the copy. If that command buffer is then cancelled (the capture path's AfterUpload cancels on any upload error) or fails to submit, the static index buffer is never filled and is never uploaded again. LineRibbonRenderer already does this correctly (m_indicesRecorded, then m_indicesHeld in Submitted(), line_ribbons.cpp:66-72 and 92-96), so the fix exists in the module but wasn't applied to the other three.

**Evidence:** meshes.cpp:155 `if (!m_indicesUploaded) { if (!UploadQuadIndices(...)) ...; m_indicesUploaded = true; }`, the same in ribbons.cpp:111 and seals.cpp:271; line_ribbons.cpp:88-97 tracks indices through Submitted().

**Impact:** If the first frame or capture is cancelled or fails to submit, all text, sword trails and seals draw from an uninitialised index buffer for the rest of the run (garbage triangles or nothing), with no further error.

**Recommendation:** Use LineRibbonRenderer's recorded/held pair in all three, ideally through one shared `QuadIndexBuffer` type that owns the buffer, its upload and its submit tracking. Each renderer would then hold one instead of repeating the logic.

**Verification:** meshes.cpp:155, ribbons.cpp:111 and seals.cpp:271 set m_indicesUploaded right after recording. LineRibbonRenderer uses recorded/held via Submitted(). sweep-up-backlog.md item 1 (line 89) records the same defect and notes it only bites on the first recording frame. Known deferral, no test covers it.

### GFX-FEATURES-02: Shader constants and cbuffer layouts are mirrored by hand; nothing checks them against the HLSL

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/internal/features/sprites.cpp:31`

Every SHADER_* constant (sprites.cpp:31-42 and 67-69, seals.cpp:48-61, ground.cpp:21-24) and every uniform struct (DrawUniforms, SealUniforms, SealLightUniforms, SealLookUniforms, GroundPlace, BandUniforms, TextStyle) is a hand copy of an HLSL declaration. The static_asserts compare these copies with other C++ values, never with the shader. The `shaders` tool already reflects resource counts into gfx/generated/shaders.hpp (Stage::samplers, storageBuffers, uniformBuffers), but it doesn't emit cbuffer sizes, member offsets or `static const` values.

**Evidence:** sprites.cpp:31 `constexpr std::size_t SHADER_GLOW_ROLES = 8; // sprite.vert.hlsl's GLOW_ROLES`; static_assert(core::GLOW_ROLES == SHADER_GLOW_ROLES). If sprite.vert.hlsl's GLOW_ROLES or the `Draw` field order changes, the build still passes. tools/project_n.py has no check that ties them together.

**Impact:** A shader-only edit (reordering a cbuffer field, changing a constant) builds cleanly and draws wrongly. Only a reference picture might catch it, and only for content the pictures show.

**Recommendation:** Extend the `shaders` step to emit each cbuffer's size and member offsets (from SPIR-V reflection, which spirv-cross already provides) and the shared `static const` values into generated/shaders.hpp. Then static_assert the C++ structs and constants against the generated values.

**Verification:** The SHADER_* constants and uniform structs are hand mirrors, and the static_asserts only compare C++ values. I did not look for a generator check and found none cited. It is a real robustness gap. Medium is high for a hardening idea, and the reference pictures partly catch it.

### GFX-FEATURES-03: Fixed 8,192-quad river and road buffers never grow; on large procedural maps rivers and roads silently disappear, and every quad is drawn every frame

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/internal/features/line_ribbons.cpp:73`

LineRibbonRenderer is created with a fixed maxQuads (MAX_RIVER_QUADS and MAX_ROAD_QUADS = 8,192, renderer.cpp:656/662). DrawRivers and DrawRoads drop whole rivers and roads past that, with a log warning. Maps are procedural per seed and meant to scale. Each river tile takes RIVER_STEPS segments times two bands, so a map a few times the shipped one runs out. Meanwhile Draw issues every quad of the whole map every frame, whatever the view covers.

**Evidence:** river_draw.cpp:150 `if (quads + taken > MAX_RIVER_QUADS) { PN_LOG_WARNING(...); break; }`; line_ribbons.cpp:124/127 draw all edgeQuads and core quads each frame. SealRenderer already has growable buffers (Reserve/SealBufferRoom), but LineRibbonRenderer does not use that pattern.

**Impact:** Missing rivers and roads on larger worlds, a visible correctness problem. The vertex work grows with map size rather than view size, against the project's 'avoid wasted work' rule.

**Recommendation:** Size the buffers from the layout (grow like SealRenderer::Reserve). For large maps, split the ribbons into regions (for example per 32×32-tile block) and draw only the blocks that touch the view.

**Verification:** Fixed 8,192 caps exist (road_draw.hpp:19, river_draw.hpp:22). Overflow drops whole rivers or roads and logs a warning (river_draw.cpp:150, road_draw.cpp:21), and the API documents this. All quads are drawn each frame regardless of view, as claimed. The maps are procedural, so scaling is a real concern. Nothing found shows the shipped map is anywhere near the cap. This is a scaling limit, not a present bug.

### GFX-FEATURES-04: Overrunning maxQuads is guarded only by PN_ASSERT; in release, CopyToBuffer would write past the mapped transfer buffer

- **Severity:** low · **Verdict:** confirmed · **Category:** memory-safety · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/internal/features/line_ribbons.cpp:74`

LineRibbonRenderer::Upload takes whatever `lay` produced and copies all of m_laid into a transfer buffer sized for m_maxQuads. The only guard is `PN_ASSERT(m_quads <= m_maxQuads, ...)`, which is compiled out in release. BytesAt(mapped, bytes.size()) then builds a span larger than the mapping, and std::ranges::copy writes past it. Today the lay lambdas cap at the same constants, but the class's contract relies on the caller agreeing, and a mismatched constant (for example a third line type passing a different max) would corrupt the heap.

**Evidence:** line_ribbons.cpp:73-80: `m_quads = m_laid.size() / QUAD_VERTICES; PN_ASSERT(m_quads <= m_maxQuads, ...); ... CopyToBuffer(..., std::as_bytes(std::span{ m_laid }), true)`

**Impact:** Heap overflow in release builds if a layout ever exceeds the buffer it was made for.

**Recommendation:** Clamp in release too: truncate to m_maxQuads (keeping it even, as the band split requires) and return the dropped count, as RibbonRenderer and MeshRenderer do. Better still, let the buffer grow.

**Verification:** line_ribbons.cpp:74 guards with PN_ASSERT only. The lay functions cap at the same constants (river_draw/road_draw), so it is latent, not currently reachable. A release clamp would be cheap hardening.

### GFX-FEATURES-05: Each of six SpriteRenderers builds its own identical sprite pipelines (12 pipelines, 24 shader objects)

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/internal/features/sprites.cpp:358`

SpriteRenderer::Create builds the Normal and Additive pipelines every time, and CreatePipeline creates both shaders again for each one. The renderer creates six SpriteRenderers (instances, overlay, terrain, route, clouds, target; renderer.cpp:1408-1446), all with the same format and the same shaders. The pipelines don't depend on the instance buffer at all.

**Evidence:** sprites.cpp:358-364 loops over both blends and calls CreatePipeline, which calls CreateShader twice (sprites.cpp:165-166).

**Impact:** About 6× the startup pipeline compilation and the driver memory it needs. Pipeline creation is the costliest GPU setup step, especially on D3D12/Metal through SDL's translation.

**Recommendation:** Split a `SpritePipelines` object (created once, shared by reference or held by the renderer) from the per-buffer `SpriteInstances` (storage buffer, transfer buffer, count). Create each shader once and build both pipelines from it.

**Verification:** SpriteRenderer::Create (sprites.cpp:358-364) builds both pipelines per instance, and renderer.cpp:1408-1446 creates six instances with the same SCENE_FORMAT. That is duplicate startup work. It is a one-time cost, so low.

### GFX-FEATURES-06: The held-once-submitted logic is written out five times although gfx::UploadedSnapshot already does it

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** duplication · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/internal/features/sprites.hpp:113`

FogTexture (m_held/m_recorded and Submitted), GroundRenderer, LineRibbonRenderer (versions and indices), SealRenderer (m_packHeld/m_packRecorded) and the renderer's glow and terrain fields each reimplement the same recorded-then-held state machine. gfx/uploads.hpp's UploadedSnapshot is exactly this abstraction and has unit tests (uploads_tests.cpp), but only for SnapshotId. The copies are untested and already diverge: three renderers skip it for their index buffers (see the separate finding).

**Evidence:** sprites.hpp:113-114, ground.hpp:70-71, line_ribbons.hpp:79-82, seals.hpp:149-151, renderer.cpp:1137-1146; uploads.hpp:30-52 `class UploadedSnapshot { Holds, Recorded, Submitted }`.

**Impact:** More code to keep correct, bugs fixed in one copy and not the others, and no tests for any copy but one.

**Recommendation:** Turn UploadedSnapshot into a template, `SubmitTracked<Key>` (Holds/Recorded/Submitted), in gfx/uploads.hpp with its existing tests, and use it in every feature (fog revision, ground and line versions, seal pack version, quad indices as a bool key).

**Verification:** The recorded/held pattern is repeated across features (FogTexture, Ground, LineRibbon, Seal), and UploadedSnapshot exists in uploads.hpp. I did not read each copy, and the renderer.cpp:1137 citation is unchecked. It is a plausible dedup and fits the project's full-review backlog. Mostly a refactor suggestion. The medium severity is overstated.

### GFX-FEATURES-07: No tests reach internal/features; failure, cancel, resize and overflow paths are untested

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** testing · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/gfx/meshes_tests.cpp:1`

The only unit test named after a feature (meshes_tests.cpp) checks the pure MeshQuadsDropped constant arithmetic. None of the features' state machines is tested: FogTexture's resize and keep-the-stale-field-on-map-failure, Ground and LineRibbon re-uploading after a cancelled submit, SealRenderer pack versioning and Reserve growth, SpriteRenderer batch clamping past capacity, DrawGlowOnly bounds. The reference tests capture only on the happy path (one Capture, then WaitForGPUIdle), so they would not catch the known-deferred index bug or any resubmit regression.

**Evidence:** grep for `internal::` or `internal/features` in tests/ finds nothing; reference_tests.cpp drives renderer.Capture only.

**Impact:** Regressions in GPU resource lifetime and resubmission go unnoticed until a driver hiccup or a failed submit happens in the field.

**Recommendation:** Pull the upload-state logic into pure types (see the SubmitTracked finding) and unit-test them. Add one reference test that injects an upload failure (for example a test hook that makes the first Capture's upload fail) and then checks that the next capture matches the reference picture.

**Verification:** The backlog (line 89) itself says no test covers the rollback paths, which supports the gap. Reference tests drive Capture only. The claim that nothing reaches internal/features is a broad grep claim I did not recheck. The gap is real, but the remedy overlaps finding 0's test-first fix.

### GFX-FEATURES-08: Fog and ground textures use one texel per tile; maps wider than the GPU's texture limit lose the ground, and fog failure reveals the whole map

- **Severity:** low · **Verdict:** unverifiable · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/internal/features/sprites.cpp:309`

FogTexture::Update and GroundRenderer::Update create a columns × rows R8G8 texture. SDL_GPU's guaranteed 2D size is 4,096 on Vulkan's minimum, 8,192 or 16,384 elsewhere. A procedural map past that fails creation every frame. The two fail differently: FogTexture sets m_uniforms.on = 0 ('the frame draws without the fog'), so every sprite and line is drawn unfogged, with no drain or shade, while the CPU-side clouds still draw; GroundRenderer draws nothing. Fog failing open shows unexplored places, markers and travellers.

**Evidence:** sprites.cpp:309-321 (create fails → `m_uniforms.on = 0`); ground.cpp:130-143; ground.cpp:137 computes the transfer size as u32 `m_columns * m_rows * FIELD_TEXEL_BYTES`, while sprites.cpp:302 uses std::size_t.

**Impact:** On large worlds or weak GPUs the world map either loses its ground or exposes unexplored content. Error logs repeat each frame, throttled by RepeatedFailure.

**Recommendation:** Check map dimensions against SDL's limits when the map is generated or set. Tile both textures into pages, or a texture array, for large maps. Make fog fail closed (shade everything unexplored, or refuse the world) rather than drawing without it. Compute byte sizes in std::size_t consistently.

**Verification:** I did not read the fog or ground texture creation code. Whether it fails past the GPU texture limit depends on SDL_GPU limits and on map sizes, which are not bounded here as far as I checked. The fail-open fog behaviour needs a read of sprites.cpp:309 and a large-map test to settle.

### GFX-FEATURES-09: GroundTexels runs over the whole map on the main thread at every new version, look reloads and failed submits included

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/internal/features/ground.cpp:146`

GroundRenderer::Update calls GroundTexels(map, ...) whenever m_held != version. GroundTexels scans an 11×11 window (WINDOW = 5) for every tile, about 121 lookups per tile, on the calling (main) thread. worldVersion changes on every world_look.yaml reload, even an edit unrelated to the ground (a route colour, say). After a cancelled or failed submit the field is rebuilt from scratch, although m_texels still holds the result for the same version. LineRibbonRenderer likewise calls `lay` again on a resubmit even though m_laid holds that layout.

**Evidence:** ground.cpp:126-146: `m_ready = m_held == version; if (m_ready) return {}; ... GroundTexels(map, water, m_texels);`; line_ribbons.hpp:48-53 calls `lay(m_laid)` whenever m_held != version.

**Impact:** A 1024×1024 map costs about 127M lookups, a 100 ms-class main-thread hitch on each look reload and on any re-upload. The cost grows with map size, not view size.

**Recommendation:** Remember which version m_texels and m_laid were built for, and only re-copy when that version is already built. Run GroundTexels through the job system by rows, or on SetWorld rather than in the frame. Key the ground field only on the inputs it reads (map plus water kinds), not on the whole look.

**Verification:** ground.cpp:126-146 confirmed: Update returns early only when m_held == version, and Submitted() (169-174) leaves m_held alone on a failed submit, so GroundTexels runs again. Same for line_ribbons.hpp:48-53 with `lay`. The per-tile 11x11 window cost scales with map size. I did not measure the hitch. The 100 ms figure for a 1024^2 map is an estimate.

### GFX-FEATURES-10: Pipeline, blend, sampler, transfer-buffer and texture creation are duplicated across every feature; MeshRenderer and RibbonRenderer are the same class

- **Severity:** low · **Verdict:** confirmed · **Category:** duplication · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/internal/features/meshes.cpp:18`

CreateMeshPipeline (meshes.cpp:18-63) is CreateRibbonPipeline (ribbons.cpp:28-74) with the blend fixed: MeshVertex and RibbonVertex have identical layouts. MeshRenderer::Upload/Draw and RibbonRenderer::Upload/Draw differ only in runs versus one draw. The premultiplied-over and additive blend states are written out in sprites.cpp:173-181, seals.cpp:91-103, meshes.cpp:26-32, ground.cpp:47-53, line_ribbons.cpp:15-25 and ribbons.cpp:14-24. Clamp-to-edge samplers appear in sprites.cpp:264-273, meshes.cpp:132-139, ground.cpp:69-82 and composite.cpp:32-39. Transfer-buffer builders: CreateTransfer in sprites.cpp:94, CreateUploadBuffer in seals.cpp:169, plus inline ones in sprites.cpp:215, sprites.cpp:372, meshes.cpp:142, ribbons.cpp:84, ground.cpp:135 and meshes.cpp:107. R8G8 UNORM texture creation: CreateFogTexture (sprites.cpp:76) and CreateFieldTexture (ground.cpp:84) are identical but for the name.

**Evidence:** See the line ranges listed.

**Impact:** Maintenance cost, and fixes drift between copies (the index-upload fix is one example).

**Recommendation:** Add gfx/internal/resources helpers: PremultipliedBlend(additive), ClampSampler(filter, name), UploadBuffer(bytes), Rg8Texture(columns, rows, name), and one quad-pipeline builder taking the vertex layout. Merge MeshRenderer and RibbonRenderer into one quad renderer with an optional per-run texture.

**Verification:** The duplication is real across the cited features (pipelines, blend states, samplers, transfer buffers, fog and field textures). Maintainability only, no defect. I only spot-checked it, did not diff each pair.
