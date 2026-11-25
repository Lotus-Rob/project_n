# Review: gfx-renderer (part 3)

### GFX-RENDERER-21: SavePng and DifferingPixels trust Image::rgba's size

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/image.cpp:68`

SavePng takes subspans of image.rgba per row without checking `rgba.size() == width*height*4`. DifferingPixels indexes second.rgba using first's pixel count when the dimensions match, without checking the sizes match.

**Evidence:** image.cpp:168-172, 180-188.

**Impact:** A malformed Image (a harness bug) reads out of bounds: an abort under hardening, undefined behaviour in release.

**Recommendation:** Return an error (SavePng) or treat every pixel as differing (DifferingPixels) when the buffer size disagrees with the dimensions, and add a test case for it.

**Verification:** Cited lines 170-188 do not exist (image.cpp has 95 lines). The real code is SavePng at lines 60-70 and DifferingPixels at 76-93. SavePng uses image.rgba.subspan(line*row, row) with no size check against width*height*4. DifferingPixels sizes its loop from first.rgba.size() and indexes second.rgba[byte] after only comparing width and height. So a malformed Image could read out of bounds. But Image only comes from LoadPng, which builds it consistently (line 48), and it is test-harness code. Impact is very low.

### GFX-RENDERER-22: Sprite array checks R8 colour-target support but not sRGB RGBA8 array support; sampler max_lod underflows on mipLevels 0

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/gfx/internal/resources/sprite_array.cpp:108`

CreateSpriteArray checks only FIELD_FORMAT for SAMPLER|COLOR_TARGET on 2D_ARRAY, although the colour array (R8G8B8A8_UNORM_SRGB) also needs COLOR_TARGET for the clears and mip blits. CreateSampler computes `mipLevels - 1` in u32.

**Evidence:** sprite_array.cpp:183-187, 124.

**Impact:** On a device lacking sRGB colour-target arrays, creation fails with a generic error. A manifest with 0 mip levels gives max_lod of about 4e9.

**Recommendation:** Check both formats with SDL_GPUTextureSupportsFormat. Assert or validate `mipLevels >= 1` where the manifest is read.

**Verification:** The support check is at line 108 and covers only FIELD_FORMAT (R8) with ARRAY_USAGE, so the missing sRGB RGBA8 check is true. Support for RGBA8 sRGB colour targets is effectively universal, so the risk is negligible. The mipLevels underflow is refuted: data/manifest.cpp:148-150 rejects mipLevels below MIN_MIP_LEVELS, and line 159 already relies on mipLevels - 1.

### GFX-RENDERER-23: Terrain is laid out again after a failed submit even when the layout for the key is already there

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/renderer.cpp:594`

UploadTerrain re-runs DrawTerrain whenever `terrainHeld != key`, even if `terrainLaid == key` (the layout is current and only the upload needs repeating).

**Evidence:** renderer.cpp:594-600.

**Impact:** Minor: wasted CPU only on retry frames.

**Recommendation:** Skip DrawTerrain when `terrainLaid == key` and only re-upload.

**Verification:** The code differs from the description. Lines 592-596 return early only when terrainHeld == key AND terrainLaid == key. Otherwise DrawTerrain always runs. On a retry where terrainLaid == key but terrainHeld != key, DrawTerrain is repeated for no reason. That is true but only wastes CPU on the rare retry after a failed upload. I did not check whether DrawTerrain output could have been overwritten in between.

### GFX-RENDERER-24: Draw() documentation misstates when instances upload

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/renderer.hpp:101`

The doc says Draw 'uploads its instances only when it isn't the snapshot drawn last'. In fact the rule is 'unless the GPU holds the upload of a snapshot with the same (epoch, wallTick), counted once submitted', and uploads.hpp's premise that wall ticks never repeat is false (see the snapshot-identity finding).

**Evidence:** renderer.hpp:101-102; uploads.hpp:16.

**Impact:** The doc misleads readers about a correctness-relevant invariant.

**Recommendation:** Update both documents when fixing the snapshot identity.

**Verification:** The Draw doc is terse ('only when it isn't the snapshot drawn last'). uploads.hpp:16 does say 'wall ticks never repeat within an epoch'. The finding depends on a separate snapshot-identity finding that I have not seen, so I cannot confirm that the premise is false. The doc imprecision is plausible, and the impact is documentation only.

### GFX-RENDERER-25: A seal key logged as missing is never logged again after a reload

- **Severity:** info · **Verdict:** confirmed · **Category:** logging · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/renderer.cpp:718`

sealsMissing is never cleared in SetSeals. If a reload adds a missing key and a later reload removes it again, nothing is logged the second time.

**Evidence:** renderer.cpp:717-723; SetSeals (1617-1620) doesn't touch sealsMissing.

**Impact:** A seal that silently draws nothing is confusing while hot-reloading seals.yaml.

**Recommendation:** Clear sealsMissing in SetSeals.

**Verification:** sealsMissing (line 436, 'each logged once') is only pushed to at 720 and never cleared; SetSeals (1617-1620) only replaces sealLooks and the pack. After a reload that adds a key and a later reload that removes it, the warning is suppressed. Minor and low impact: it only affects hot-reload logging. Clearing it in SetSeals is the right fix. I did not look for a test or a recorded decision.
