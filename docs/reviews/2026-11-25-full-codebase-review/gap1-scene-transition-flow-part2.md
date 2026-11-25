# Review: gap1-scene-transition-flow (part 2)

### GAP1-SCENE-TRANSITION-FLOW-11: A camera drag in progress carries across a scene change and frees the new scene's camera at once

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/app.cpp:760`

CameraControl resets on a new ShownChange, but App's Drag (m_drag) does not: m_from survives the change. A middle or right drag held while the scene changes, which is easy with the right button on the world map, keeps feeding dragFrom into the new scene's camera. In a Roam scene, any pan or drag sets m_free on the very first frames of the new scene.

**Evidence:** app.cpp:760 `m_drag.Take(...)`; only paused frames call m_drag.End() (app.cpp:565-567). CameraControl::Update resets its own state only (camera_control.cpp:152-160).

**Impact:** Entering the world map mid-drag leaves the camera detached from the cultivator until C or a middle click.

**Recommendation:** End the drag when the shown change moves on, in the same presentation reset.

**Verification:** m_drag.End() is called only on paused frames (app.cpp:565-567), and CameraControl resets only its own state, so a drag does continue across a scene change. But the user is still physically holding the button and moving the mouse. A drag that keeps panning is arguably the correct reading of that gesture, not a leak. It needs a drag held during a portal step, which is rare. The camera frees only if the pointer actually moves. I rate it info.

### GAP1-SCENE-TRANSITION-FLOW-12: Presentation state keyed by StableId assumes IDs are unique across scenes, but world-range IDs repeat between the game world and every scratch world

- **Severity:** info · **Verdict:** confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/gfx/labels.hpp:262`

LabelPass::m_seen, barsShown, number origins and damage-number targets are keyed by core::StableId across frames, and therefore across a scene change. Scene-range IDs are never reused. World-range IDs, however, start from 1 in the game's world and again in each dev scene's scratch world (`m_scratch.emplace(core::State{ DOMAINS, SEED })`), so arena NPC 5 and world traveller 5 share an ID.

Today no scratch-world NPC publishes a label, number or health bar: the arena labels only the cultivator and foxes, which use scene IDs. So nothing breaks yet. Once arena NPCs get labels, a label's opacity, stack and glide would continue from an unrelated NPC across arena↔world.

**Evidence:** host.cpp:817-819 creates the scratch State fresh. labels.hpp:262 `std::unordered_map<core::StableId, Seen> m_seen;`. host.cpp:1200 labels only the player and foxes.

**Impact:** Latent: wrong label fades or stacks across scene changes once dev-scene NPCs are labelled.

**Recommendation:** Reset per-entity presentation caches at the shown-change reset (see the architecture finding), or document that presentation keys are valid only within one shown change.

**Verification:** state.hpp:115-118 allocates World-range IDs from m_ids and scene-range IDs from m_sceneIds with a separate base. host.cpp:817-819 builds a fresh scratch State, so its world IDs restart. The author's own evidence admits that nothing is labelled from scratch-world NPCs today, so the problem is latent. I did not verify the labels.hpp:262 key type beyond the finding's quote. Info severity is right.
