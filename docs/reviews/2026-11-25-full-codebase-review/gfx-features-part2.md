# Review: gfx-features (part 2)

### GFX-FEATURES-11: Seal light is drawn or dropped inconsistently with the quads; the quad-overflow path is unreachable

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/internal/features/seals.cpp:316`

Upload clears both planes' light when the frame has no quads at all (seals.cpp:316-319). DrawRuns returns before drawing a plane's light when that plane has no wash or line quads (seals.cpp:373-375). When quads are dropped past MAX_SEAL_QUADS (later lists first), the light records of the seals whose quads were cut still draw in full, so a seal can show particles without its rings. MAX_SEAL_QUADS is sized for the worst case (MAX_SEALS_DRAWN × (layers × max(segments, sides) + emblem + 2)), so in practice the dropped path and its sealQuadOverflows report never fire.

**Evidence:** seals.cpp:285-298 (the add lambda that truncates), 301-310 (light records always kept), 373 `if (wash.quads == 0 && lines.quads == 0) return;`

**Impact:** Today, unreachable code and a misleading report path. If the bound is ever loosened (for example more frame sides or satellites), seals would draw partly, light without rings.

**Recommendation:** Either static_assert that DrawSeals can never exceed MAX_SEAL_QUADS and drop the runtime truncation, or drop a seal's light with its quads (truncate at whole-seal boundaries). Draw the light independently of whether the plane has band quads.

**Verification:** seals.cpp:285-298 truncates quads but the light records are kept whole (301-310, with a comment saying they always fit), so the inconsistency exists in principle. It is unreachable if MAX_SEAL_QUADS is the worst-case bound, as claimed. The claim that light is dropped when a plane has no quads holds (373-375 returns early). That only matters if a seal has light but no band or wash quads, which seems unlikely. Hypothetical, so low.

### GFX-FEATURES-12: After a failed SealRenderer::Reserve, the pack buffers can be left null yet counted as fitting an empty pack

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/internal/features/seals.cpp:238`

Reserve replaces `buffer` and `transfer` before knowing whether creation succeeds, and on failure sets capacity = 0 with the buffer possibly null. A later pack with no parts or sprites (bytes 0) gets SealBufferRoom(0, 0) == 0 == capacity, so Reserve returns true without making a buffer. DrawRuns would then bind a null storage buffer (seals.cpp:379, 392), which SDL backends dereference.

**Evidence:** seals.cpp:238-245: `if (*room == capacity) return true; buffer = CreateMeshBuffer(...); transfer = CreateUploadBuffer(...); capacity = buffer && transfer ? *room : 0;`

**Impact:** An unlikely chain (a GPU allocation failure, then a reload to a table with no parts) leads to a crash in the backend.

**Recommendation:** Treat capacity 0 or a null buffer as needing room: never return true without non-null buffers, and keep the minimum SEAL_SMALLEST_PACK room. Build the new buffers into locals and swap them in only on success, so the old pack stays usable.

**Verification:** seals.cpp:238-245 and SealBufferRoom (seal_draw.cpp:848-857) confirmed. With capacity 0 after a failure, bytes 0 gives room 0, equal to capacity, so Reserve returns true with null buffers. Impact is smaller than stated: an empty pack has no seals, so m_quads is empty and DrawRuns returns early at line 373 before binding the buffers. Buffers are also re-reserved once bytes > 0. Still a latent robustness gap.

### GFX-FEATURES-13: Seals ignore the fog; upright and overhead seals are drawn after the clouds

- **Severity:** low · **Verdict:** unverifiable · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/internal/features/seals.cpp:364`

The seal and seal-light shaders bind no fog texture or uniforms (seal.frag.hlsl binds field, layers, parts, sprites and Look only), and DrawAbove runs after the cloud-shade pass and the clouds (renderer.cpp:1247-1258). On the world map an upright or overhead seal over unexplored land therefore shows through the clouds. Every other world-map element (sprites, ground, rivers, roads) is fogged or hidden under the cloud shade.

**Evidence:** shaders register list: seal.frag.hlsl has no fog.hlsli include; renderer.cpp:1258 `seals.DrawAbove(...)` after `clouds.Draw`.

**Impact:** Today only the portal seal shows on the world map, near the start city. Once NPCs or sects cast seals on the map (planned genre content), their seals will reveal unexplored activity.

**Recommendation:** Give seal records a fog flag (as instances have INSTANCE_FOGGED_LAND / SIGHT_FADE), sample fog.hlsli in the seal shaders, or cull seals whose centre is unexplored when laying them out.

**Verification:** I did not verify the shader bindings or the draw order in renderer.cpp:1247-1258. Whether fogged seals are wanted is a design choice. Current impact is only the portal seal, as the finding itself says. It looks plausible but speculative about future content.

### GFX-FEATURES-14: Some uniform structs have no size static_assert, unlike the others

- **Severity:** low · **Verdict:** confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/internal/features/ground.cpp:33`

