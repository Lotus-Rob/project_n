# Review: app-input-ui

**Scope:** src/app/bindings.*, camera_control.*, camera_path.*, cast.*, target_cursor.*, click.*, hud.*, damage_numbers.*, fog_fade.*. Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 16 standing (0 critical, 0 high, 1 medium, 12 low, 3 info), 0 raised but refuted on verification.

**Parts:** app-input-ui.md, app-input-ui-part2.md

## Assessment

## Assessment of app-input-ui

The scope is nine small, mostly pure modules: `bindings`, `camera_control`, `camera_path`, `cast`, `target_cursor`, `click`, `hud`, `damage_numbers` and `fog_fade`. I read every source file in full, plus `app.cpp`'s call sites and the SDL event translation in `src/platform/window.cpp`. I read `tests/unit/app/fog_fade_tests.cpp` and `target_cursor_tests.cpp` in part, and judged the rest of the tests by their TEST_CASE titles, which I listed for all nine suites.

**Strengths**
- The code is well factored. Logic is pure and window-free, every function is small, and the doc comments are precise about edge cases (inclusive and exclusive rect edges, the dead-zone rescale, the one-notch-a-frame zoom rule, edge-pan arming, the click-versus-drag slop).
- The camera and drag state machines are carefully built:
  - Focus loss, a minimised window and the pointer leaving the window are all handled.
  - A calendar step's "still" frames hold the camera.
  - A scene change resets state.
- The damage-number scatter is a hash of the hit, so it replays identically. It never drops a number, and sums saturate at the `i64` ends instead of wrapping.
- The fog fade snaps on epoch, scene change and debug-sight switch, and `FindMovers` keeps easing proportional to the tiles still moving.
- Test titles cover the documented behaviours well. Those suites are the target cursor (edge, repeat, hitch and release-awaiting), camera control (about 40 cases), HUD layout and colours, damage numbers (determinism, replay, saturation), and the fog fade (snap cases, recompute only on version change).

**Weaknesses and risks**
1. **Input state can go stale** when the OS never sends the neutral or release event.
   - SDL does not send zero axis events when a gamepad is unplugged.
   - `Bindings` and `DeviceInUse` have no reset API, and `App::HandleInput` calls `LoseFocus()` but never clears the bindings.
   - The result is a character that walks forever after a pad is unplugged mid-push, and held pad buttons that can stick the same way.
2. **Fog fade scales badly.**
   - On every player step, `FogFade::SetTargets` reassigns and refills `2 × tiles` floats and `FindMovers` scans them all.
   - Memory is 4 floats plus 2 bytes per tile (current and target are `f32`, two channels).
   - The fix is cheap: diff the old and new sight diamonds, and touch only the changed tiles and the newly explored ones.
   - `MAP_COLUMNS` is 64 today, but the project's stated target is procedural maps that may be very large.
3. **The HUD is not device-aware.**
   - `Hud` hard-codes the strings "Left click", "Right click" and "Space" for its prompts.
   - `DeviceInUse` documents that it drives what prompts show, but `Hud` never reads it.
   - For a gamepad-first player this is both an accessibility problem and a gap against the stated "accessibility first" priority.
   - It will also need rework when rebinding lands, because the strings are tied to the default table.
4. **Quadratic work in `DamageNumbers`.** `Play(UseEnded)` rebuilds `m_waitingOf` over every waiting number even when nothing matched, so many uses ending in a busy combat tick cost O(uses × waiting).
5. **Target cursor with stick jitter.** `TargetCursor::Steer` has no hysteresis, so a stick jittering around the 0.5 threshold produces a step on every crossing, with no repeat limit. That affects tremor users, and the test only covers a single clean threshold crossing.
6. **Edge cases.**
   - `TargetCursor` can divide by zero if shown with 0 columns, and `Confirm` does not check the map against the cursor's stored dimensions.
   - A refused press still holds a `CastLatency` press slot, which can misattribute the next volley's latency.
   - The camera path's pan displacement depends on the current zoom at each frame.

**Tests.** They are good at documented behaviours but weak at the lifecycle holes above. I saw nothing that would catch stuck stick or key state after a device or focus loss, or the cursor jitter. I saw nothing that bounds `DamageNumbers` cost, and nothing that exercises a 64×64-or-larger fog map for time or memory.

**Overall health.** Good. These are clean, defensive and heavily tested modules. The real defects are in the input lifecycle (stale held state), the fog fade's scaling, and the HUD ignoring the device in use. The rest is low-severity polish.

