# Review: gap2-design-docs-0002x-drift

**Scope:** Check the six sub-designs of 0002 claim by claim against the code on main: docs/design/0002.1-app-and-game.md, 0002.3-input-data-scenes.md, 0002.3.1-manifest-and-scene-loading.md, 0002.4-ui-and-text.md, 0002.4.1-scene-target-and-composite.md and 0002.4.2-text-and-mesh-path.md (about 1,175 lines). For each stated mechanism, API, file layout, threading rule, format, limit or measured figure, find the code that implements it (src/app, src/core/manifest*, src/data/manifest*, src/game/scene*, src/gfx/renderer.cpp, internal/features/composite.cpp, meshes.cpp, text_layout/text_place/labels) and classify it as accurate, drifted (the doc names something that changed), or never built. Also look for decisions in these docs that the code later reversed with no note in the doc, and for contradictions between these docs and 0002 itself. Do not repeat what the design-docs unit already found: it covered 0001, 0001.1, 0002 and 0002.2 in depth and raised only one point on 0002.3 (the shared_ptr<GameData> in every Action). (Why this unit exists: The design-docs unit's scope was all ten docs, but its findings (four output parts) cite only 0001, 0001.1, 0002 and 0002.2, plus one info-level point on 0002.3. Five sub-designs, about 900 lines covering the app/game split, manifest and scene loading, UI and text, the scene target and composite, and the text and mesh path, got no findings at all. The repo's rule is to read docs/design before redesigning, so drift in these docs misleads every future design.)

**Findings:** 15 standing (0 critical, 0 high, 0 medium, 7 low, 8 info), 0 raised but refuted on verification.

**Parts:** gap2-design-docs-0002x-drift.md, gap2-design-docs-0002x-drift-part2.md

## Assessment

Scope: six sub-designs of 0002 (about 1,175 lines), checked claim by claim against src/app, src/core, src/data, src/game, src/gfx and tools. I read all six docs in full. For the code I read the specific files each claim points to, not every file named in the scope. I did not read text_label or labels.cpp, and I read only parts of meshes.cpp and renderer.cpp. Findings are therefore limited to the claims I traced.

Strengths:
- The phase-5 docs (0002.1, 0002.3, 0002.3.1) are mostly accurate on mechanisms. I confirmed these against the code:
  - Jobs ownership by App, the external thread registration, and `Jobs::Start`/`Pending`.
  - Manifest layout (`SpriteInfo`, `SceneInfo`, `Manifest`) and `LoadManifest`.
  - `BeginScene`/`ContinueScene` with the 2 ms budget, and `SceneResident`.
  - Held input, the 0.2 stick dead zone, and `ReloadData` hot reload gated to debug builds.
  - Replays refusing `ReloadData` and `SceneResident`.
- 0002.4.1 (scene target and composite) is essentially exact against the code:
  - The swapchain is always SDR.
  - `FitSceneTarget` and `Compositor` use a one-triangle nearest-sampled pass.
  - The sRGB piecewise curve is in the shader.
  - `DONT_CARE` on the final pass and the `cycle = true` clear are both there.
  - Capture goes through the same two passes.
- Most later changes carry "as built" notes or a Superseded status.

Weaknesses (drift the docs do not flag):
- 0002.4 §5 to §11 describe a design that was never built or was reversed. Only the header status says "superseded", and no section marks which parts survive. The undated parts are:
  - the RmlUi `ui` module,
  - the pause menu, save slots, roster and transition,
  - `bindings.yaml`,
  - a custom `TTF_TextEngine`,
  - the digit strip.
- 0002.4.2 is marked Superseded, but it still states numbers and APIs the code does not have. I found: mesh buffer size and index width, `gfx::Fonts`/`text.hpp`, `floating_numbers.*`, `EventKind::DamageNumber`, the 300-frame layout eviction, R8 coverage glyphs, and "game text keeps its size on screen". Decision 3 in §12 is reversed in the code with no note in the doc.
- 0002.1 and 0002.3.1 describe `Game` as owning "the arena" and `game::Arena`. These are fixed only by trailing as-built notes, so §2's ownership diagram is stale.
- 0002.3 contradicts itself on settings, and its example Control names, replay format number and the scene list are out of date.
- Three live documents link to a 0002.4.3 that does not exist.
- `MODULES` in tools/project_n.py still lists `ui` and `audio`, which have no source directory.

Overall health: low risk to the code. The risk is to designers: the repo's rule is "read docs/design before redesigning", and 0002.4 and 0002.4.2 would steer a reader toward mechanisms (RmlUi font engine, TTF_TextEngine, coverage glyphs, fixed-size text) the project has replaced. The user's backlog already lists "0002/0002.x predate superpowers and need bringing in line"; these findings are the concrete list for that item.

