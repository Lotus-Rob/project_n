# Review: x-performance (part 3)

### X-PERFORMANCE-21: Relationships::Forget and Travel::Forget are O(E) and O(n) per NPC, so Fold is quadratic in bulk; nothing outside the tests calls it

- **Severity:** low · **Verdict:** confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/npcs.cpp:292`

Fold calls relationships.Forget (an erase_if over all edges) and travel.Forget (a vector erase in journals and journeys, with a linear find_if fallback) for each NPC. The tier model's fold and materialise path, meant for LOD with thousands of NPCs, has no production caller: grep finds Fold and Materialise only in tests/unit/game/npcs_tests.cpp.

**Evidence:** `std::erase_if(m_edges, [npc](const Relationship& edge) { return edge.holder == npc || edge.subject == npc; });` called per folded NPC

**Impact:** When LOD folding is wired up to fold many NPCs at once (a region the player leaves), folding k NPCs costs O(k x E), millions of edges times thousands of NPCs.

**Recommendation:** Give Fold a batch form that takes a sorted set of IDs and filters the edges, journals and journeys in one pass each. Until then, mark the tier API as unused in its header so nobody assumes it scales.

**Verification:** Fold (l.292-299) calls relationships.Forget and travel.Forget per NPC. grep over src finds Fold and Materialise only in the npcs.hpp/.cpp declarations and definitions, with no production caller. So the cost is latent and is not on any current path. I did not open Forget's body, and the O(E) claim rests on the finding's quoted erase_if. The recommendation to batch it before wiring up LOD is sound.

### X-PERFORMANCE-22: Full-map fog and ground textures take one texel per tile, so MAX_TILES admits maps no GPU texture can hold

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/world_map.cpp:16`

WorldMap::Build accepts any columns*rows up to MAX_TILES = 2^24 with no limit per axis. FogTexture (sprites.cpp:309-314) and GroundRenderer (ground.cpp:90-91) create a 2D texture of columns x rows. GPUs cap 2D textures at 16384 per side (and some at 8192).

**Evidence:** `constexpr u32 MAX_TILES = 1U << 24;` / `static_cast<u64>(source.columns) * source.rows > MAX_TILES`; `info.width = columns; info.height = rows;`

**Impact:** A generated map wider or taller than the device limit, for example 20000 x 800, passes validation and then fails texture creation at runtime: the fog is logged as not drawn and the ground goes missing.

