# Review: shaders

**Scope:** shaders/*.hlsl and *.hlsli, shaders/AGENTS.md, src/gfx/generated/shaders.hpp (only for consistency with the HLSL and with the C++ that binds uniforms/vertex layouts in src/gfx/internal/features/), and the shader-compilation part of tools/project_n.py. Check struct layouts vs C++, colour space handling, precision, branch/perf, unused code.

**Findings:** 16 standing (0 critical, 0 high, 0 medium, 9 low, 7 info), 0 raised but refuted on verification.

**Parts:** shaders.md, shaders-part2.md

## Assessment

## Shaders: assessment

**Overall health: good.** The HLSL is small (about 2,000 lines), carefully written and heavily commented. The CPU/GPU contracts are deliberate. I checked every uniform and storage struct against its C++ twin field by field: `Fog`/`FogUniforms` 64 B, `Ground`/`GroundUniforms` 384 B, `GroundPlace` 32, sprite `Draw`/`DrawUniforms` 80, `Instance` 56, `GlowRecord` 96, `TextStyle` 32, `RiverBand` 16, seal `Draw` 32, `LightDraw` 48, `Look` 16, `SealLayer` 88, `SealPart` 64, `SealSprite` 32, `SealParticles` 160, and the `MeshVertex`/`RibbonVertex`/`SealVertex` attribute layouts. They all match under DXC's SPIR-V layout (relaxed std140 / std430) and DXIL's layout. Resource counts in `shaders.hpp` match what each feature binds and pushes. The enum and constant mirrors (`GROUND_MODE_*`, `DRIVE_*`, `CURVE_*`, `SEAL_*`, `INSTANCE_*`, `FIELD_REACH_TEXELS`, `REDUCED_LIGHT_OPACITY`) are guarded by `static_assert`s on the C++ side. The GPU copies of CPU maths match the CPU versions line for line: `CurveAt`/`CurveMean`/`GlowStrength`, `SealParticleAt`/`SealHash`/`SealStaggered`, `UnexploredShade` vs `ExploredFade`, sight fade, and the B-spline `ShoreAt`. Colour handling is right throughout: the atlas is `UNORM_SRGB` and premultiplied in linear space, the scene target is linear fp16, and one composite encodes sRGB with the exact piecewise curve. Derivative discipline is good: `fwidth`/`ddx` are taken before any discard, branches depend only on flat per-primitive inputs, and the seals use `SampleGrad`. The compile step in `project_n.py` checks SDL_GPU's register layout from SPIR-V reflection, keeps Linux and Windows on the same tool versions, and hashes sources and outputs so lint catches stale binaries.

**Weaknesses and risks (ordered by importance):**
1. **Only SPIR-V is verified.** Layout and binding checks reflect the SPIR-V alone, and the picture tests run only on lavapipe. DXIL takes its bindings from separately written `register()` annotations that nothing checks against the `[[vk::binding]]` values. Neither DXIL nor MSL is ever drawn in CI, so a divergence would first appear on a player's Windows or macOS machine.
2. **HLSL struct layouts are checked only against comments.** The C++ asserts its own sizes, not the HLSL's. Struct sizes and offsets from SPIR-V reflection could be emitted into `shaders.hpp` and checked by `static_assert`.
3. **Changing the compile flags doesn't mark binaries stale.** The manifest hashes sources, outputs and tool pins, but not the command lines in `project_n.py` (profile, target env, MSL version, binding maps).
4. **Wasted GPU work on the world map.** The painted ground recomputes about 11 octaves of value noise for every pixel every frame, even with the camera still. Two more full-view passes follow: a veil with 4 more octaves, and a cloud shade that blends zeros wherever land is explored or the view has no fog.
5. **Scaling.** Absolute f32 world coordinates go into a `world*scale+offset` clip transform, and the fog and ground fields are one texel per tile. Both have ceilings on very large procedural maps: sub-pixel jitter, and textures past GPU size limits.
6. **Smaller items.** The glow age loses sub-tick precision earlier than its comment says. The text ink's alpha is ignored. A few comments are stale or describe what the code does (against the AGENTS.md rule). Some helpers are duplicated (PCG hash, PI/TAU, smoothstep variants). The composite clips additive light per channel, with no tone curve or dither.

No crash, undefined behaviour or simulation-determinism issue was found in scope (shaders don't touch the sim). Divisions are guarded, and the data loaders validate the values the shaders divide by (pulse period, band, edge, step and count).

**Files read in full:** shaders/AGENTS.md, shaders/colour.hlsli, shaders/composite.hlsli, shaders/composite.vert.hlsl, shaders/composite.frag.hlsl, shaders/field.hlsli, shaders/fog.hlsli, shaders/ground.hlsli, shaders/ground.vert.hlsl, shaders/ground.frag.hlsl, shaders/mesh.hlsli, shaders/mesh.vert.hlsl, shaders/mesh.frag.hlsl, shaders/ribbon.frag.hlsl, shaders/river.vert.hlsl, shaders/river.frag.hlsl, shaders/sprite.hlsli, shaders/sprite.vert.hlsl, shaders/sprite.frag.hlsl, shaders/seal.hlsli, shaders/seal.vert.hlsl, shaders/seal.frag.hlsl, shaders/seal_light.vert.hlsl, shaders/seal_light.frag.hlsl, src/gfx/generated/shaders.hpp, src/gfx/internal/features/composite.cpp, src/gfx/internal/features/ground.hpp, src/gfx/internal/features/ground.cpp, src/gfx/internal/features/sprites.hpp, src/gfx/internal/features/sprites.cpp, src/gfx/internal/features/meshes.hpp, src/gfx/internal/features/meshes.cpp, src/gfx/internal/features/ribbons.cpp, src/gfx/internal/features/line_ribbons.cpp, src/gfx/internal/features/seals.cpp, src/gfx/internal/resources/shaders.cpp, src/gfx/ground.hpp, src/gfx/fog.hpp, src/gfx/fog.cpp, src/gfx/glow_table.hpp, src/gfx/mesh_vertex.hpp, src/gfx/ribbon.hpp, tools/project_n.py (lines 1594-1745, shader compilation)

## Findings

### SHADERS-01: Shader layout/binding verification covers SPIR-V only; DXIL registers and the MSL/DXIL render paths are never checked or exercised

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** testing/tooling · **Status:** new · **Reviewer confidence:** high
- **Where:** `tools/project_n.py:1733`

cmd_shaders reflects only the SPIR-V output (`spirv-cross <base>.spv --reflect`) and shader_resources() checks that layout. DXIL is compiled separately (`dxc ... -Fo base.dxil`). Its bindings come from the HLSL `register(tN/sN/bN, spaceM)` annotations, which are written by hand next to the `[[vk::binding(N, M)]]` attributes, and nothing compares the two. Example: fog.hlsli builds the register from the FOG_TEXTURE_SLOT macro and the vk::binding from the same macro, but a future texture could easily get `[[vk::binding(1,2)]] ... register(t2, space2)`. The reference picture tests run only on Linux/lavapipe (SPIR-V), and CI has no GPU-backed Windows or macOS draw test, so DXIL and MSL are only compiled, never drawn.

**Evidence:** project_n.py:1730-1738: SPIR-V validated (`spirv-val`) and reflected; DXIL compiled with `-Wno-ignored-attributes` and never reflected. tests/reference is Linux-only (AGENTS.md: 'reference ... on software Vulkan (Linux)').

**Impact:** A register/binding mismatch, or a backend-specific difference (cbuffer packing, NaN or clamp semantics, derivative behaviour), would draw correctly on Vulkan and wrongly or not at all on D3D12 or Metal, discovered only by players.

**Recommendation:** Either drop the redundant `[[vk::binding]]` attributes and let DXC derive SPIR-V bindings from `register()` (t/s of the same N already map to one binding, which combinedImageSampler needs), so there is one source of truth; or reflect the DXIL (`dxc -dumpbin`, or the DXIL reflection API) and assert it matches the SPIR-V. Add a D3D12 WARP smoke draw (one reference scene) to the Windows CI job.

**Verification:** Checked cmd_shaders: only the SPIR-V is validated and reflected, and the DXIL is compiled with -Wno-ignored-attributes and never inspected. The register() and vk::binding annotations are written by hand, and a stray mismatch would not be caught. Mitigation: nearly all files use the same N for t/s/b and the fixed spaces; fog.hlsli derives both from FOG_TEXTURE_SLOT. The claim that no draw test runs on D3D12 or Metal is true but reflects the platform and CI limits. The recommendation to drop vk::binding is questionable, because DXC needs explicit vk::binding to merge a texture and sampler into one combinedImageSampler. Impact is real but speculative, so low.

### SHADERS-02: HLSL struct layouts are tied to C++ only by comments; no generated or reflected size/offset check

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** architecture/robustness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/generated/shaders.hpp:12`

Every C++ uniform or storage struct asserts its own sizeof/offsetof (e.g. GroundUniforms, DrawUniforms, SealGpuLayer), but the HLSL side (`Ground`, `Draw`, `SealLayer`, `GlowRecord`, ...) is matched only by comments such as '/// gfx::SealGpuLayer (src/gfx/seal_draw.hpp), 88 bytes'. If someone adds or reorders a field in the HLSL only, or a float3 lands across a 16-byte row, the C++ static_asserts still pass. The generated header already carries per-stage resource counts, so the tool already reflects each stage.

**Evidence:** shaders.hpp Stage has only name/entryPoint/binaries and four counts; spirv-cross --reflect also reports each block's member offsets and sizes, which are unused.

**Impact:** A silent layout mismatch corrupts uniforms (wrong colours, NaN positions). It is caught only if a reference scene happens to exercise the field, and only on Vulkan.

**Recommendation:** Have `shaders` emit, per uniform block and structured-buffer element, constexpr sizes and member offsets (e.g. `inline constexpr std::size_t SPRITE_VERT_DRAW_BYTES = 80;`, `..._DRAW_TEXELUV_AT = 64`). The C++ features then `static_assert(sizeof(DrawUniforms) == shaders::SPRITE_VERT_DRAW_BYTES)` against the generated values instead of hand-copied numbers.

**Verification:** Checked the HLSL structs and C++ headers: the HLSL layouts are matched to C++ by comments and by each side's own sizeof/offsetof asserts. The generated header carries only resource counts, no sizes or offsets. A one-sided HLSL edit would pass the static_asserts. Reference pictures cover some fields. This is a legitimate robustness gap, not a defect today.

### SHADERS-03: Shader manifest ignores the compile flags, so changing DXC/SPIRV-Cross options leaves stale binaries that lint accepts

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** tooling · **Status:** new · **Reviewer confidence:** high
- **Where:** `tools/project_n.py:1654`

shader_hashes() covers the tool pins and the bytes of every source and output, but not the command lines in SHADER_STAGES (profiles vs_6_0/ps_6_0), `-fspv-target-env=vulkan1.0`, MSL_VERSION, `--msl-decoration-binding` or the msl_bindings() mapping. Editing any of these without running `./dev shaders` leaves binaries built with the old flags, and stale_shaders() still reports them fresh.

**Evidence:** `pins = ...shader_tool_pins()...; files = sorted(p for p in [*(ROOT / 'shaders').glob('*.hlsl*'), *(ROOT / SHADER_BIN).glob('*'), ROOT / SHADER_HEADER] ...)`. No flag text is included.

**Impact:** A flag change, such as raising the shader model or changing Metal binding numbering, can merge with old binaries. The mismatch shows up only at runtime on the affected backend.

**Recommendation:** Add one manifest line holding a hash of the compile recipe: SHADER_STAGES, MSL_VERSION, the DXC/spirv-cross argument templates, and the source of msl_bindings/shader_resources. Changing the recipe then marks binaries stale, as a tool bump does.

**Verification:** shader_hashes() hashes the tool pins, the sources, the outputs and the header, and nothing else. SHADER_STAGES (profiles), -fspv-target-env, MSL_VERSION, --msl-decoration-binding and msl_bindings() are not hashed. A flag edit therefore leaves a manifest that still matches. In practice project_n.py is edited in the same change, and lint/check on a flag change would not notice. The gap is real but narrow.

### SHADERS-04: World-map ground recomputes heavy procedural noise per pixel every frame, plus two more full-view passes that are often no-ops

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `shaders/ground.frag.hlsl:242`

Painted() evaluates for every pixel of the view on every frame: 2×3 warp octaves, 4 blotch octaves and 2 grain octaves (11 value-noise octaves, 44 lattice hashes), a 4-tap bicubic shore filter and a 4-tap kind fetch. renderer.cpp then draws the same quad twice more. VEIL mode (renderer.cpp:1221) runs 4 more octaves over the whole view and blends, even where its alpha is 0. CLOUD_SHADE (renderer.cpp:1248) runs over the whole view and blends `cover = 0` wherever land is explored or `fog.on == 0`. The painted result depends only on world position, zoom and the look, none of which change while the camera is still.

**Evidence:** renderer.cpp:1207 ground (GROUND_MODE_GROUND), :1221 veil, :1248 cloud shade; ground.frag:242-246 `const float cover = fog.on == 0 ? 0.0 : ...; return float4(fog.shade.rgb * cover, cover);`.

**Impact:** Three full-screen fragment passes, one of them heavy, at uncapped frame rates (165 Hz monitors), on the GPU budget the world view shares with up to 100k instances. This conflicts with the project's 'avoid wasted work' rule and will weigh on low-end and integrated GPUs.

**Recommendation:** Skip the cloud-shade draw when `fog.on == 0`, and restrict it to the bounding box of unexplored tiles. Cull the veil where its noise can't pass the threshold, or fold it into the ground pass. Cache the painted ground (and veil) into an offscreen target re-rendered only when the camera moves past a margin, the zoom changes or the look changes. At least measure the ground pass with `measure` and record the number.

**Verification:** Read ground.frag lines 236-250 and renderer.cpp 636-644, 1221 and 1248. The cloud-shade mode does a cheap fog lookup and returns cover 0 when fog.on==0, but the full-quad draw is still issued. Veil and ground are separate full-quad passes with per-pixel noise. The 'per frame with nothing changing' point is accurate. The backlog says GPU cost is unmeasured and Tracy GPU zones are to be added before judging, so there is no measured number and the severity is speculative. The skip-draw-when-fog-off suggestion is cheap and valid. Whether the veil or cloud draw is gated by the CPU before line 1221/1248 I did not fully read.

### SHADERS-05: Glow age loses its sub-tick fraction from 2^23 ticks (~39 h), earlier than the comment's 'three days'

- **Severity:** low · **Verdict:** confirmed · **Category:** precision/docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `shaders/sprite.vert.hlsl:179`

`const float age = float(int(draw.glowTick - instance.glowStart)) + draw.glowFraction;`. The comment on GlowStrength says f32 holds whole ticks exactly for 2^24 ticks. True for the whole part, but adding the fraction (0..1) rounds it to 0.5-tick steps from 2^22 ticks (~19 h at 60 tps) and drops it entirely from 2^23 (~39 h), so pulses step at tick rate rather than render rate. Past 2^24 the phase itself quantises to 2+ ticks. The CPU twin uses f64. Today only demo glows set startTick, but any persistent object whose pulse counts from its creation would reach these ages in a long ToI-style save, since ticks persist in saves.

**Evidence:** sprite.vert:116-119 comment; :179 computation; core/glow.cpp GlowStrength uses f64 ageTicks.

**Impact:** Glows on long-lived objects stutter on high-refresh displays after many hours of game time, and the comment overstates the safe range.

**Recommendation:** Wrap the age by the drive's period on the CPU side, or pass the pulse phase origin modulo the period (as the seal light already wraps ages by their period). At minimum, correct the comment to say the fraction is lost from 2^23 ticks.

**Verification:** Line 179 is float(int(glowTick-glowStart)) + glowFraction, and the comment at lines 116-119 says whole ticks are exact to 2^24 ticks (three days). The comment is about whole ticks only, but adding the fraction does lose it from about 2^23 (about 39 h at 60 tps). The finding's arithmetic holds. Whether any glow stays alive that long is unknown: the only startTick users I found are volley/seal, which are short-lived. Impact is minor, and a comment fix is cheap.

### SHADERS-06: Text ink alpha is uploaded but ignored; premultiplied ink rgb is treated as opaque

- **Severity:** low · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `shaders/mesh.frag.hlsl:61`

`const float4 ink = float4(style.ink.rgb, 1.0) * input.colour.a;` discards style.ink.a. TextStyle::ink comes from InkOf(packed), a premultiplied colour-table value (core::UiColour::TextInk). If that table entry ever has alpha < 1, its rgb is already multiplied by alpha, and forcing alpha to 1 draws a darker, fully opaque outline and shadow instead of a translucent one.

**Evidence:** meshes.hpp TextStyle::ink doc: 'linear, red, green, blue and alpha'; InkOf unpacks all four channels; mesh.frag uses only .rgb.

**Impact:** A palette or accessibility theme with translucent text ink renders wrongly; style.ink.a is dead uniform data.

**Recommendation:** Use `style.ink * input.colour.a`, which is premultiplied-correct and identical for opaque ink. Alternatively, document and enforce that TextInk is opaque (validate on palette load) and stop uploading the alpha.

**Verification:** mesh.frag line 61 is float4(style.ink.rgb,1.0)*input.colour.a, so ink alpha is ignored. meshes.hpp documents ink as 'red, green, blue and alpha', and text_place.cpp scales the TextInk by the opacity of the label colour 'as opaque as it was'. So today TextInk is assumed opaque and the ink alpha is dead data. Dormant defect that only appears with a translucent palette. Low.

### SHADERS-07: In-body comments that state what the next lines do, against shaders/AGENTS.md and the root comment rule

- **Severity:** low · **Verdict:** confirmed · **Category:** agents-rule · **Status:** new · **Reviewer confidence:** medium
- **Where:** `shaders/seal_light.frag.hlsl:26`

shaders/AGENTS.md says a comment inside a body 'stays only to give a why'; the root AGENTS.md says a plain comment 'never [says] what the next lines do'. Several in-body comments only describe what follows: seal_light.frag:26 ('A soft round spark, brightest at its middle, fading to nothing at its edge.'), :30-31 (the shard's diamond), :13 ('0 at its plane, 1 at its far end'), :14 and :17; seal.frag:150 ('Texels from the sprite's middle, right and down as its image runs.') and :222; sprite.vert:157 ('How far past the region the quad reaches, in UV: a glow's reach.') and :171 ('A progress with a result shows the result's colour.').

**Evidence:** Quoted lines above; lint's design_references check doesn't cover this rule.

**Impact:** Minor rule drift; these comments become stale when the code changes.

**Recommendation:** Rewrite each as a why (e.g. why the spark fades to nothing at its edge: no hard edge to crawl, which the file header already says) or move the contract into the helper's `///` doc; delete pure restatements.

**Verification:** Checked seal_light.frag.hlsl:13-31. The in-body comments ('A soft round spark...', 'A diamond sliver...', '0 at its plane...') mostly restate what the next line does. That breaks shaders/AGENTS.md's rule that an in-body comment gives only a why. Some of them carry a little rationale, so the drift is minor. I did not check the seal.frag and sprite.vert citations.

### SHADERS-08: Vertex input semantics are tied to SDL attribute locations only by convention

- **Severity:** low · **Verdict:** unverifiable · **Category:** tooling/robustness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `shaders/seal.vert.hlsl:4`

SDL_GPU maps attribute `location N` to SPIR-V location N (which DXC assigns in declaration order) and to the D3D12 semantic TEXCOORD N. The shaders keep TEXCOORDn equal to declaration order (mesh.vert, river.vert, seal.vert), but nothing checks it. A VertexIn with fields reordered, or a skipped TEXCOORD index, would work on one backend and break on the other. The pipelines' attribute tables (meshes.cpp:36, ribbons.cpp:47, seals.cpp:115) are also not checked against the shader's inputs.

**Evidence:** No `[[vk::location]]` attributes; the shader_resources() reflection ignores 'inputs'.

**Impact:** Latent cross-backend divergence on a future vertex layout change.

**Recommendation:** In shader_resources(), check that the reflected stage inputs' locations equal their TEXCOORD indices, or add `[[vk::location(n)]]` beside each semantic; optionally emit the input count into shaders.hpp and assert it against each pipeline's attribute count.

**Verification:** I did not read the vertex shaders, the pipeline attribute tables or shader_resources(). Whether anything checks the TEXCOORD index against the attribute location can only be decided by reading tools/project_n.py and the pipeline code. The risk is plausible but latent.

### SHADERS-09: CreateShader falls back to MSL without checking the device supports it

- **Severity:** low · **Verdict:** confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/internal/resources/shaders.cpp:10`

If the device reports neither SPIRV nor DXIL, MSL is chosen without testing SDL_GPU_SHADERFORMAT_MSL. On a backend that offers only DXBC or METALLIB, SDL_CreateGPUShader fails and the user sees a generic 'pipeline was rejected' error rather than 'no supported shader format'.

**Evidence:** `std::span<const u8> code = stage.msl; info.format = SDL_GPU_SHADERFORMAT_MSL; if (SPIRV) ... else if (DXIL) ...`

**Impact:** An unclear start-up failure on unusual drivers.

**Recommendation:** Test the MSL bit too, and return or log a specific error naming the formats the device offers.

**Verification:** shaders.cpp:10-20 defaults to MSL and never tests SDL_GPU_SHADERFORMAT_MSL. It picks SPIRV, then DXIL, and otherwise falls through to MSL with no check. The effect is only a worse error message on a backend offering neither SPIRV nor DXIL. SDL GPU backends that offer only DXBC are not realistic here.

### SHADERS-10: Absolute f32 world coordinates and one-texel-per-tile fields cap how large a procedural map can be drawn

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `shaders/sprite.vert.hlsl:198`

Every vertex stage computes `world * worldToClip.xy + worldToClip.zw` from absolute world positions: sprites (lerp of previous/current), the ground quad, rivers and roads, and seal vertices. The zw offset is about -camera×scale, so precision cancels far from the origin. With tile_size 256, a 10,000-tile map spans 2.56M units, where the f32 ulp is 0.25 units, i.e. 0.5 px at zoom 2: sprites and ribbons quantise and shimmer while panning. The fog and ground fields are 2D textures of columns×rows texels. Past 16384 tiles in either direction they exceed the D3D12/Vulkan minimum max-texture size and creation fails, and the ground and fog then silently stop drawing.

**Evidence:** sprite.vert:198-200, ground.vert:10, river.vert:18, seal.vert:41; ground.cpp CreateFieldTexture(columns, rows); sprites.cpp CreateFogTexture(columns, rows). world.yaml tile_size: 256.

**Impact:** This becomes a visible-quality or availability ceiling once maps are generated per seed at large sizes (memory: 'maps may be very large').

**Recommendation:** Upload positions relative to a camera-near origin, subtracting in f64 on the CPU (instances, ribbons, seal vertices), so shaders see small numbers. Document a maximum map size, or tile the fog and ground fields (texture array or virtual pages) and check against SDL's max texture dimension with a clear error.

**Verification:** The arithmetic is right: absolute f32 world coordinates lose precision at millions of units. The memory notes say maps are procedural and may be large, but there is no current map at that size (world.yaml is a test map). This is a forward-looking scaling concern, not a present bug. The texture-size limit is a plausible but unchecked claim.