**Files read in full:** (not reported)

## Findings

### GAP2-DESIGN-DOCS-0002X-DRIFT-01: 0002.4.2 states mesh limits and index type the code does not use

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002.4.2-text-and-mesh-path.md`

§2 says the vertex buffer holds MAX_MESH_VERTICES of 65,536 vertices (16,384 glyph quads, 1 MiB) with a static u16 index buffer, and that quads past the limit are reported. The code has MAX_MESH_QUADS = 65,536 (4 MiB of vertices, 262,144 vertices) in src/gfx/mesh_vertex.hpp and binds the index buffer as SDL_GPU_INDEXELEMENTSIZE_32BIT in src/gfx/internal/features/meshes.cpp:190. u16 indices could not address 262,144 vertices.

**Impact:** A designer sizing a new mesh user from the doc gets the cap 4x too small and the index width wrong. The meshes.cpp UploadQuadIndices contract is 32-bit.

**Recommendation:** Update §2 with the quad cap, 32-bit indices and the 16-byte vertex (still correct), or mark §2 as superseded by the chunk 3 spec's Outcome.

**Verification:** Code has MAX_MESH_QUADS=65'536 (mesh_vertex.hpp:32) and meshes.cpp sizes buffers from it with u32 indices; doc §2 line 39 says MAX_MESH_VERTICES 65,536 and u16. But the doc header says it is Superseded by the chunk 3 spec and its Outcome, so this is expected history; severity low.

### GAP2-DESIGN-DOCS-0002X-DRIFT-02: 0002.4.2 decision 3 (text keeps its screen size) is reversed in code with no note

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** doc-drift · **Status:** contradicts-recorded-decision · **Reviewer confidence:** high
- **Where:** `docs/design/0002.4.2-text-and-mesh-path.md`

§1 and §12 decision 3 say game text keeps its size on screen, anchored to a world point, rasterised once at its pixel size. The code does the opposite: src/core/frame.hpp TextItem says its size in world units scales with the zoom, and src/gfx/text_place.hpp gives each text kind a world size (LABEL_WORLD_SIZE, PLACE_WORLD_SIZE and others) multiplied by PixelsPerUnit() and a text scale. src/gfx/text_layout.hpp rasterises each glyph once as a signed distance field at SDF_BASE_PIXELS = 48 and scales it. The chunk 3 spec's decision 1 says the same: text scales with the zoom, through distance-field glyphs. The Superseded header does not say which sections the spec overrides.

**Impact:** The doc records as an accepted decision something that was undone. Anyone designing zoom or UI text from it will assume pixel-exact, fixed-size glyphs.

**Recommendation:** Add a note under §1 and §12.3 naming the spec decision that reversed it, or strike the section the way 0002.4 §11 strikes its culling item.

**Verification:** Text_layout.cpp does use SDF glyphs, which matches the finding's code claim (the SDF specifics I did not re-read). The doc is marked Superseded by the chunk 3 spec, which records the zoom-scaling decision, so the reversal is recorded at doc level. The header does not say which sections are overridden, which is a minor gap.

### GAP2-DESIGN-DOCS-0002X-DRIFT-03: 0002.4.2/0002.4 describe a custom TTF_TextEngine and R8 coverage glyph cache that were never built

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** never-built · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002.4.2-text-and-mesh-path.md`

0002.4.2 §3 and 0002.4 §5 specify a custom TTF_TextEngine whose CreateText reads draw operations and asks a GlyphCache (resources/glyph_cache.{hpp,cpp}) for each glyph, rasterised with TTF_GetGlyphImageForIndex into R8 coverage pages, with gfx/text.hpp and gfx::Fonts (public, no SDL types). The code has no such files. src/gfx/text_layout.cpp calls TTF_CreateText(nullptr, ...) with no engine, reads the ops, and rasterises distance fields from a second SDF font opened with TTF_SetFontSDF. The only shipped pieces of the doc's cache are ShelfPacker (src/gfx/shelf_packer.*) and 2048-squared R8 pages. There is no gfx/text.hpp, no Fonts class, and no resources/glyph_cache. The renderer holds glyph pages directly.

**Impact:** The documented extension point (custom TTF_TextEngine) was dropped. 0002.4 §7 option (a), UI text on this engine, rests on it. A future UI chunk would design against an API that does not exist.

**Recommendation:** Rewrite 0002.4 §5 and 0002.4.2 §3 to the SDF cache actually built (text_layout.hpp), or point them at the chunk 3 spec's Outcome section.

