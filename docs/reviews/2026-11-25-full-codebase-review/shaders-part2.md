# Review: shaders (part 2)

### SHADERS-11: Composite clips HDR light per channel with no tone curve and no dither

- **Severity:** info (reviewer said low) · **Verdict:** deliberate · **Category:** rendering/colour · **Status:** new · **Reviewer confidence:** medium
- **Where:** `shaders/composite.frag.hlsl:14`

The scene target is R16G16B16A16_FLOAT ('room above 1.0 for stacked additive light', scene_target.hpp), but the composite does `saturate(...)` per channel before encoding. Stacked additive light (glows, seal light, sword ribbons) therefore hard-clips and shifts hue (an orange glow goes yellow, then white, as one channel saturates first). The 8-bit sRGB output is not dithered, so smooth low-contrast gradients (fog shade edges, washes, vignettes) can band.

**Evidence:** `return float4(EncodeSrgb(saturate(scene.Sample(sceneSampler, input.uv).rgb)), 1.0);`

**Impact:** Hue shifts and banding in the bullet-heavy combat look the genre leans on; the fp16 headroom is mostly unused.

**Recommendation:** If the clip is intended, say so in the comment. Otherwise apply a gentle shoulder (e.g. a luminance-preserving soft clip above about 0.8) before the encode, and add a ±0.5 LSB ordered or blue-noise dither after it. Both are cheap in this single fullscreen pass. Also consider `scene.Load` instead of a nearest sampler, since the target is always the pass size.

**Verification:** Composite is saturate then EncodeSrgb, as stated. docs/design/0002-engine-architecture.md line 390 records the linear colour space and the half-float scene target with a final composite that encodes sRGB. It mentions bloom as later. Clipping per channel and the lack of dither are not explicitly recorded as decisions, so this is a documented design with an unrecorded clipping choice. It is a style suggestion, and the headroom is partly intended for stacked light before the encode. Info.

### SHADERS-12: Stale or incomplete cross-references in comments

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/internal/features/sprites.cpp:34`

(1) sprites.cpp: `constexpr u32 SHADER_FIELD_REACH_TEXELS = 16; // sprite.hlsli's FIELD_REACH_TEXELS`, but the constant lives in shaders/field.hlsli, which sprite.hlsli only includes; seal.frag uses it too. (2) seal.hlsli:4 says 'Register spaces follow SDL_GPU's order: vertex uniforms in space1, fragment resources in space2', but seal.vert and seal_light.vert also use vertex storage in space0 and seal.frag uses fragment uniforms in space3. (3) ground.hlsli names RiverVertexOut 'The river's vertex stage's output' and river.frag says 'A river's band', yet line_ribbons.cpp uses the same pair for roads. (4) mesh.vert's `VertexIn.position // target pixels` and `Draw.pixelsToClip` are reused by the sword-ribbon pipeline in world units (ribbons.cpp:97).

**Evidence:** Quoted lines.

**Impact:** Misleading pointers for whoever next changes a shared constant or a register.

**Recommendation:** Point the sprites.cpp comment at field.hlsli (and mention seal.frag). List all four spaces in seal.hlsli. Rename to `LineVertexOut`/`line.frag` or mention roads. Rename `pixelsToClip` to a neutral `toClip` with 'target pixels or world units'.

**Verification:** Point (1) is confirmed. sprites.cpp:34 and :46 say 'sprite.hlsli's FIELD_REACH_TEXELS', but the constant is defined in field.hlsli:5. Point (2) is confirmed. seal.hlsli:4 lists only vertex uniforms in space1 and fragment resources in space2. I did not check whether the shaders use the other spaces. Points (3) and (4) I did not verify. The name-reuse complaints are cosmetic.

### SHADERS-13: Duplicated helpers and constants across shaders

- **Severity:** info · **Verdict:** unverifiable · **Category:** duplication · **Status:** new · **Reviewer confidence:** high
- **Where:** `shaders/sprite.frag.hlsl:16`

BlockHash (sprite.frag:16-25) is LatticeValue's PCG core (ground.frag:68-78) without the seed. PI is defined in both sprite.vert:57 and seal.frag:25. seal.frag:26 defines TAU = 6.28318531 beside seal.hlsli's SEAL_TAU of the same value. Smoothstep is hand-rolled in river.frag:24 and ground.frag Ramp alongside intrinsic smoothstep elsewhere. FOG_UNIFORM_SLOT = 0 is redefined in ground.cpp and line_ribbons.cpp and hard-coded as 0 in sprites.cpp Bind().

**Evidence:** Quoted definitions.

**Impact:** Changing a hash or constant in one place leaves the twin behind; minor.

