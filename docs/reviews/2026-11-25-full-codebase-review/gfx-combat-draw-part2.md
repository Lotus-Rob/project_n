# Review: gfx-combat-draw (part 2)

### GFX-COMBAT-DRAW-11: DrawHealthBars adds a zero-length fill instance at zero health (DrawHudBars skips it) and divides by thickness unchecked

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** consistency · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/volley_draw.cpp:207`

For fraction 0 the fill is still added with widthScale 0, so it uses overlay room for nothing. DrawHudBars skips the fill at 0. `scale = thickness / height` and `length / (width * scale)` divide by zero when effects.yaml sets a 0 height or fill height (if the reader allows it), giving an inf or NaN widthScale.

**Evidence:** `add(look.fillWidth, look.fillWidth * fraction, look.fillHeight, ...)` with no `fraction > 0` guard, unlike DrawHudBars.

**Impact:** Wasted overlay instances (which compete with swords, see above). The two bar drawers behave differently for the same input.

**Recommendation:** Skip the fill at fraction 0 as DrawHudBars does, and guard the division by a zero thickness or height.

**Verification:** The inconsistency is real: volley_draw.cpp:~207 adds the fill with no fraction>0 guard, while DrawHudBars at :242 guards it. The zero-division part is refuted, since effects.cpp:146 reads fill_height with POSITIVE, and height is presumably the same. The remaining cost is one wasted instance per zero-health bar.

### GFX-COMBAT-DRAW-12: Health bars silently disappear whenever glyph pages fail, because barsShown is filled only in PlaceTexts

- **Severity:** low · **Verdict:** confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/volley_draw.cpp:181`

DrawHealthBars draws a bar only for an entity found in `shown` (barsShown). The renderer fills barsShown inside PlaceTexts, which runs only `if (glyphs)`. When MakeGlyphPages fails, every health bar vanishes, though the bars need no glyphs.

**Evidence:** renderer.cpp:464 `barsShown.clear();` and :471 `if (glyphs) { PlaceTexts(shown, jobs); }`; renderer.cpp:814 pushes into barsShown inside label placement.

**Impact:** A text-system failure (for example running out of GPU memory for a glyph page) also hides every enemy's health, a gameplay-relevant loss that is logged only as a glyph failure.

**Recommendation:** Decouple them: fall back to showing every bar at full opacity when labels weren't placed, or document the dependency in DrawHealthBars' contract.

**Verification:** renderer.cpp:464 clears barsShown. PlaceTexts runs only `if (glyphs)` (~471), and barsShown is pushed in label placement (:814). DrawHealthBars (:483) draws only for entities in shown (opacity 0 otherwise). So a glyph page failure hides every health bar. Real coupling, but it is a degraded-mode edge case. Whether it is a deliberate choice, since bars are tied to labels' fade, I did not verify.

### GFX-COMBAT-DRAW-13: Swords and seals have hard per-frame caps (2,048 swords, 64 seals) and are laid out per item on the CPU; hidden swords still use room

- **Severity:** info (reviewer said low) · **Verdict:** deliberate · **Category:** scaling · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/gfx/volley_draw.hpp:54`

MAX_DRAWN_SWORDS is min(8192, 16384*4/32) = 2,048. Each sword of every volley in the snapshot takes an instance and 8 ribbon quads from the first frame until the volley leaves, including swords not yet launched or already struck (zeroed). MAX_SEALS_DRAWN = 64 includes satellites. All of this is CPU work each frame.

**Evidence:** `DrawSword` writes zero instances/trails for hidden swords; `SwordRoom`; `MAX_SEALS_DRAWN = 64`.

**Impact:** Two overlapping 1,000-sword volleys nearly fill the frame, and NPC sword users would drop whole swords. A battle of formations is capped at 64 seals.

**Recommendation:** Already planned: GPU projectiles at scale and seal layout on the GPU. Until then, consider leaving out volleys whose swords have all gone.

**Verification:** The finding itself is marked known-deferred. The caps exist (MAX_DRAWN_SWORDS, MAX_SEALS_DRAWN), and renderer.cpp:539 counts and reports the dropped swords. I did not find a design doc entry for the GPU-projectile plan, so the deferral is taken from the finding's own status.

### GFX-COMBAT-DRAW-14: StampReach and LayStamp divide by a sprite's width and cast the result to u32 unchecked: undefined for a zero-width sprite

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** undefined-behaviour · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/gfx/stamp_draw.cpp:164`