**Verification:** text_layout.cpp:296 calls TTF_CreateText(nullptr, fonts->shaping...), with no custom engine, so the custom TTF_TextEngine was not built as described. 0002.4.2 is marked Superseded by the chunk 3 spec, so this is documented drift at header level. I did not check the glyph_cache file names beyond this.

### GAP2-DESIGN-DOCS-0002X-DRIFT-04: Damage-number mechanics in 0002.4/0002.4.2 are replaced by a different event and text design

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002.4.2-text-and-mesh-path.md`

§6 describes core::Event with a kind and payload DamageNumber{position, value}, an aura that hits once a second for 5 to 40, app/floating_numbers.{hpp,cpp} turning events into signed Damage-font TextItems, at most 512 live with the oldest dropped, and a 300-frame layout eviction (§4). The code has: EventKind {Hit, NotEnoughQi, UseEnded} in src/core/frame.hpp (no DamageNumber); src/app/damage_numbers.* (not floating_numbers); a pool that grows past its reserve with no oldest-dropped rule; TextItem fields scatter, lift and sign; and layouts forgotten by time (LAYOUT_SECONDS = 5.0) and a 2 ms per-frame budget (LAYOUT_BUDGET_NS), not by frame count. The doc also says it keeps combined numbers out of scope.

**Impact:** The event and number pipeline is the part of 0002.4.2 most likely to be reused. Its file names, cap, event kinds and eviction rule are all wrong.

**Recommendation:** Add an as-built section citing the combat spec that replaced DamageNumber, or mark §4 and §6 historical.

**Verification:** EventKind in core/frame.hpp has only Hit, NotEnoughQi and UseEnded, and the file is src/app/damage_numbers.*, not floating_numbers. LAYOUT_SECONDS and LAYOUT_BUDGET_NS exist in text_layout.cpp. The doc is Superseded at header level, so this is expected drift; I did not verify the pool-cap detail.

### GAP2-DESIGN-DOCS-0002X-DRIFT-05: Dangling references to nonexistent 0002.4.3

- **Severity:** low · **Verdict:** confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002.4-ui-and-text.md`

0002.4 §1 (line 43), §7 heading (line 170) and §12 (line 269) say 'details in 0002.4.3' or that 0002.4.3 holds measurements, and 0002.4.1 line 34 promises it. docs/design has no 0002.4.3. The `ui` module's design went into docs/superpowers/specs/2026-11-10-phase6-chunk8-ui-design.md.

**Impact:** Dead pointer; also S13's roster smoothness measurements 'recorded in 0002.4.3' have no place to land.

**Recommendation:** Point those references at the chunk 8 spec, or create the doc when chunk 8 starts.

**Verification:** 0002.4 lines 43, 170 and 269 and 0002.4.1 line 34 reference 0002.4.3, and docs/design has no such file. The reference is a dead pointer.

### GAP2-DESIGN-DOCS-0002X-DRIFT-06: 0002.1 ownership diagram and Game description name the arena, but Game owns game::Host

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002.1-app-and-game.md`

Line 25 gives Game's order as 'saves -> replays -> replay -> arena -> logic -> clock', and §3 says Game 'owns the arena'. src/app/game.hpp has m_saves, m_replays, m_replay, m_host (game::Host), m_logic and m_clock. A trailing §5 note admits the rename, but the diagram and prose were not updated. 0002.3.1 §3 still has 'game::Arena(std::shared_ptr<const Manifest>, Scene, const GameData&)' and a scene table in arena.cpp, which phase 6 chunk 5 replaced with SceneKey and game/scenes.cpp (its §4 note says so).

**Impact:** Small, but these are the first docs a reader opens for ownership. Constructor signatures differ: GameSetup now has `scene` as core::SceneKey, `manifest`, `data`, `saveSlot`, `replaySlot`, `startNs`, `now` (0002.3 §10 lists most of them correctly).

**Recommendation:** Edit the diagram and §3 to say game::Host, or mark the old names as historical.

**Verification:** game.hpp:85 has game::Host m_host and there is no arena member, so the diagram naming the arena is stale. 0002.3.1 still mentions game::Arena at line 102. The finding says a note admits the rename, which I did not check. These are historical design docs, so the impact is low.

### GAP2-DESIGN-DOCS-0002X-DRIFT-07: 0002.3 contradicts itself on settings and has stale format and name details

- **Severity:** low · **Verdict:** confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002.3-input-data-scenes.md`