**Files read in full:** /home/lotus/Development/project_n/src/app/bindings.hpp, /home/lotus/Development/project_n/src/app/bindings.cpp, /home/lotus/Development/project_n/src/app/camera_control.hpp, /home/lotus/Development/project_n/src/app/camera_control.cpp, /home/lotus/Development/project_n/src/app/camera_path.hpp, /home/lotus/Development/project_n/src/app/camera_path.cpp, /home/lotus/Development/project_n/src/app/cast.hpp, /home/lotus/Development/project_n/src/app/cast.cpp, /home/lotus/Development/project_n/src/app/target_cursor.hpp, /home/lotus/Development/project_n/src/app/target_cursor.cpp, /home/lotus/Development/project_n/src/app/click.hpp, /home/lotus/Development/project_n/src/app/click.cpp, /home/lotus/Development/project_n/src/app/hud.hpp, /home/lotus/Development/project_n/src/app/hud.cpp, /home/lotus/Development/project_n/src/app/damage_numbers.hpp, /home/lotus/Development/project_n/src/app/damage_numbers.cpp, /home/lotus/Development/project_n/src/app/fog_fade.hpp, /home/lotus/Development/project_n/src/app/fog_fade.cpp

## Findings

### APP-INPUT-UI-01: Held-pad cast aims at the mouse cursor / key casts have no keyboard/gamepad aiming

- **Severity:** medium · **Verdict:** deliberate · **Category:** accessibility · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/app/cast.cpp:43`

CastAction needs a world 'point' from the cursor; App::Cast takes m_window.Cursor() for a non-mouse device (so keyboard and gamepad casts aim wherever the mouse rests, or send nothing).

**Evidence:** CastAction returns nullopt when point is absent ('a key cast without a cursor sends nothing', cast_tests.cpp:123); app.cpp Cast() uses m_window.Cursor().

**Impact:** Gamepad-only players cannot aim combat: a conflict with 'every option by mouse, keyboard and gamepad'.

**Recommendation:** Add aim sources (right-stick direction, auto-target nearest, keyboard aim keys) as part of the bindings chunk.

**Verification:** True as stated (CastAction needs a point; cast.hpp doc says no point means no cast). Recorded in sweep-up-backlog.md item 4 as a known deferral awaiting a small design (aim at followed creature, target or aim stick). Real accessibility gap, medium stays.

### APP-INPUT-UI-02: Gamepad unplug leaves the stick (and held pad buttons) latched: character walks or pans forever

- **Severity:** low (reviewer said high) · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/bindings.cpp:250`

Bindings stores the last stick axis values (m_stick, m_panStick) and a held-key bitmask (m_heldKeys); DeviceInUse stores its own copies (m_stick, m_rightStick). They change only on input events. SDL_EVENT_GAMEPAD_REMOVED is handled in platform/window.cpp (CloseGamepad) by closing the pad, with no neutral axis or button-release events sent. Bindings has no Reset()/ReleaseAll(), and App::HandleInput (app.cpp ~519-525) calls m_camera.LoseFocus() and m_moveLatency.Forget() when paused but never clears bindings.

**Evidence:** Bindings::Handle sets m_stick.x = input.value for PadLeftX; the only way it returns to 0 is another axis event. No code path synthesises one on removal. Same for PadDpad* bits in m_heldKeys.

**Impact:** Unplugging or losing a Bluetooth pad while the stick is deflected or a d-pad direction is down makes Move()/Pan() keep returning non-zero until another device event arrives: an uncontrollable walk or pan, and DeviceInUse can keep reporting gamepad. Accessibility-first project, so this matters.

**Recommendation:** Add Bindings::ReleaseAll() and DeviceInUse::Reset(); have the platform layer emit a removal event (or App detect it) and call them, and on focus loss too, as defence in depth if the OS does not synthesise key-ups. Add a test that a held stick/key is zero after the reset.

**Verification:** Code facts hold: Bindings keeps m_stick/m_panStick/m_heldKeys, changed only by events (bindings.cpp 118-142). CloseGamepad (window.cpp 257-263) only closes the pad, and App::HandleInput's pause branch (app.cpp 520-524) never clears Bindings. But the claim of a permanent latch is not shown. SDL3 force-recentres a joystick when it is closed or removed, which sends axis-zero and button-up events. I recall this from SDL3 but could not check it by reading here, so a real unplug test would settle it. There is no Reset path and no test, which is a gap, but 'walks forever' is overstated. The severity is high only if SDL does not recentre.

### APP-INPUT-UI-03: No reset path for held inputs on focus loss or pause

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/app/bindings.hpp:127`

Held movement/pan keys are tracked only by press/release events. SDL generally synthesises key-ups on focus loss, but the repo does not rely on that or test it: the Drag and the camera edge-arm are explicitly cleared on focus loss ('a button's release may never reach it') while Bindings' keys are not. The inconsistency suggests a missed case.

**Evidence:** app.cpp HandleInput: on m_paused it calls m_camera.LoseFocus() and m_moveLatency.Forget() only; Drag::End() exists, Bindings has no equivalent.

**Impact:** If a platform or compositor drops the key-up (Wayland variants, remote desktop, a modal OS dialog), a movement key stays latched on return.

**Recommendation:** Clear Bindings' held state when the window loses focus, the same place Drag::End and CameraControl::LoseFocus are called, and document it.

**Verification:** True that nothing clears Bindings' held keys when the window loses focus (app.cpp 520-524 clears the camera and latency only). SDL also synthesises key-ups on focus loss, so the impact is speculative. The inconsistency with Drag and camera edge-arm is real but harmless in practice. Low is right.

### APP-INPUT-UI-04: FogFade recomputes and rescans the whole map on every player step

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/fog_fade.cpp:15`

