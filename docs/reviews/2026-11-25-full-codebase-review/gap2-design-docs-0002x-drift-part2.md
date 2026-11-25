# Review: gap2-design-docs-0002x-drift (part 2)

### GAP2-DESIGN-DOCS-0002X-DRIFT-11: 0002.3's shared_ptr<GameData> in every Action is still in the code

- **Severity:** info · **Verdict:** confirmed · **Category:** design · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `src/core/frame.hpp:520`

The Action struct still carries `std::shared_ptr<const GameData> data` beside target, scene, slot, point and change, as 0002.3 §3 proposed. Every queued Action copies a control block pointer slot it only uses for ReloadData. The design-docs unit already raised this on 0002.3; I confirm the layout unchanged and that Action has since grown four more fields (scene, slot, point, change) that the doc does not list.

**Impact:** Action is 48+ bytes with a refcounted pointer on a hot input path; copies of high-volume actions touch an atomic only when non-null, so the cost is small.

**Recommendation:** Carry reload data out of band, or split Action into a variant. Update 0002.3 §3's field list either way.

**Verification:** src/core/frame.hpp:520 still has shared_ptr<const GameData> in Action. It is a duplicate of the design-docs unit's finding, and the review brief said not to repeat that one. Doc field list staleness not re-checked.

### GAP2-DESIGN-DOCS-0002X-DRIFT-12: Scene lists in the docs stop at four scenes; the manifest and F2 cycle now include `world`

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002.3.1-manifest-and-scene-loading.md`

0002.3.1 §2 shows scenes.toml with [arena] [stress] [painted] [cells] and decision 5 in 0002.3 §12 says F2 cycles arena, stress, painted, cells. art/scenes.toml has a fifth table, [world] (line 44). The doc's note that the dev scenes are the F2 cycle and `world` is a game scene is in src/AGENTS.md only (devScene traits). Likewise 0002.3.1 §3's 'game::SceneNamed(string) -> optional<game::Scene>' is now optional<core::SceneKey>.

**Impact:** Cosmetic, but the doc's explicit F2 order is wrong.

**Recommendation:** Add an as-built line listing world and noting dev scenes versus game scenes.

**Verification:** art/scenes.toml does have a [world] table at line 44 beyond arena, stress, painted and cells. I did not check the F2 cycle or the SceneNamed return type.

### GAP2-DESIGN-DOCS-0002X-DRIFT-13: 0002.4 text API and settings claims: hot reload and settings now broader than documented

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/design/0002.4.2-text-and-mesh-path.md`

§5 says debug-build hot reload stays creatures-only, and 0002.3 §7 says the reload sends only game data. src/app/app.cpp:435-480 reloads effects, seals, animations, world look and game data through a DataWatch over the whole folder. Also 0002.4.2 §5 says strings are loaded once and handed to the renderer; they are passed to the hud (app.cpp:247).

**Impact:** Hot reload of seals and animations may change replay behaviour in ways the 0002.3 §8 determinism text does not cover (it covers GameData only).

**Recommendation:** Document which tables are hot-reloadable and which are presentation-only (so replay-safe) in 0002.3 §7.

**Verification:** app.cpp WatchData (~line 431) is a DataWatch over the data folder, debug-only (HOT_RELOAD). I did not verify the claim that it reloads effects, seals and animations, or the hud strings claim.

### GAP2-DESIGN-DOCS-0002X-DRIFT-14: MODULES in tools/project_n.py lists ui and audio modules that do not exist

- **Severity:** info · **Verdict:** confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:1236`

The module-direction check has entries for `ui` and `audio` and lets `app` depend on them, but src/ has no ui or audio directory. 0002 A1 and 0002.4 §7 describe `ui` as a module in the build. This is not wrong (the table is forward-looking), but nothing marks it as such, and the repo has no commit scope or test guarding a typo that creates src/ui without xmake targets.

**Impact:** Negligible. It records the intended layering for an unbuilt module, while the docs say nothing about it being unbuilt.

**Recommendation:** Add a one-line comment that ui and audio are reserved, as 0002 A1 plans.

**Verification:** tools/project_n.py:1236-1237 lists ui and audio. src/ has no ui or audio directory (app, core, data, game, gfx, platform only). It is a forward-looking table, harmless.

### GAP2-DESIGN-DOCS-0002X-DRIFT-15: 0002.3.1 §5: renderer comment on single-slot scene loading and ReportFindings of 'uploads start once all decodes are done' diverge slightly from the optimisation the code made

- **Severity:** info · **Verdict:** confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** low
- **Where:** `docs/design/0002.3.1-manifest-and-scene-loading.md`

§8 documents 'uploads start once every decode is done'. The code matches (ContinueScene returns false until decoding.Done()). However the same ContinueScene also now accumulates distance-field work (DecodedSprite::fieldNs) computed in the decode jobs and logs it, which the doc never mentions: the decode step does more than 'decode and premultiply'. The 0002.3.1 note dated 2026-11-12 covers only the gutter and the distance-field array.

**Impact:** Decode cost per scene is higher than the doc's model, which matters to the 'window never freezes' and 'no decode timeout' known items.

**Recommendation:** Add one sentence on the distance-field computation to §5.

**Verification:** DecodedSprite::fieldNs is set in sprite_array.cpp:159 and summed into load.transformNs at renderer.cpp:1527. The decode job therefore also computes distance fields. Whether the doc omits this was not checked.
