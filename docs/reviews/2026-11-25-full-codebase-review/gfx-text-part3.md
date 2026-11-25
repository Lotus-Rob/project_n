# Review: gfx-text (part 3)

### GFX-TEXT-21: Shelf packer wastes page space (exact-height shelves, abandoned page tails)

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** performance/memory · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/shelf_packer.cpp:14`

A rectangle fits only a shelf of exactly its rounded height. When the last page lacks vertical room for a new shelf, a whole new page opens, even though a shorter shelf might still fit on an older page's remaining rows. find_if is a linear scan over every shelf ever opened.

**Evidence:** shelf_packer.cpp:77-87.

**Impact:** Lower page occupancy, so more pages and more 4 MiB textures, as CJK coverage grows. Minor now.

**Recommendation:** Allow a shelf up to some slack taller than the rectangle (e.g. ≤ 1.25x), and keep a free-row tail per page.

**Verification:** Cited line 77 does not exist (the file is 30 lines); the code is at lines 10-23. Core holds: a shelf matches only on exactly equal rounded height (SHELF_STEP), find_if scans all shelves, and a new page opens when m_bottom + shelfHeight > pageSize, abandoning the old tail. Quantisation to SHELF_STEP limits the waste, and the scan is cheap at this scale. Efficiency suggestion, not a defect.

### GFX-TEXT-22: distance_field.hpp hard-codes 32/16 and uses the opposite sign convention to glyph fields

- **Severity:** info · **Verdict:** confirmed · **Category:** docs/naming · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/distance_field.hpp:9`

The EncodeDistance doc hard-codes '/ 32' and '16 texels', while the code uses core::FIELD_REACH_TEXELS. Sprite fields encode high = outside (0 deep inside); glyph fields (FreeType) encode high = inside. Both live in gfx under the name 'distance field'.

**Evidence:** distance_field.hpp:9-12 vs distance_field.cpp:13, 105; text_layout.hpp:38-41.

**Impact:** The docs go stale if the reach changes, and someone moving shader code between the sprite and text paths may invert a field.

**Recommendation:** Refer to FIELD_REACH_TEXELS in the docs, and state both conventions next to each other (e.g. in module.dox).

**Verification:** The header doc hard-codes '/ 32' and '16 texels' while distance_field.cpp pads with core::FIELD_REACH_TEXELS. The doc-staleness claim holds. The sign-convention difference between sprite and glyph fields is not something I verified in depth. Documentation nit only.

### GFX-TEXT-23: f32 FAR in the distance transform swallows small squared offsets

- **Severity:** info · **Verdict:** confirmed · **Category:** numerics · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/distance_field.cpp:21`

FAR = 1e10 in f32 has a ulp of 1024, so FAR + q² rounds q² away for |q| < 32. Intersections between two FAR parabolas are then arbitrary, though finite. The result is harmless today: lines that are entirely FAR stay about FAR and clamp to 255, and finite parabolas always win against FAR ones. The comment's guarantee (finite, so the hull loop terminates) holds.

**Evidence:** distance_field.cpp:21, 166-167.

**Impact:** None at present. Wrong envelopes could appear if FAR were reduced, or the encoding stopped clamping.

**Recommendation:** Use a FAR just beyond the largest possible squared distance in a block (e.g. (2*(w+h))²), or note why the imprecision is harmless.

**Verification:** 1e10F lies in [2^33, 2^34), so the f32 ulp is 2^10 = 1024 and the arithmetic claim is right. The reporter itself concedes the result is harmless: results clamp, and finite parabolas beat FAR ones. The comment's guarantee (finite) holds. No action needed.

### GFX-TEXT-24: The layout budget counts all wall time since the frame's first make, not layout time

- **Severity:** info · **Verdict:** deliberate · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/text_layout.cpp:538`

`spent` compares `now - firstMake` with the budget, so any work between TryLay calls in the same frame counts against it: label gathering, StackOf, name composition. This is documented, but it means the effective layout budget shrinks as the per-label CPU work around it grows, and a scene with many labels can starve layout entirely after the first make.

**Evidence:** text_layout.cpp:526-540; text_layout.hpp:30-32.

**Impact:** Labels may take more frames to appear in busy scenes than the budget suggests.

**Recommendation:** Accumulate only the time spent inside Make. If wall time is intended, record why in the constant's doc.

**Verification:** text_layout.hpp:30-32 and the TryLay doc state the budget is wall time since the frame's first make, checked after each make, so a frame always makes at least one. That is documented behaviour. The starvation concern is plausible but is a tuning trade-off with no measurement shown. It is a cheap-clock design choice (one timestamp), not a defect.
