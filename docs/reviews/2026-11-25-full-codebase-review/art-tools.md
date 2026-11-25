# Review: art-tools

**Scope:** art/*.py, art/*.toml, art/AGENTS.md, art/atlas.sha256: generator correctness, determinism of outputs, coupling to world.yaml, scaling to procedural maps.

**Findings:** 15 standing (0 critical, 0 high, 0 medium, 9 low, 6 info), 0 raised but refuted on verification.

**Parts:** art-tools.md, art-tools-part2.md

## Assessment

## art-tools review: art/*.py, art/*.toml, art/AGENTS.md, art/atlas.sha256

I read all of `placeholders.py`, `world_map.py`, `sprites.toml`, `scenes.toml`, `AGENTS.md` and the head and tail of `atlas.sha256`. I read the hash and packing code in `tools/project_n.py` and the `WorldMapRoadsTest` self-test for context. I did not build or run anything.

**Strengths**
- Both generators are standard-library only and avoid transcendental functions. They use integer hashing, `sqrt`, a fixed seed, and sorted or stable tie-breaks (`(-score, t)`, `max(sorted(counts))`). I found no non-determinism sources such as set iteration order or `id()` ordering. `id()` is used only as a dict key, never to order anything.
- The PNG writer pins zlib level 9 and the Sub filter. Output is byte-reproducible only for a fixed Python and zlib build, which is a mild cross-host risk (see the zlib finding).
- `world_map.py` mirrors the C++ rules (`StepCost`, `Rasterise`, ford and danger handling). The self-test `test_the_shipped_roads_are_the_ones_the_file_routes` and the idempotency test catch drift between `world.yaml` and the routing.
- Fit-checking in `forest()` and the stamp margins is carefully reasoned, and `PlaceholderTest` holds the margins.

**Weaknesses and risks**
- `world_map.py` is a 1,700-line, single-seed, hand-authored generator. About 40 hand-placed constants (sea ellipses, range spines, passes, city tiles) are tuned to one 150x85 map. Nothing in it scales to larger or other maps. It is also a Python reimplementation of game rules (cost table, step rule, rasteriser, `MAX_WALK_COST`, ford and danger semantics) that are not cross-checked against C++ constants, only against the roads in the shipped file.
- All pipelines are pure-Python per-pixel or per-tile loops. They are fine at this size and would be unusable at tens of thousands of tiles per side.
- `art/AGENTS.md` is accurate but terse. It does not mention that `world_map.py` aborts via `SystemExit` for several input conditions, or that a plain run rewrites the whole `world.yaml` (it does say a rerun starts over).

**Overall health:** good for what it is, a test-map and placeholder-art generator. The main risks are drift from C++ and fragility if the map inputs or the constants (`WORLD_BUSIEST`, costs) change.

**Files read in full:** (not reported)

## Findings

### ART-TOOLS-01: Generator constants duplicated from C++ with no cross-check

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** medium
- **Where:** `art/world_map.py:55`

`BUSIEST=(33_000, 10_800)` (game::WORLD_BUSIEST in src/game/scenes/world.hpp), `MAX_WALK_COST=1000` (core::MAX_WALK_COST), `ROAD_COST`, `FORD_COST`, the KINDS cost table and the `7/5` diagonal factor are hand-copied. The only guard is the roads-staleness self-test over world.yaml, which does not read the C++ headers.

**Impact:** If WORLD_BUSIEST, MAX_WALK_COST or the step rule changes in C++, the generator silently keeps the old values. A rerun then puts the port at the wrong tile and routes roads with stale costs. The staleness test still passes because it only compares the generator with itself.

**Recommendation:** Add a self-test that parses the C++ constants (or have the game emit them into a small file the script reads), or move the shared rules into data that both sides load.

**Verification:** The values do match the C++ (world.hpp:21 WORLD_BUSIEST, world_map.hpp:32 MAX_WALK_COST=1000), and nothing in tools/test_project_n.py references them, so no cross-check exists. But the lines carry comments naming the C++ constant, and the map is a hand-tuned test map whose roads are re-routed on request. The staleness self-test would not catch drift, as claimed. Impact is small.

### ART-TOOLS-02: The map generator is one hand-tuned map, not a procedural generator

- **Severity:** low (reviewer said medium) · **Verdict:** deliberate · **Category:** scaling · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `art/world_map.py:79`

SEA, ISLANDS, SHOALS, RANGES, GREAT_RIVER, PASSES, LAKES, CITIES and DANGERS are literal coordinates for a 150x85 map with SEED=7. Functions such as `great_river_path`, `delta` and `settle` raise SystemExit when the land does not match those hand inputs. The module header and AGENTS.md note that world.yaml is a test map.

**Impact:** The recorded decision says maps are procedural per seed. This script cannot serve that: a different SEED or size fails or produces nonsense, and the real procedural generator would have to be written separately. Rivers, roads and fords logic here is not reusable at scale (pure Python, O(N log N) several times, `add_fords` runs up to 25 full Dijkstra passes).

**Recommendation:** State plainly in AGENTS.md that this is a one-off test-map tool and not the seed-driven generator. When the procedural generator is written, port the reusable parts (rivers, roads, ford detour test) rather than extending this file.

**Verification:** The module docstring and art/AGENTS.md describe world.yaml as hand-tuned and the source after generation. The memory note says world.yaml is only a test map. The script is that test-map tool, so it does not contradict the procedural decision. The only valid point is that AGENTS.md could say outright that it is not the seed-driven generator. Not read in full: the scaling claims (25 Dijkstra passes, etc.).

### ART-TOOLS-03: join() and related helpers crash on a single-point polyline

- **Severity:** low · **Verdict:** unverifiable · **Category:** correctness · **Status:** new · **Reviewer confidence:** low
- **Where:** `art/world_map.py:620`

`min(... for i in range(len(course) - 1))` in `join()` and `through()` raises ValueError on an empty range when a polyline has one point. `delta()` can return a one-point line when the split tile is already within 1 of the sea, because `simplify` returns `points[:]` for fewer than 3 points. `delta()` also indexes `path[-1 - DELTA_SPLIT]` and raises IndexError for short great-river paths.

**Impact:** A changed map raises an unhelpful traceback instead of the script's usual explicit SystemExit message.

**Recommendation:** Guard for `len(course) < 2` and a short main channel, and fail with a clear message.

**Verification:** join() does use min over range(len(course)-1), which is empty for a one-point polyline (seen at lines 610-622). Whether delta() or simplify can actually return one point depends on the fixed inputs (SEED=7 and the literals). The shipped map generates, so this is a hypothetical for changed inputs and affects only the error message. Not traced further.

### ART-TOOLS-04: fray_shore applies mutations that cut across keep and city overrides

- **Severity:** low · **Verdict:** unverifiable · **Category:** correctness · **Status:** new · **Reviewer confidence:** low
- **Where:** `art/world_map.py:1681`

After `fray_shore` the loop `for x, y in [tile for ... in CITIES]: land[...] = True` forces city tiles to land even where an inlet was cut, after harbour() computed its keep set. The inlet to the port protects only the port tile and its sea neighbours.

**Impact:** A city tile forced onto sea can leave a one-tile land speck in open water that `drop_specks` already ran before and cannot clean. `settle` then moves such cities only for non-port cities.

**Recommendation:** Apply the city overrides before `fray_shore` and include them in `keep`, or check that each city tile has land neighbours.

**Verification:** The order is as described (fray_shore, then the city override at 1680-1682). Whether the inlets reach a city tile is determined by the fixed inputs, which I did not trace. Plausible but hypothetical.

### ART-TOOLS-05: Road routing fit_line can silently produce a line that does not follow the path

- **Severity:** low · **Verdict:** unverifiable · **Category:** correctness · **Status:** new · **Reviewer confidence:** low
- **Where:** `art/world_map.py:1077`

When a simplified segment fails `fits`, the fallback uses `corners.index(a)`/`corners.index(b)`. `index` returns the first occurrence, so repeated corner points (a path revisiting a tile, or pulled corners that coincide) select the wrong slice.

**Impact:** A road can skip or repeat a stretch. `derive` or `join_forks` later raises SystemExit with a confusing message, or a wrong road ships if it happens to stay passable.

**Recommendation:** Track corner positions by index (carry the pulled index list through `simplify`) instead of searching by value.

**Verification:** Not opened. Whether corner points repeat, and whether derive or join_forks catches a bad line downstream, was not checked. The shipped roads are covered by the staleness self-test.

### ART-TOOLS-06: PNG output byte-reproducibility depends on the host zlib

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** determinism · **Status:** new · **Reviewer confidence:** medium
- **Where:** `art/placeholders.py:167`

The docstring promises 'byte for byte' reproducible output, but `zlib.compress(..., 9)` output is identical only for the same zlib implementation and version (zlib-ng, which some Python builds use, compresses differently). atlas.sha256 hashes every PNG.

**Impact:** A Pixi update that changes the Python or zlib build makes `atlas --placeholders` rewrite every PNG with different bytes. Image hashes and `lint`'s stale-atlas check then fail until the hash file and images are all recommitted, with no visible art change.

**Recommendation:** Hash decoded pixels instead of file bytes, or write stored (level 0) deflate blocks, or pin and note the zlib dependency. Mention it in AGENTS.md.

**Verification:** zlib.compress(...,9) is used at line 167, and tools/project_n.py:1801 hashes PNG file bytes into atlas.sha256. Output can differ across zlib implementations. Python and zlib are pinned exactly in pixi.toml/pixi.lock (the AGENTS.md rule that pins are exact), so drift happens only on a deliberate toolchain bump. The docstring claim holds under the pinned environment. Worth a note in bump-toolchain, not a defect.

### ART-TOOLS-07: Dead sprites kept 'for stable ID' contradict the no-compat-before-1.0 rule

- **Severity:** low · **Verdict:** confirmed · **Category:** dead-code · **Status:** contradicts-recorded-decision · **Reviewer confidence:** high
- **Where:** `art/sprites.toml:1`

`halo` and `glow_core` are generated, listed and hashed but in no scene ('Unused since the swords wear glows: kept for its stable ID'). The same file's header and the memory note say IDs may change freely until 1.0.0 and dead sprites should be removed. `PLACEHOLDERS` also keeps their painters.

**Impact:** Dead art is packed into the atlas and hashed, and the comments cite a stability reason that does not apply pre-1.0.

**Recommendation:** Remove `halo`, `glow_core`, their painters, PNGs and manifest entries, then rerun `atlas`.

**Verification:** sprites.toml has [halo] and [glow_core] marked 'kept for its stable ID, in no scene', and scenes.toml does not mention them. Painters remain at placeholders.py:498 and 509 and in PLACEHOLDERS (1482, 1486). art/AGENTS.md says IDs may change until 1.0.0, so the stated reason is stale. The memory note says to remove dead sprites. The placeholders.py header (line 6) also notes both are 'unused now'.

### ART-TOOLS-08: Scene lists hand-duplicated and unchecked against what the code draws

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** duplication · **Status:** known-deferred · **Reviewer confidence:** low
- **Where:** `art/scenes.toml:5`

The `arena` and `stress` scenes repeat the same 30+ sprite names by hand. The scene list is the only record of which sprites a scene needs. Missing seal or creature sprites are listed as known-deferred (seal sprites unchecked against the scene list).

**Impact:** Adding a sprite to one scene and forgetting the other, or forgetting to list a drawn sprite, only surfaces at runtime as a missing upload.

**Recommendation:** Support an `extends` or `include` key in `scenes.toml` (checked by `atlas`), and add a lint that every SpriteId used in code appears in a scene.

**Verification:** scenes.toml header says each scene lists every sprite it draws, so the hand listing is by design. The duplication between arena and stress is plausible but I did not count the names. I did not verify the claimed 'known-deferred' status in the backlog. An extends key would be a feature request, and the missing-sprite check is a runtime matter.

### ART-TOOLS-09: read_world parser is a bespoke YAML subset that can mis-parse hand edits

- **Severity:** low · **Verdict:** unverifiable · **Category:** robustness · **Status:** new · **Reviewer confidence:** low
- **Where:** `art/world_map.py:1541`

`parse_flow`/`read_world` re-implement a flow-YAML reader: no escapes in quoted strings, `#` handling only for ' #', no block scalars other than the grid, and no support for anchors. A top-level key without `:` or a continuation line under a scalar key is silently concatenated.

**Impact:** A hand edit with an unusual construct is read wrongly. `--roads` then rewrites roads from wrong data and the self-test cannot tell, because it reads with the same parser.

**Recommendation:** Use PyYAML (or ruamel) if available in the Pixi environment; otherwise fuzz the parser against the C++ reader's accepted forms.

**Verification:** I did not open read_world/parse_flow in world_map.py. Whether a hand edit is misread would need testing the parser against edge cases. The risk is bounded because world.yaml is a repo-owned file and the selftest compares roads against the same parser.

### ART-TOOLS-10: Rivers rewritten on every plain run lose hand-placed sources and edits

- **Severity:** info (reviewer said low) · **Verdict:** deliberate · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `art/world_map.py:1674`

A plain run overwrites world.yaml entirely. AGENTS.md says hand-placed river sources and all hand edits are lost, and nothing refuses or backs up an edited file. main() prints a notice but writes unconditionally.

**Impact:** An accidental `python art/world_map.py` without `--roads` destroys hand tuning that exists only in the YAML, recoverable only through git.

**Recommendation:** Require a `--regenerate` flag or refuse when the working tree's world.yaml is dirty or differs from what the script would write.

**Verification:** The module docstring and art/AGENTS.md line 8 say a rerun loses hand edits. main() prints 'this starts over and overwrites ... hand edits' before writing. A plain run needs an explicit invocation with no args, and git recovers the file. A guard flag would be a nicety only.