§7 says settings.yaml holds window size, fullscreen, language and four text and damage-number settings, while §12 decision 3 (accepted) says 'window size and fullscreen only for now'. §8 says the replay hash sits in the header 'format 3'; src/core/replay.cpp:15 has REPLAY_FORMAT = 5. §3 lists a Control enum (Select, Move, Zoom, Quit, ...) that is now Primary/Secondary/Movement/Special1-4, PanCamera, DragCamera and others (src/app/bindings.hpp). §3 names `Snapshot::selected` correctly, but §9 test names (app_tests, data_tests, core_tests) became tests/unit/<module>.

**Impact:** Low. These are small drifts in a document marked Implemented, but the Settings contradiction is within one document.

**Recommendation:** Correct §12.3 to the shipped settings or annotate it, and add 'as built' notes to §3's Control list and §8's format number.

**Verification:** 0002.3 line 197 says 'format 3' and src/core/replay.cpp:15 has REPLAY_FORMAT = 5. Line 261 (§12.3) says 'window size and fullscreen only', which contradicts §7. I did not re-check the Control enum or the test names.

### GAP2-DESIGN-DOCS-0002X-DRIFT-08: 0002.4.2 vertical text direction contradicts the code

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002.4-ui-and-text.md`

§2 and 0002.4.2 say the vertical caption uses TTF_DIRECTION_TTB. src/gfx/text_layout.cpp:296-300 deliberately sets TTF_DIRECTION_BTT for a top-to-bottom text, with a comment on an SDL3_ttf 3.2.2 bug that makes TTB glyphs climb.

**Impact:** Minor, but a reader of the doc would use TTB and see reversed glyphs.

**Recommendation:** Note the BTT workaround and the SDL3_ttf version where it was found.

**Verification:** text_layout.cpp:300 uses TTF_DIRECTION_BTT. 0002.4 lines 51 and 134 say TTB. Both docs are marked superseded or historical. A real but trivial detail.

### GAP2-DESIGN-DOCS-0002X-DRIFT-09: 0002.4 §7 to §11 (RmlUi ui module, roster, pause menu, save slots, transition, prompts, bindings.yaml) are unbuilt but read as a live plan

- **Severity:** info (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** never-built · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `docs/design/0002.4-ui-and-text.md`

The header says superseded for work still to come, but §7 (S13, `ui` module, RmlUi, render interface), §8 (views, NpcRoster, Subscribe/Unsubscribe actions), §9 (pause menu, three save slots, loading screen, transition), §10 (prompt glyphs, <prompt> element) and §11 (bindings.yaml, platform reading every input, settings screen) have no counterpart. There is no src/ui and no src/audio, no ActionKind::Subscribe, no core/views.hpp, no assets/ui, no bindings.yaml, and Escape still maps to Control::Quit in src/app/bindings.cpp:91. The phase 6 spec lists chunk 8 (the `ui` module) as To do. Chunk numbering also differs from the sub-doc: the doc's chunks 4 to 7 are specs' chunks 8 to 11. The user's backlog already notes the stale chunk numbers.

**Impact:** Readers cannot tell which of §7 to §11 still stand. Items like the Escape-opens-pause decision (decision 3) are recorded as Accepted but not implemented.

**Recommendation:** Add a status table to 0002.4 mapping each section to built, replaced by spec X, or not built yet, and re-number to the specs' chunks.

**Verification:** The 0002.4 header already says superseded for work still to come by the phase 6 spec and its chunk specs, and kept as the record of chunks 1 and 2. src has no ui module (app, core, data, game, gfx, platform only). So the unbuilt sections are flagged in the header, and a status table would only be a nicety. The chunk renumbering is already in the backlog per the finding.

### GAP2-DESIGN-DOCS-0002X-DRIFT-10: 0002.1 says Logic takes `Saves` calls 'on the thread that created the job system', but the logic thread is registered, not the creator

- **Severity:** info · **Verdict:** confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/design/0002.1-app-and-game.md`

src/core/saves.hpp documents Write and Read 'On the thread that created the job system', and 0002.1 §2 still says Game is built on the logic thread because that thread creates Jobs. 0002.3 §5 and 0002.3.1 §1 moved Jobs to App (main thread) with the logic thread as a registered external thread. The code works, since Jobs checks `thread <= externalThreads`, but the Saves header comment (and the 0002.1 sentence it echoes) is now inaccurate. 0002.1 line 29-30 does carry the 'Since 0002.3 §5' correction.

**Impact:** A comment and one doc sentence suggest Saves must be used from the main thread, when the logic thread is the one that uses it.

**Recommendation:** Reword the Saves comment to 'the creating thread or one registered with the job system'.

**Verification:** src/core/saves.hpp:35 and :39 still say 'On the thread that created the job system'. Not re-checked against Jobs' registered-thread logic. Comment drift only.