DrawUniforms, SealUniforms, SealLightUniforms and SealLookUniforms are static_asserted to the shader's size. GroundPlace (ground.cpp:33-36), BandUniforms (line_ribbons.cpp:33-36) and TextStyle (meshes.hpp:25-37, pushed raw through SDL_PushGPUFragmentUniformData) are not.

**Evidence:** ground.cpp:33 `struct GroundPlace { std::array<f32,4> worldToClip{}; std::array<f32,4> quad{}; };` with no static_assert; the same for BandUniforms and TextStyle.

**Impact:** An added member would change what the shader reads without any check.

**Recommendation:** Add `static_assert(sizeof(X) == N, ...)` for each, ideally against generated reflection sizes (see the shader-mirroring finding).

**Verification:** The static_assert grep over features/*.cpp shows size asserts for DrawUniforms (sprites.cpp:29), SealUniforms, SealLightUniforms and SealLookUniforms (seals.cpp), and none for GroundPlace or BandUniforms. I did not check TextStyle in meshes.hpp. A missing guard rather than a bug.

### GFX-FEATURES-15: A cancelled cycled copy can leave the fog texture bound to undefined contents while it is counted as held

- **Severity:** info (reviewer said low) · **Verdict:** unverifiable · **Category:** correctness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/gfx/internal/features/sprites.cpp:325`

Each upload cycles the texture or buffer (cycle = true) when recorded. If that command buffer is cancelled (capture path), the resource's current backing never receives the data, but m_held still names the older revision. When the next frame's CopyFog fails to map, FogTexture keeps the fog on because m_held is set (sprites.cpp:328-330) and binds m_field, whose current backing is the cycled, never-written one.

**Evidence:** sprites.cpp:325-331 (on map failure, keep the fog on if m_held); CopyFog uploads with cycle true (sprites.cpp:112, 127); renderer.cpp:1704-1706 cancels on capture upload failure.

**Impact:** Edge case: one frame of garbage fog after a cancelled capture followed by a map failure.

**Recommendation:** When a recorded cycled copy is reported unsubmitted, treat the resource as undefined (reset m_held) rather than still holding the previous version. Or avoid cycling on the capture path.

**Verification:** Whether SDL's cycle=true on a cancelled command buffer leaves the new backing undefined depends on SDL GPU backend semantics. The sprites.cpp:325-331 logic does keep the fog on when m_held is set. Needs a backend test or the SDL source to decide. It is an edge case needing a cancelled capture and then a map failure.

### GFX-FEATURES-16: FogTexture lives in sprites.hpp; ground and line ribbons include the whole sprite renderer header for it

- **Severity:** info · **Verdict:** confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/internal/features/line_ribbons.hpp:14`

FogTexture is shared by every land shader (sprites, ground, rivers and roads), yet it is declared in features/sprites.hpp alongside GlowTableBuffer and SpriteRenderer. ground.hpp:15 and line_ribbons.hpp:14 include sprites.hpp only for it, which pulls in core/hit.hpp, core/glow.hpp, gfx/glow_table.hpp and sprite_array.hpp.

**Evidence:** line_ribbons.hpp:14 `#include "gfx/internal/features/sprites.hpp"`; ground.hpp:15 likewise.

**Impact:** Wider rebuilds and a misleading place for a shared resource.

**Recommendation:** Move FogTexture into features/fog.hpp (and GlowTableBuffer into its own header). Update src/AGENTS.md's Rendering line, which names `gfx/internal/features/sprites.hpp` for FogTexture, in the same commit.

**Verification:** The finding is an include-hygiene point. I did not re-open the headers to check which includes are pulled in. The finding's own evidence (line_ribbons.hpp:14 includes sprites.hpp for FogTexture) is consistent with what I saw of the header. Cosmetic.

### GFX-FEATURES-17: Misleading names and comments in the feature layer

- **Severity:** info · **Verdict:** confirmed · **Category:** documentation · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/internal/features/sprites.hpp:140`

(1) sprites.hpp:140 says 'one without draws as before, reading no field': 'as before' refers to the code's history, not its contract. (2) shaders/seal_light.vert.hlsl:3 names 'LightUniforms in src/gfx/internal/features/seals.cpp', but the struct is SealLightUniforms. (3) MeshRenderer is text-only (TextStyle, R8 glyph pages, outline and shadow), so 'mesh' oversells it. (4) RibbonRenderer reuses MESH_VERT, whose cbuffer member is pixelsToClip, but passes it a worldToClip (ribbons.cpp:143). (5) seals.cpp:282 '// The lists end to end, each as much of it as still fits.' and meshes.cpp:26 '// premultiplied over' restate what the code does, against src/AGENTS.md's comment rule.

**Evidence:** See the lines cited.

**Impact:** Readers and reviewers are misled. These are small.

**Recommendation:** Reword (1) as a contract. Fix the struct name in the shader comment. Consider renaming MeshRenderer to TextRenderer and MESH_VERT's member to `toClip`. Drop or reword the 'what' comments.

**Verification:** Checked each cite. sprites.hpp:140 has 'one without draws as before'. seal_light.vert.hlsl:3 says LightUniforms while seals.cpp:69 names SealLightUniforms. mesh.vert.hlsl:12 names the member pixelsToClip, and ribbons.cpp:143 pushes worldToClip into it, so the name is misleading, though the layout matches. seals.cpp:282 and meshes.cpp:26 are 'what' comments. The mesh-vs-text naming is opinion. All are minor cosmetics.

### GFX-FEATURES-18: Single-brace std::array initialisations, against the MSVC C5246 gotcha

- **Severity:** info · **Verdict:** confirmed · **Category:** style · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/gfx/internal/features/meshes.cpp:191`

src/AGENTS.md's Gotchas say std::array needs double braces for MSVC C5246. meshes.cpp:191 and ribbons.cpp:143 (`const std::array<f32, 4> uniforms{ a, b, c, d }`) and sprites.cpp:133 (`.worldToClip = { ... }`) use single braces, while line_ribbons.cpp:117 and ground.cpp:190 use double. Presumably the windows-msvc builds pass today (the warning may not apply to these contexts), but the code is inconsistent with the documented rule.

**Evidence:** meshes.cpp:191, ribbons.cpp:143, sprites.cpp:133 compared with line_ribbons.cpp:117.

**Impact:** Inconsistency, and a possible MSVC warning-as-error if flags change.

**Recommendation:** Use double braces consistently, or narrow the gotcha's wording to the contexts where MSVC actually warns.

**Verification:** meshes.cpp:191, ribbons.cpp:143 and sprites.cpp:133 (a std::array<f32,4> member of DrawUniforms, line 16) use single braces. src/AGENTS.md:41 says std::array needs double braces, including for members and arguments. The code is inconsistent with that rule. It presumably passes MSVC today, because float scalars into an array with an explicit type may not trigger C5246 in practice, but I did not run MSVC, so that is unverified. Low impact.

### GFX-FEATURES-19: SpriteRenderer::Draw rebinds the pipeline and re-pushes 80 bytes of uniforms for every batch

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/internal/features/sprites.cpp:426`

Each batch calls SDL_BindGPUGraphicsPipeline, even when the pipeline is unchanged from the previous batch, and pushes the whole DrawUniforms block, though only firstInstance changes. The Composite's Draw also uses a bare literal 3 for its fullscreen triangle (composite.cpp:50), where other features name their vertex counts.

**Evidence:** sprites.cpp:426-436.

**Impact:** Small with today's batch counts. It would grow if batches multiply (per-page batching of large crowds).

**Recommendation:** Skip the rebind when the pipeline is unchanged. Consider passing firstInstance through a small second uniform slot, or SDL's first_instance once it reaches SV_InstanceID. Name the composite's vertex count.

**Verification:** sprites.cpp:426-436 rebinds the pipeline every batch and pushes the full DrawUniforms struct (firstInstance changes each time). The comment at 425 shows the rebind is a known choice. The comment at 434 explains why firstInstance goes through the uniform: SDL warns that first_instance does not reach SV_InstanceID. Skipping the rebind when the pipeline is unchanged would be a valid micro-optimisation. composite.cpp:50 does use a bare 3. The cost is negligible and no measurement shows it matters.