`tallest = widest * size.y / size.x` and `size.y / size.x` for puffs give inf or NaN when a sprite's width is 0. `static_cast<u32>(std::ceil(...))` of inf, NaN or a value ≥ 2^32 is undefined behaviour. LayStamp's `width / SizeOf(manifest, sprite).x` gives an infinite scale. cloud_draw guards `info.width == 0`; stamp_draw does not.

**Evidence:** `return static_cast<u32>(std::ceil(std::max(tallest, mostHalfWidth) + jitter + puffHalf));`

**Impact:** Only reachable with a degenerate manifest entry, or with huge stamp sizes in world_look.yaml if the reader does not cap them. Then the widening is undefined or huge, and the Widened() addition can overflow u32.

**Recommendation:** Skip zero-sized sprites (as cloud_draw does), and clamp the reach to the map's size before the cast.

**Verification:** stamp_draw.cpp:~149 and ~158 divide by size.x unchecked, and cloud_draw.cpp:267 does guard width==0. But world_look.cpp:135 caps STAMP_SIZE at (0,4], so the u32 cast overflow from huge stamp sizes can't happen. Only a zero-width manifest sprite triggers it, and that is generated by the atlas tool. Practically unreachable.

### GFX-COMBAT-DRAW-15: Glow record colour 0 means 'use the element colour', which a transparent palette colour would also produce

- **Severity:** info · **Verdict:** confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/glow_table.cpp:19`

`.colour = look.colour.element ? 0U : colours.Of(look.colour.colour)`. The shader treats 0 as 'the instance's element colour'. A palette entry that is fully transparent black (premultiplied 0x00000000) cannot be told apart from it.

**Evidence:** glow_table.hpp: "Linear premultiplied RGBA8 (core::UiColours::Of()); 0 for the ability's element colour."

**Impact:** A palette edit giving a glow colour alpha 0 (to hide it) would instead show the element colour. Unlikely, but surprising.

**Recommendation:** Add an explicit flag in GlowRecord (padding is available), or refuse alpha-0 glow colours in the reader.

**Verification:** glow_table.cpp:19 uses 0U as the element-colour sentinel (`look.colour.element ? 0U : colours.Of(...)`), so a transparent palette colour is indistinguishable from it. Real, but only reachable by a deliberate palette edit that makes a glow colour fully transparent.

### GFX-COMBAT-DRAW-16: GlowTimeAt: fraction can round to 1.0F, alpha > 1 drops its fraction, NaN alpha gives a NaN fraction

- **Severity:** info · **Verdict:** confirmed · **Category:** numerics · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/glow_table.cpp:50`

`static_cast<f32>(std::max(alpha, 0.0))` rounds an alpha like 0.99999999997 to 1.0F, which breaks the documented 'up to (not including) 1'. Any alpha ≥ 1 returns fraction 0 at simulationTick, which disagrees with core::RenderTick for alpha > 1. std::max(NaN, 0.0) returns NaN.

**Evidence:** `if (alpha >= 1.0) { return { .tick = static_cast<u32>(simulationTick), .fraction = 0.0F }; } return { .tick = static_cast<u32>(simulationTick - 1), .fraction = static_cast<f32>(std::max(alpha, 0.0)) };`

**Impact:** Harmless at present (tick + 1.0 equals the next tick, and alpha is presumably clamped upstream), but the contract and the tests don't cover these edges.

**Recommendation:** Clamp the narrowed fraction below 1 (std::nextafter(1.0F, 0.0F)) or carry it into the tick. Treat NaN as 0. Document that alpha is clamped to [0, 1].

**Verification:** glow_table.cpp GlowTimeAt matches the cited code. alpha>=1 gives fraction 0 at simulationTick. A NaN alpha passes through std::max(NaN,0.0) as NaN. Narrowing 0.99999999997 to f32 gives 1.0F. All harmless: tick+1.0 equals the next tick, and alpha comes from the loop accumulator.

