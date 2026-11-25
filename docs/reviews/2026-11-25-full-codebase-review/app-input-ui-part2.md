# Review: app-input-ui (part 2)

### APP-INPUT-UI-11: Cast dropped during dash

- **Severity:** low · **Verdict:** deliberate · **Category:** gameplay · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `src/app/cast.hpp:22`

Every press is its own Cast action with no buffering or repeat while held. 'A held button casts once' (cast_tests.cpp:93): a press during a cooldown, a dash or a refusal is lost, with no input buffering.

**Evidence:** cast.hpp header comment; the history lists 'cast dropped during dash' as deferred.

**Impact:** Bullet-heavy action combat feels unresponsive without a short input buffer or hold-to-repeat.

**Recommendation:** The sim decides what a press does, so add a small input-buffer window and optional hold-to-cast (an accessibility setting) in the sim/bindings design.

**Verification:** cast.hpp states every press is its own Cast, never repeated or debounced here (deliberate: the sim decides). The dash-drop case is in sweep-up-backlog.md line 32 ('Cast queued behind the dash', user 2026-11-12) as a deferred idea. Hold-to-cast as an accessibility setting is new suggestion, not recorded.

### APP-INPUT-UI-12: HUD layout assumptions: window coordinates equal target pixels, fixed separators, positional slot index

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/app/hud.cpp:155`

The date-line click compares window coordinates with an area in 'target pixels' and relies on the window not being high-DPI (app.cpp comment). DIGIT_KEYS.at(index - Special1) depends on the Slot enum order, and SEPARATOR is a hard-coded two spaces for all languages. Bars use layout.margin offsets from the bottom with no clamp for very short windows.

**Evidence:** hud.cpp BuildDate area in target px; app.cpp 'The window isn't made high-density'; DIGIT_KEYS.at(...).

**Impact:** If the window ever becomes high-DPI, scaled, or the target resolution differs from the window's, the date-line click lands in the wrong place. A reorder of core::Slot would throw from .at() at runtime. Tiny windows push the bars off-screen.

**Recommendation:** Convert the pointer to target pixels in one place (a shared helper), index digits through an explicit slot-to-key table, and clamp or assert the HUD's minimum height.

**Verification:** DIGIT_KEYS.at(index - Special1) exists at hud.cpp:155 and would throw if Slot order changed (std::array of 5, so a slot reorder is a real coupling). app.cpp:867 comments that the window isn't high-density, so window coords equal pixels is a stated assumption, not a bug today. Bars offset from the bottom without clamp is plausible (hud.cpp ~126-130) but harmless. Latent design nit.

### APP-INPUT-UI-13: Tests do not cover input lifecycle holes or scale

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/app/bindings_tests.cpp:242`

Bindings tests cover dead zone, diagonal and combined keys well. None checks that held keys or stick state clear on focus loss or a device change, or that DeviceInUse recovers when a pad disappears. Fog tests use a 5x5 map only, and nothing asserts a bound on SetTargets work. Damage-number tests lack a many-uses-ending-in-one-tick case. Target-cursor tests lack the jitter and diagonal-release cases.

**Evidence:** TEST_CASE lists reviewed for bindings_tests, fog_fade_tests, damage_numbers_tests and target_cursor_tests.

**Impact:** The defects above would pass the current suite.

**Recommendation:** Add the cases alongside each fix; keep to ~20 samples per test (small-unit-tests rule) and use fuzz or measure for breadth.

**Verification:** Reasonable given the backlog items: item 5 explicitly says no test covers focus-loss pan, so such defects would pass. I did not read every test file to confirm each listed gap (fog 5x5, damage-number, target-cursor cases), so those are unverified. Meta-finding, low.

### APP-INPUT-UI-14: Combined damage numbers wait unbounded for a UseEnded that may never come

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/app/damage_numbers.hpp:128`

Waiting numbers are released only by UseEnded or Clear(). The header states every use emits one. I confirmed emitters in combat.cpp, host.cpp and arena.cpp, but a hit emitted after its use's UseEnded, or an event dropped on a replay seek, would leave a number waiting (and growing) forever. Live() reports it as live; nothing is drawn-expired.

**Evidence:** Texts() draws m_waiting at held pose every frame with no age-out; Clear() logs a debug count only.

**Impact:** Visible stuck numbers over a target and a slow leak, if the sim ever breaks its own invariant. It is a self-diagnosing invariant currently.

**Recommendation:** Add a debug assert or warning for a waiting number older than some number of seconds, and a sim-side test that every hit's use ends after it.

**Verification:** Waiting numbers are released only by UseEnded or Clear(), as stated. It is a self-diagnosing sim invariant, and the finding itself concedes emitters exist. A debug assert or warning would be harmless, but no concrete failing scenario was shown. Info is right.

### APP-INPUT-UI-15: Camera path pan displacement scales with the current zoom, not the segment's start

- **Severity:** info · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/app/camera_control.cpp:236`

For PathCentre::Pans the centre is m_segmentFrom + pan * ScreenOf(m_camera) * through, with ScreenOf evaluated at the current per-frame zoom. For a segment whose zoom changes, the displacement per second is not 'even' as documented ('at an even speed, in screens'). Today the only zooming Pans segment has pan = 0.

**Evidence:** m_segmentFrom + (segment.pan * (ScreenOf(m_camera, input) * through)) after m_camera.zoom = SegmentZoom(...).

**Impact:** A latent trap: a future path segment with both pan and zoom would move non-linearly and at a different speed from what its comment and the 'stay within CULL_MARGIN' note claim.

**Recommendation:** Use the zoom at the segment start for the pan, or assert that a Pans segment with non-zero pan has constant zoom, and test it.

**Verification:** Cited line 730 is wrong; the code is at camera_control.cpp:235-236. m_camera.zoom is set by SegmentZoom before ScreenOf(m_camera, input) is evaluated, so displacement uses the current zoom and is non-linear for a zooming Pans segment, contradicting 'even speed' in camera_path.hpp:35. The finding itself says today's zooming segment has pan 0; I did not check every segment in camera_path.cpp. Latent only.

### APP-INPUT-UI-16: Zoom is centred on the screen, not the cursor

- **Severity:** info · **Verdict:** confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/app/camera_control.cpp:667`

Wheel zoom only changes camera.zoom; the centre is unchanged, so the world point under the pointer drifts while zooming. Many map-heavy games zoom toward the pointer.

**Evidence:** m_camera.zoom = gfx::ZoomedBy(m_camera.zoom, notch) with no centre compensation.

**Impact:** A usability weakness on a very large procedural world map: finding a place by zooming needs follow-up pans.

**Recommendation:** Consider zoom-to-cursor (keep the world point under the pointer fixed) as an option in the settings screen.

**Verification:** Wheel zoom changes only m_camera.zoom (ZoomedBy); centre follows the followed creature or stays, no cursor compensation. It's a design opinion, not a defect, and I found no recorded decision on it. Info.