**Recommendation:** Add a `hash.hlsli` (PCG2D with seed) and a `math.hlsli` (PI, TAU), and share one FOG_UNIFORM_SLOT constant in gfx/fog.hpp beside FogUniforms.

**Verification:** I only saw BlockHash in sprite.frag.hlsl, which is a PCG-style hash. I did not open the other duplicates. Duplicating a small hash or PI is a style trade-off, and the finding is low value.

### SHADERS-14: Ground frame-limited assumptions: grid-fade ordering and water-kind indices aren't defended in the shader

- **Severity:** info · **Verdict:** unverifiable · **Category:** robustness · **Status:** new · **Reviewer confidence:** low
- **Where:** `shaders/ground.frag.hlsl:186`

WaterWash indexes `ground.washes[ground.shallowKind]` and `[ground.deepKind]` straight from uniforms, while KindAt clamps texture-derived kinds to LAST_KIND. An out-of-range index reads past the cbuffer (undefined; robust access may return zeros). Ramp(gridFade.y, gridFade.x, zoom) also collapses to a hard step if a look gives `whole <= gone`. Both rely on the YAML reader's validation, which is outside this scope; I didn't verify it.

**Evidence:** ground.frag:173, :186, :220.

**Impact:** Only if validation is ever loosened: garbage water colour or a popping grid.

**Recommendation:** Clamp the two indices with `min(..., LAST_KIND)` as KindAt does (cost: nothing), and assert `gridFade.x > gridFade.y` where GroundUniformsOf builds them.

**Verification:** I did not read ground.frag. The finding is self-labelled low confidence and relies on validation elsewhere. Clamping the water-kind indices is cheap hardening, and nothing I read decides whether it is needed.

### SHADERS-15: Dissolve block pattern reshuffles every frame while the camera zooms

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** rendering · **Status:** new · **Reviewer confidence:** medium
- **Where:** `shaders/sprite.frag.hlsl:32`

Blocks are `floor((uv - region.xy) / (fwidth(uv) * 4))`. Their size in UV follows screen pixels, so as the zoom changes continuously the block grid rescales and every block's hash changes: during a zoom, a dissolving sprite sparkles randomly rather than keeping its pattern.

**Evidence:** `const float2 block = floor((input.uv - input.region.xy) / max(uvPerPixel * DISSOLVE_BLOCK_PIXELS, 1e-7));`

**Impact:** Cosmetic noise during zoom on dissolving sprites; it could read as flicker for photosensitive players, an accessibility-first concern.

**Recommendation:** Quantise the block size to power-of-two texel sizes (choose a level from log2 of texels per pixel, as mips do), so the pattern stays stable between levels; or accept it and note why.

**Verification:** The code at sprite.frag.hlsl:32 matches the quote: the block size is uvPerPixel * DISSOLVE_BLOCK_PIXELS, taken from fwidth(uv). The block grid therefore rescales as zoom changes, and the hashes change with it. The mechanism is real. Dissolve is a transient effect, so the practical impact is small. The accessibility-first concern is plausible but I did not check any record of this being decided.

### SHADERS-16: shaders/AGENTS.md omits the binding conventions every new shader must follow

- **Severity:** info · **Verdict:** confirmed · **Category:** docs/agents · **Status:** new · **Reviewer confidence:** high
- **Where:** `shaders/AGENTS.md:3`

The directory guide has three rules: file naming, Doxygen, no design references. The conventions that a new shader most needs, and that are enforced or assumed elsewhere, are missing: SDL_GPU register spaces (vertex resources space0, vertex uniforms space1, fragment resources space2, fragment uniforms space3); combined image samplers via `[[vk::combinedImageSampler]]` with matching register and binding; the fog.hlsli include pattern (FOG_TEXTURE_SLOT, the fog's ubo at b0); TEXCOORD index = vertex attribute location; every constant or struct copied from C++ needing a static_assert or offset check in the feature's .cpp; 64-bit/f16 types unavailable (pack by hand); and running `./dev shaders` and committing bin/ and the generated header with the source.

**Evidence:** shaders/AGENTS.md is 6 lines; those rules are scattered over header comments (ground.hlsli:2-3, sprite.hlsli:2-3, fog.hlsli:1-3) and project_n.py:1598.

**Impact:** Agents and contributors rediscover the rules from error messages, or miss the ones nothing checks.

**Recommendation:** Add one line per convention to shaders/AGENTS.md (keeping its ~100-line budget).

**Verification:** shaders/AGENTS.md has three rules: file naming, Doxygen, and no design references. It says nothing about register spaces, TEXCOORD order or fog slots. The binding conventions are in header comments only, and those comments are partly stale (see finding 9). This is a documentation gap, not a bug.
