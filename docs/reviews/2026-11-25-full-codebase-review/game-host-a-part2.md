# Review: game-host-a (part 2)

### GAME-HOST-A-11: Test-only mutators on the production Host API bypass input and replays

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** api · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.hpp:251`

Host::Damage(), SetSightRadius() and Explore() change simulated, hashed and saved state from outside Tick(), and no recorded action drives them. Only tests (and reference_tests for Explore) call them. scenes/arena.cpp uses WorldContext::Damage, not Host::Damage. SetSightRadius is also the only way the radius changes after a new game, so a game can never grow its sight.

**Evidence:** grep finds Host::Damage only in tests/unit/game/{save_exactness,save,combat}_tests.cpp; SetSightRadius only in save_tests and host_tests; Explore in scenes_tests, save_tests, host_tests and reference_tests.

**Impact:** If one of these ever gets wired to a debug key or the UI, it changes the simulation without a replay record, and replays diverge. The public surface is wider than the game needs.

**Recommendation:** Move them behind a test-harness friend or a `HostTestAccess` type in tests/support, or turn them into recorded actions if the game needs them (sight radius from data or progression).

**Verification:** Host::Damage, SetSightRadius and Explore are public mutators outside Tick, and SetSightRadius asserts the radius against core::MAX_SIGHT. The claim that they are called only from tests is plausible but I did not grep every caller. The replay-divergence risk is speculative and none of them is wired to input. It is an API-hygiene suggestion.

### GAME-HOST-A-12: ClearScene's Deactivate branch can't be reached by current scenes, though the docs present it as core behaviour

- **Severity:** info · **Verdict:** confirmed · **Category:** dead-code · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:426`

