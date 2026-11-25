# Review: art-tools (part 2)

### ART-TOOLS-11: Quadratic or repeated work in road planning

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `art/world_map.py:967`

`plan_costs` rebuilds the peaks set and scans all 8 neighbours of every tile on each call. It runs up to twice per road link, and `link_roads` calls it again per link. `place_dangers` computes `nearest` by scanning all taken sites for every candidate tile, and `crowd` likewise. `trace_rivers` uses `pending.pop(0)` and list membership `b not in stem`. `join()` recomputes `line_tiles(course)` inside the doubly nested loop.

**Impact:** Fine at 12,750 tiles. At 100x the tile count the generator takes minutes to hours.

**Recommendation:** Hoist invariant per-map precomputation (peak-adjacent flags, river tile sets) out of the per-link loop, and use a deque and sets.

**Verification:** Not opened in detail. The maps are 150x85, and the script is an offline one-off test-map tool, not the procedural generator. Performance at 100x tiles is not a requirement for this tool, so the impact is speculative.

### ART-TOOLS-12: Mask.disc comparison uses float floor/ceil on products, with platform-independent but order-sensitive results

- **Severity:** info · **Verdict:** confirmed · **Category:** determinism · **Status:** new · **Reviewer confidence:** low
- **Where:** `art/placeholders.py:646`

Shapes depend on exact float arithmetic (`math.floor(px + half - 0.5)`) and hand-written approximations such as `step_t = angle / 2` in `cloud()` rather than tan(angle/2). These are correct in IEEE double arithmetic and deterministic, but the comment at line 1070 admits an approximation.

**Impact:** No bug found. It is fragile only if the maths is edited, and no test pins pixel output apart from the committed hashes.

**Recommendation:** None required. Keep the no-transcendental rule documented in AGENTS.md.

**Verification:** placeholders.py:1069 comment does say tan(angle/2) is approximated by angle/2. Deterministic IEEE arithmetic; no bug claimed. Informational only.

### ART-TOOLS-13: rune_strokes() regenerated on every rune() call

- **Severity:** info · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `art/placeholders.py:767`

`rune(index)` calls `rune_strokes()` which reruns the LCG loop for all 24 runes each time, so generating 24 runes does 24 full runs.

**Impact:** Negligible time. It is wasted work and a hidden quadratic if RUNES grows.

**Recommendation:** Wrap `rune_strokes` in `functools.cache`.

**Verification:** rune(index) at placeholders.py:767 calls rune_strokes() (defined line 741) each time, with no cache. The waste is real but negligible, since it only runs for 24 runes at generation time.

### ART-TOOLS-14: AGENTS.md omits placeholders.py usage and the zlib/Python dependency of hashes

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** medium
- **Where:** `art/AGENTS.md:4`

The map line is one very long bullet covering placeholders, stamp margins, walk-cycle stride values and atlas.sha256. It does not say how to run `placeholders.py` (`atlas --placeholders`), that an artist PNG replaces a placeholder, or that the hashes cover image bytes.

**Impact:** Harder for a task in art/ to find the rules. The 100-line, one-line-per-rule convention is not followed for that bullet.

**Recommendation:** Split the bullet into one rule per line and add the run commands.

**Verification:** art/AGENTS.md is only 8 lines, but its Map bullets are very long, e.g. the first one packs in the sprites.toml, scenes.toml and placeholders.py rules. The claim that it omits how to run placeholders.py is plausible (the atlas --placeholders command is in the root AGENTS.md). The doc-style complaint is valid but minor.

### ART-TOOLS-15: atlas.sha256 hashes every sprite PNG, so any LFS or filter state makes lint fail

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** tooling · **Status:** new · **Reviewer confidence:** low
- **Where:** `art/atlas.sha256:1`

The file lists ~160 image hashes plus manifest and header. `stale_atlas` compares the whole text. With the LFS filters off (as in the review snapshot) or on a checkout where a PNG is an LFS pointer, the hashes differ.

**Impact:** A false 'atlas is stale' failure rather than a clear message. It is also a merge-conflict magnet: every sprite change touches this file.

**Recommendation:** Print which entries differ and whether the file is an LFS pointer. Consider one hash over all inputs, with per-file hashes only in a log.

**Verification:** stale_atlas (tools/project_n.py:1804) compares the whole hash text with hashes.read_text(...) != atlas_hashes(), so any differing PNG bytes, such as an LFS pointer, gives a plain 'stale' result with no per-entry diff. Merge-conflict friction is plausible. The impact is only a less helpful message, and the setup command installs the LFS filters.