Every change of fog.version, playerTile or sightRadius runs SetTargets(), which does m_target.assign(tiles*2, 0), loops all tiles for explored bits, then FindMovers() scans all 2*tiles values. Only the sight diamond (radius <= 64) and newly explored tiles actually change. current and target are f32 pairs (16 bytes/tile) plus a byte copy.

**Evidence:** m_target.assign(tiles * 2, 0.0F); for (tile < tiles) ... IsExplored(...); FindMovers loops m_current.size().

**Impact:** O(map) main-thread work each time the cultivator crosses a tile. Negligible at 64x64 (MAP_COLUMNS=64 today), but maps are procedural and 'may be very large': at 4096x4096 that is a 16M-tile scan and about 260 MB of fog state, a visible per-step hitch. The wasted-work rule (avoid-wasted-work) says hot paths compute only what is drawn.

**Recommendation:** Keep the previous sight diamond; on change, clear the old diamond's channel-1 targets and set the new, and track newly explored tiles by diffing explored words (or have the sim publish a delta list). Build the movers list from touched tiles only. Consider u8 targets and u8 current with a fixed-point step instead of f32. Add a scaling test or a measured spike.

**Verification:** fog_fade.cpp SetTargets runs assign(tiles*2) plus full explored and sight scans on each change, and FindMovers scans all of m_current. The cost is O(map) per step as described. At the current 64x64 it is negligible, and the 4096x4096 case is hypothetical. I found no design doc or backlog entry that records it as deliberate. The avoid-wasted-work rule makes it a fair design note, but medium is too high for now.

### APP-INPUT-UI-05: HUD prompts are hard-coded to mouse/keyboard and ignore DeviceInUse

- **Severity:** low (reviewer said medium) · **Verdict:** deliberate · **Category:** accessibility · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/hud.cpp:15`

KeyOf() maps Generator/Spender/Movement to StringId::KeyLeftClick/KeyRightClick/KeySpace and the specials to digit literals 1-5. Hud::Build never takes a device or family, although DeviceInUse is documented as deciding 'what prompts will show'. The strings are not derived from the Bindings table either.

**Evidence:** hud.cpp KeyOf; hud.hpp Hud::Build signature has no Device parameter; bindings.hpp DeviceInUse::Used()/Family().

**Impact:** A gamepad player is told to 'Left click'/'Space', against the stated accessibility-first priority. When rebinding lands (planned), the HUD silently lies, because prompts are not generated from the table.

**Recommendation:** Derive prompt strings from the active Bindings table and the DeviceInUse family (glyph or name), pass them into Hud::Build, and add a test per device.

**Verification:** hud.cpp KeyOf hard-codes the mouse and keyboard strings and Hud::Build takes no device. docs/design/0002.3-input-data-scenes.md line 69 says 'Nothing draws a prompt until the UI...'. 0002.4-ui-and-text.md lines 20 and 235-240 defer glyph prompts and the `<prompt>` element to the UI chunk, and rebinding to chunk 7. The facts are right, but it is a recorded deferral. Accessibility makes it worth tracking.

### APP-INPUT-UI-06: Target cursor steps on every stick threshold crossing: no hysteresis or debounce

- **Severity:** low · **Verdict:** confirmed · **Category:** accessibility · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/target_cursor.cpp:69`

Steer() resets m_stepColumns/m_stepRows to 0 whenever an axis is inside +/-0.5, and any new direction steps immediately and returns. A stick jittering around the threshold (tremor, worn stick) therefore steps once per crossing with no repeat limit. Rotating the stick from one cardinal to another also passes through a diagonal state that steps both axes, and releasing one axis of a diagonal steps again.

**Evidence:** if (columns != m_stepColumns || rows != m_stepRows) { ...; Step(columns, rows); return; }. Tests only cover a single clean crossing ('a stick axis steps once past half its travel').

**Impact:** Overshooting steps for users with tremor, and unintended diagonal steps when changing direction. Accessibility is the project's top priority.

**Recommendation:** Add hysteresis (press at 0.5, release at ~0.35), and optionally a short step-settle time for a changed direction. Add tests for the jitter and diagonal-transition cases.