Build() calls ClearScene only on m_world, and only when no scratch world runs, so only game scenes are ever cleared. Game scenes never Activate world NPCs (only arena_layout's SpawnArenaNpcs does, and it runs in dev scenes, whose scratch world is thrown away whole). The world-NPC branch (the assert, then Deactivate) therefore never runs in the game. The header promises 'the world NPCs it brought in back in the world'.

**Evidence:** grep Activate( in src: only arena_layout.cpp:137 (dev scenes). host.cpp:800-802 `if (!m_scratch) { ClearScene(m_world.state); }`.

**Impact:** The code path has no realistic test through Build and may rot until a game scene starts activating NPCs.

**Recommendation:** Keep it for future game scenes, but add a host test that activates a world NPC in a game scene (with a test scene or harness) and checks that it returns to the world domain with its cell kept. Otherwise state in the doc that it exists for future scenes.

**Verification:** Activate( is called only at arena_layout.cpp:137 (dev scenes, scratch world discarded). ClearScene's Deactivate branch (host.cpp ~426-432) is reachable only if a game scene activates NPCs. Docs/test gap is real but harmless.

### GAME-HOST-A-13: RouteFits doesn't tie a loaded route to the player's position or the clock

- **Severity:** info · **Verdict:** confirmed · **Category:** validation · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/game/host.cpp:548`

RouteFits checks the member, the part count, the WALKING profile, at least 2 tiles and LegSound. It doesn't check that the leg's origin is near the player's body, that startTick is at or before the world clock, or that consecutive tiles are neighbours. LegSound places no limit on startTick other than overflow.

**Evidence:** host.cpp:552-557; travel.cpp:176-185 LegSound.

**Impact:** A crafted save can teleport the cultivator on its next tick (body set to Where(route)), or keep it standing at the origin until a far-future startTick. That is harmless but inconsistent with how strictly the rest of the loader checks.

**Recommendation:** Optionally require `leg.startTick <= worldTicks` and an origin within a tile of the player's Body.current. Document the cases the loader deliberately doesn't check.

**Verification:** RouteFits (host.cpp:548-558) checks member, parts, WALKING and LegSound, plus worldTicks <= TRAVEL_END. It does not check the origin against the player's body, startTick against the clock, or that consecutive tiles are neighbours, though LegSound may check adjacency (not traced). Only a crafted save can trigger it, and the effect is benign.

### GAME-HOST-A-14: The game's world is kept twice for the whole run, and four times during a load

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.hpp:487`

m_base (the world as made) is kept beside m_world all session for save-time diffing. Load then builds a fresh base and world (m_loaded) while both are still in memory. The spec left 'kept in memory or regenerated at save time' to the builder, and keeping it was chosen. The shipped map has 50k NPCs plus journeys. The user has asked about maps 2x to 50x larger, and the explored bits alone reach 2 MB per copy at MAX_TILES.

**Evidence:** host.hpp:486-488 `WorldState m_world; WorldState m_base;`; Loaded holds `world` and `base` (host.hpp:557-558).

**Impact:** Memory peaks at about 4× the world during a load, and the steady state is 2×. That will matter at the larger map sizes.

**Recommendation:** Measure the world's footprint at the target map sizes. If it matters, store the base in a compact form (only what SaveDomainDelta reads), or release m_world's old base before generating the new one in Load (the old one is no longer needed once the save passes its header checks).

**Verification:** host.cpp:777-788 keeps m_base beside m_world (CopyOf), and Load holds a second pair in m_loaded. The 2x steady-state and 4x peak figures follow from the code. The effect at larger maps is unmeasured, so the finding is speculative until measured. The repo's convention is to treat unknowns as measured spikes.

### GAME-HOST-A-15: Portal(), View(), OnPortal() and OnMapPortal() are non-const queries because WorldContext needs a mutable world

- **Severity:** info · **Verdict:** confirmed · **Category:** api · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.hpp:232`

LiveContext() builds a WorldContext, whose constructor takes `WorldState&` and `core::StableId&`. So every query that hands a context to a scene hook (Portal, View, OnMapPortal) is non-const, although Scene::Portal and Scene::View take `const SceneContext&`. Load, documented as 'changing nothing', calls OnMapPortal, which also builds its context from Live(), the running world, not the world being loaded.

**Evidence:** host.cpp:723-725, 1136-1144, 1161-1167; Load line 1679 `!OnMapPortal(*world.mapPlace)` checks the loaded world's place against portals computed from Live().

**Impact:** Const-correctness is lost on Host's query API. The check during Load is right today only because the world portal depends on the game data alone, and it would silently use the wrong world if a portal ever depended on world state.

**Recommendation:** Add a const read-only context (a ConstWorldContext, or SceneContext split into read and write interfaces), make these queries const, and have Load build its context from the world it is checking.

**Verification:** OnMapPortal (host.cpp:1161) builds its context from LiveContext() (the running world). Load's check at line 1679 passes the loaded world's mapPlace through it. Portals depend only on game data today, so the result is correct, but the structure is fragile and non-const. This is an API-hygiene note only.

### GAME-HOST-A-16: The Doxygen for Tick(), Load() and Publish() narrates the implementation and is already hard to keep accurate

- **Severity:** info · **Verdict:** unverifiable · **Category:** docs · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.hpp:80`

Tick()'s comment is 30 lines and Publish()'s about 40. Each lists every step, every helper and the order they run in. Smaller doc comments name specific content that will go stale ('IsCombatScene … the arena only', 'Player … zero in a scene without one (the painted scene, the cells)', 'NextScene(): arena, stress, painted, cells, world'). The file comment (lines 5-11) is one run-on sentence. src/AGENTS.md repeats much of the same text.

**Evidence:** host.hpp:80-108, 140-157, 333-372, 237, 241.

**Impact:** Every behaviour change needs this prose edited in two or three places. Drift is likely, and reviews spend time checking prose instead of code. The AGENTS.md rule asks for Doxygen covering what a function does and promises, not how.

**Recommendation:** Cut each comment down to its contract: inputs, guarantees, invariants. Point to tests for the step-by-step behaviour, and keep scene lists in scenes.hpp only.

**Verification:** Whether the doc comments are too long or drift-prone is a style opinion. I did not count lines in host.hpp. Nothing in the repo records a rule either way, beyond the AGENTS.md instruction that Doxygen documents the API and may say what.

### GAME-HOST-A-17: ARENA_FORMAT names the whole host save format; ACTIVE_VERSION's comment is split by formatting

- **Severity:** info · **Verdict:** confirmed · **Category:** naming · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:85`

ARENA_FORMAT is the version of the whole host save (header, game world, scratch world, scene), not of the arena. ACTIVE_VERSION's value '5' sits alone on a line with its history comment after it, because clang-format wrapped them.

**Evidence:** host.cpp:85-87 `constexpr u32 ARENA_FORMAT = 10; constexpr u32 ACTIVE_VERSION =\n    5; // 2: creatures; ...`

**Impact:** It is misleading to new readers, and the save-format history is harder to scan.

**Recommendation:** Rename it HOST_SAVE_FORMAT (or SAVE_FORMAT) and move ACTIVE_VERSION's history into a comment block above the constant, as WORLD_VERSION has.

**Verification:** host.cpp:85-87 matches. ARENA_FORMAT is written first in the host archive and hash (lines 1611, 1625) and checked in load (1646), so it versions the whole host save. The comment at 78-84 says 'the host's part of a save: its format'. ACTIVE_VERSION's value is wrapped onto its own line with the history after it. Naming and formatting nit only.

### GAME-HOST-A-18: FactsOf() repeats the manifest's scene lookup by name on every Current() call although m_sceneId already holds it

- **Severity:** info · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:708`

Current() calls FactsOf(m_scene), which runs SceneIdOf, a linear string search over the manifest's scenes, every time, although Build and EnterScene already store the result in m_sceneId.

**Evidence:** host.hpp:191-193; host.cpp:708-714, 560-564.

**Impact:** The cost is negligible today (a few scenes, called at scene changes), but it duplicates m_sceneId, which could get out of sync with it.

**Recommendation:** Have Current() return `{ m_scene, m_sceneId, traits.combat }` directly. Keep FactsOf for Requested().

**Verification:** host.hpp:192 Current() calls FactsOf(m_scene). FactsOf (host.cpp:708-714) calls SceneIdOf, a manifest lookup (defined at 560), and m_sceneId (hpp:497, set at 754, 780, 806) already holds that value. The duplication is real, but it is not a performance problem. Current() could return {m_scene, m_sceneId, combat} directly. Nit only.