**Recommendation:** Bound columns and rows each by the minimum guaranteed texture size (and check against SDL's reported limits at SetWorld), or tile the field textures into sectors.

**Verification:** MAX_TILES = 1U<<24 is at l.16 and bounds only the product, so a 20000x800 map passes validation. I did not open the sprites.cpp and ground.cpp texture creation to confirm one texel per tile. The finding says they create columns x rows textures, and the 2^24 cap is consistent with that. Per-axis limits are not checked in world_map.cpp. The shipped and generated maps are far smaller, and an extreme aspect ratio is hypothetical. The map is procedural with its size set by the generator, so the fix is a per-axis cap or a generator bound.

### X-PERFORMANCE-23: Every new snapshot uploads every instance to the GPU (5.6 MB per tick in the stress scene)

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/gfx/internal/features/sprites.cpp:382`

SpriteRenderer::Upload maps the transfer buffer and copies the whole instance span (56 B each) for every new snapshot, at the 60 ticks/s publish rate. Bullets and crowds are ECS entities, each publishing its own instance.

**Evidence:** `std::ranges::copy(bytes, BytesAt(mapped, bytes.size()).begin()); ... SDL_UploadToGPUBuffer(copy, &source, &destination, true);` for every `newInstances`

**Impact:** At 100k instances: about 336 MB/s of CPU memcpy plus PCIe traffic. This is the ceiling for bullet-heavy scenes.

**Recommendation:** This is already planned as GPU projectiles at scale plus sprite instancing. Keep it on the plan, and consider uploading by delta or persistent mapping meanwhile.

**Verification:** SpriteRenderer::Upload (sprites.cpp:~382) does copy the whole instance span, capped at m_capacity, and it runs only when there is a new snapshot, as claimed. The 100k-instance figure is hypothetical. The 5.6 MB per tick figure is not checked, and the claim that bullets are ECS entities publishing one instance each conflicts with src/AGENTS.md, where volleys are single records placed at render time. The finding itself says it is known-deferred. Medium is too high for a stress-scene ceiling.

### X-PERFORMANCE-24: Visit builds a reversed route vector only for MakeTileLeg to copy it again; each journey holds four route-length vectors

- **Severity:** low · **Verdict:** confirmed · **Category:** memory · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/core/journey.cpp:501`

Visit allocates `back` (the reversed tiles), passes it as a span to MakeTileLeg, which copies it into leg.tiles, so the temporary is thrown away. Together with SetOut's `route` vector, each journey allocates about six route-length vectors and keeps four (the outward and homeward tiles and arrive ticks), though the homeward tiles are the outward ones reversed.

**Evidence:** `const std::vector<u32> back{ out.rbegin(), out.rend() }; Leg homeward = MakeTileLeg(map, profile, stay.endTick, map.TileCentre(out.back()), back);`

**Impact:** Allocation churn in the month step and memory proportional to the total route length times 2. Already being addressed.

**Recommendation:** Covered by the 'journeys copy tile vectors (being fixed on branch shared-routes)' item. Make sure the shared-routes branch also removes the `back` temporary.

**Verification:** journey.cpp:501 builds the `back` vector, which MakeTileLeg then copies. in-flight.md and the approved shared-routes spec and plan (stage 1) are already addressing journey tile-vector copies, so this is a known deferral. The temporary is real.

### X-PERFORMANCE-25: GameData::BaseHash and Hash re-hash the whole map grid on every call

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/game_data.hpp:97`

BaseHash() and Hash() call world->Hash(), which mixes every grid byte and every road and river point, each time. Host::Load calls BaseHash twice per load (ReadGameWorld and the m_loaded emplace), and DataHash is called at each replay start or stop.

**Evidence:** `hasher(world ? world->Hash() : u64{ 0 }, names ? names->Hash() : u64{ 0 });`

**Impact:** One Mix per tile per call: about 100 ms per call at MAX_TILES, repeated for no reason because the map is immutable.

**Recommendation:** Compute the map's hash once in WorldMap::Build (it is immutable) and the names' hash once, and store them.

**Verification:** game_data.hpp:88-99 call world->Hash() every time. WorldMap::Hash (world_map.cpp:728) mixes every grid byte, so it is O(tiles) per call. It is called from Host::Load, logic.cpp replay start and play, and app.cpp reload comparisons, all rare and not per frame. The '~100 ms at MAX_TILES' figure is unmeasured. Caching in the immutable WorldMap is a sound, cheap fix, but the impact is low.

### X-PERFORMANCE-26: DrawList::Publish copies every instance three times (parts, then a concatenation, then the sorted snapshot) before the GPU copy

- **Severity:** info (reviewer said low) · **Verdict:** unverifiable · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/draw_list.cpp:115`

Instances are written into the parts, copied in parallel into m_instances, m_ids and m_entries, sorted, then gathered into snapshot.instances. The concatenation exists only so the gather can index a single array.

**Evidence:** `m_instances.resize(total); ... std::ranges::copy(m_parts[part].instances, std::span{ m_instances }.subspan(offset).begin()); ... sorted[i] = m_instances[m_entries[i].index];`

**Impact:** For 100k instances of 56 B each, one extra 5.6 MB copy, plus another for the ids, every tick.

**Recommendation:** Encode (part, index) in SortEntry, or keep a prefix-offset table, and gather straight from the parts into the snapshot, skipping m_instances and m_ids.

**Verification:** I did not read draw_list.cpp, so the three copies and the 5.6 MB figure are unchecked. The cost is a memcpy-scale optimisation with no stated measurement behind it. Per the project's rule, a measured profile would decide whether it matters.

### X-PERFORMANCE-27: Hover picking scans every snapshot instance each frame, unpacking halves

- **Severity:** info (reviewer said low) · **Verdict:** unverifiable · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/view.cpp:65`

App::Hovered calls PickInstance every drawn frame. It loops over all instances from the top, unpacking 4 halves per instance with UnpackHalf, whose subnormal path calls ldexp, plus rotation trig for rotated sprites. It runs even when neither the cursor nor the snapshot has changed since the last frame.

**Evidence:** `for (std::size_t i = count; i-- > 0;) { ... const f32 halfWidth = std::abs(core::UnpackHalf(...)) / 2.0F; ...`

**Impact:** About 100k iterations per frame on the main thread in the stress scene: a fraction of a millisecond to a millisecond at uncapped frame rates, for a hover outline.

**Recommendation:** Cache the result per (snapshot id, cursor position, alpha bucket). Publish per-instance AABBs in f32, or a coarse grid, with the snapshot (an extra pass in DrawList's gather), so picking tests only the cell under the cursor.

**Verification:** I did not read view.cpp or PickInstance, so the per-frame scan and UnpackHalf cost are unchecked. Only a profile of the stress scene would show whether hover picking is a measurable share of frame time.

### X-PERFORMANCE-28: Each frame repeats work that is fixed within a frame: Holds() scans the scene's sprites about 7 times, and GatherLabels resets a table the size of the strings table

- **Severity:** info · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/renderer.cpp:1331`

Holds(snapshot.scene) iterates every sprite of the scene, with a profile zone, and is called from Upload (twice), UploadTerrain, UploadGround, UploadRoute, UploadClouds and DrawWorld. GatherLabels does `measured.assign(strings->text.size(), {})` every frame (line 900) whether or not any label shows.

**Evidence:** `bool Renderer::Impl::Holds(core::SceneId scene) const { PN_PROFILE_ZONE(); return std::ranges::all_of(manifest->Scene(scene).sprites, ...` called from 7 sites

**Impact:** Minor today. The second grows with localisation (thousands of strings times 2 directions times 32 B memset per frame).

**Recommendation:** Compute Holds once per frame into a bool on Shown. Reset `measured` lazily with a frame stamp per entry, or with a small map keyed by the strings actually used.

**Verification:** renderer.cpp:1331 Holds does all_of over the scene's sprites with a profile zone; called from about 7 sites per frame (lines 460, 477, 587, 611, 674, 702, 1191). measured.assign(strings->text.size(), {}) at line 900 is per frame. Both are real but negligible today. The localisation scaling claim is speculative.

### X-PERFORMANCE-29: Over MAX_CLOUD_INSTANCES, the clouds dropped are those at the top of the view

- **Severity:** info · **Verdict:** deliberate · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/cloud_draw.cpp:429`

The laid clouds are sorted by rest.y ascending, and the first `dropped` are skipped. When the cap is hit (a wide unexplored view at zoom 0.25 on a big map), the band of clouds at the top of the screen vanishes, not an even thinning.

**Evidence:** `const std::size_t dropped = clouds.size() > MAX_CLOUD_INSTANCES ? clouds.size() - MAX_CLOUD_INSTANCES : 0; ... for (std::size_t cloud = dropped; cloud < clouds.size(); ++cloud)` after sorting by rest.y

**Impact:** Unexplored land at the top of the view shows only the flat shade, without cloud puffs: a visible seam. A sign the cap is per view and fixed, not scaled with view size.

**Recommendation:** Choose the level by view area so the count stays under the cap, or drop by hash (even thinning), or raise and derive the cap from the widest view.

**Verification:** cloud_draw.hpp:32 and :69 document 'Past MAX_CLOUD_INSTANCES the farthest back are dropped', and the back-to-front sort by rest.y makes top-of-view the farthest back. The cap is an intentional painter-order choice. The visible seam when the cap is hit is real, but the cap is 8192 and is only hit by a very wide view. A fix is optional.