**Verification:** target_cursor.cpp Steer (69-87) matches the description. A change of direction steps immediately and returns. Pressed() is a plain threshold with no hysteresis, and releasing one axis of a diagonal re-steps because the pair changes. The tremor impact is plausible. The tests cover a clean crossing only. Low is right.

### APP-INPUT-UI-07: TargetCursor divides by zero if shown on an empty map; Confirm does not check the map matches

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/app/target_cursor.cpp:47`

Show() stores columns/rows unvalidated. Step() does tile % m_columns and tile / m_columns. Confirm(map) uses m_tile with whatever map is passed, without comparing the stored dimensions, and Show() does not check tile < columns*rows.

**Evidence:** u32 column = Moved(tile % m_columns, ...) with m_columns possibly 0; Confirm: map.TileCentre(*m_tile).

**Impact:** Division by zero (UB) if Show is ever called with columns 0. If the world map is replaced (load, new seed, scene change) while the cursor is shown, Confirm can pass an out-of-range tile to TileCentre. App hides the cursor on non-world-map snapshots, but not on a world swap within the world scene.

**Recommendation:** PN_ASSERT columns/rows > 0 and tile < columns*rows in Show(); have Confirm return none (and hide) when the map's dimensions differ from the stored ones; hide the cursor on epoch or world change.

**Verification:** Show() stores columns and rows unchecked, and Step() divides by m_columns. Confirm() does not compare the stored map size with the one passed. A zero-column map is not realistic. Whether the cursor stays shown across a world swap in the same scene is unverified. This is defensive hardening at most, so low or info.

### APP-INPUT-UI-08: DamageNumbers::Play(UseEnded) rebuilds the waiting index and rescans waiting numbers on every UseEnded

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/damage_numbers.cpp:127`

Every UseEnded walks all of m_waiting and, even when nothing matched, clears and re-emplaces the whole m_waitingOf map. Cost is O(waiting) per ended use, so a tick with many uses ending while many combined numbers wait is quadratic. Numbers are also copied (about 72 bytes each) as they move.

**Evidence:** for (Number& number : m_waiting) ...; m_waitingOf.clear(); for (index...) m_waitingOf.emplace(...).

**Impact:** Combined-number mode with large fights (the planned bullet-heavy combat, debug burst of 4000) can spend avoidable main-thread time at the tick boundary. Early-exit exists only for an empty m_waiting.

**Recommendation:** Group waiting numbers by use (a map use -> list), move that list into m_playing on UseEnded, and delete the keys without rebuilding everything. At minimum skip the rebuild when no number matched.

**Verification:** damage_numbers.cpp lines 112-135 match. Each UseEnded with a non-empty m_waiting scans it, then clears and re-emplaces the whole m_waitingOf even if nothing matched. The cost is O(waiting) per ended use. It matters only in combined mode with huge fights. The minimal fix (skip the rebuild when nothing was removed) is valid.

### APP-INPUT-UI-09: CastLatency attributes the next volley to the oldest press even if that press was refused

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/app/cast.cpp:88`

Drawn() pairs a fresh volley with m_presses.front() (the oldest). A press refused for cooldown/Qi stays in the queue for up to 1 s, so a second press whose volley does draw is measured against the first, inflating the number. App::Cast also records a press for every Special1 key press without knowing if it will be accepted. The match is also by ability only, so another caster's volley of the same ability (e.g. an NPC with FlyingSwords) counts as the answer.

**Evidence:** const u64 pressed = m_presses.front(); m_presses.erase(...); return nowNs - pressed; VolleyView has no owner filter here.

**Impact:** Overstated or false input-latency numbers in profile runs. This is a measurement tool, not gameplay, so low impact.

**Recommendation:** Filter volleys by the player's ownership, and match the newest press older than the volley's start tick (or drop presses the sim refused). Document the limitation if left.

**Verification:** Drawn() does pair with m_presses.front() and VolleyView (core/frame.hpp:306) has no owner field, so matching is by ability only; a refused press stays up to the limit. Real but only in the --cast-swords profile measurement tool. Cause is a documented-simple matcher; low impact stands.

### APP-INPUT-UI-10: Held stick pan ignores focus

- **Severity:** low · **Verdict:** deliberate · **Category:** correctness · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `src/app/camera_control.cpp:167`

Pan from the right stick and keys is not gated on input.focused, unlike edge pan and drag.

**Evidence:** held = input.move + input.pan, no focused check.

**Impact:** A held key or stick keeps panning when the window is unfocused (input events normally stop, but the held state persists).

**Recommendation:** Gate pan and move on focus or clear on focus loss (see the Bindings reset finding).

**Verification:** Confirmed: held = input.move + input.pan with no focused check, while edge pan and cursorIn use focused. Recorded in sweep-up-backlog.md item 5 (with the cause: SDL doesn't reset gamepad axes on focus loss). Known deferral; the backlog asks for a unit test too.