### GFX-COMBAT-DRAW-17: Mist puffs are a separate batch drawn over every stamp, so a far tile's puff covers nearer trees

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** rendering · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/gfx/stamp_draw.cpp:337`

Stamps and paintings are sorted by foot, but all puffs go in one later batch. A puff belonging to a farther-up tile draws over a nearer (lower) stamp that overlaps it, against the back-to-front rule that the stamps themselves follow.

**Evidence:** `addBatch(..., puffs.size()); into.instances.insert(into.instances.end(), puffs.begin(), puffs.end());` after the sorted things.

**Impact:** Mist sometimes veils foreground trees or paintings that should stand in front of it. This may be intended (the reference picture world_mountains was approved), but it is not documented as deliberate.

**Recommendation:** If it is intended, say why in the header, for example that mist always veils. Otherwise interleave the puffs into the sorted list.

**Verification:** The code matches the claim: sorted things go in one batch (line 331), then all puffs go in a second batch (lines 336-337). stamp_draw.hpp:23 documents the puff batch as 'one normal batch of the mist puffs, laid among the stamps', so the separate batch is stated. The header does not say why mist should draw over nearer stamps. I did not find a record that it is deliberate.

### GFX-COMBAT-DRAW-18: SealLookIndex duplicates core::Seals::Find's lower_bound

- **Severity:** info · **Verdict:** confirmed · **Category:** duplication · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/seal_draw.cpp:671`

SealLookIndex repeats Seals::Find's lower_bound over `looks` by key, returning an index instead of a pointer. Both rely on the reader's sort.

**Evidence:** core/seals.hpp:285 Find uses `std::ranges::lower_bound(looks, key, {}, &std::pair<SealKey, SealLook>::first)`; seal_draw.cpp:673 does the same.

**Impact:** Two places to change if the lookup changes (for example to a hash map for 1,024 looks).

**Recommendation:** Add `std::optional<std::size_t> Seals::IndexOf(SealKey)` and implement Find with it.

**Verification:** core/seals.hpp:285-288 Find and seal_draw.cpp:671-677 SealLookIndex both do the same lower_bound on looks by .first. The duplication is real but tiny. The index is needed to address frame.pack->looks, so the proposed IndexOf is reasonable.

### GFX-COMBAT-DRAW-19: cloud_draw.hpp uses std::size_t without including <cstddef>; glow_table.cpp's include blocks differ from the rest of gfx

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** conventions · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/cloud_draw.hpp:33`

CloudDrawing::dropped is a std::size_t, but the header includes only <vector> from the standard library (src/AGENTS.md: headers include everything they use). glow_table.cpp puts its own header and core/cxx23.hpp in one block, then core/tick_clock.hpp in a second project block, unlike its siblings.

**Evidence:** cloud_draw.hpp includes: core/frame.hpp, core/manifest.hpp, core/rect.hpp, core/types.hpp, core/vec2.hpp, core/world_look.hpp, gfx/fog.hpp, <vector>.

**Impact:** It compiles only through transitive includes. Style drift.

**Recommendation:** Add <cstddef> to cloud_draw.hpp, and make glow_table.cpp's includes one block like the sibling files.

**Verification:** cloud_draw.hpp uses std::size_t at line 33 and includes only <vector> from the standard library. I did not check whether glow_table.cpp's include blocks differ. Whether <cstddef> comes in through core/types.hpp, which is not verified, does not change the finding. Style only.

### GFX-COMBAT-DRAW-20: SealDrawing::tinySeals also counts negative and infinite sizes under 'tiny'

- **Severity:** info · **Verdict:** confirmed · **Category:** naming · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/seal_draw.cpp:707`

`if (!std::isnormal(seal.size) || seal.size < 0.0F)` puts negative, infinite and NaN sizes, along with subnormal ones, under tinySeals, and the renderer logs them as 'too small to lay out'.

**Evidence:** renderer.cpp:79: "Logs ... seals DrawSeals() left out for a size too small to lay out".

**Impact:** Misleading log text for a corrupt snapshot (core::Seal::Valid rejects these from saves, so only code-made seals reach here).

**Recommendation:** Rename it to something like `unsizedSeals`, or log the size itself.

**Verification:** seal_draw.cpp:707 sends non-normal and negative sizes, which includes NaN and infinity, to tinySeals. seal_draw.hpp:458 documents it as 'a seal whose size isn't a normal f32', so the behaviour is documented. Only the name and the renderer's log wording ('too small to lay out') mislead for NaN, infinity and negative sizes. Low impact.
