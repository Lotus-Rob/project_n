# Review: x-accessibility (part 2)

### X-ACCESSIBILITY-11: No screen-reader, TTS or high-contrast mode in any spec; first-launch page assumes the player can already read it

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** accessibility · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk11-settings-design.md:98`

Blind and low-vision players have no support: RmlUi has no accessibility tree and nothing in the specs plans a speech path, narrated menus, or a high-contrast whole-UI theme (only 'high-contrast labels and selection outlines'). The first-launch essentials page (text size, palette, reduce motion) is itself drawn at the default text size, and the spec's Goal 5 gives no way to open it by keyboard or gamepad without reading it. Labels fade out below 9 px, and numbers stop shrinking at 12 px.

**Evidence:** chunk 11 goals 4 and 5; text_place.hpp LABEL_MIN_PIXELS = 9, NUMBER_MIN_PIXELS = 12; no TTS or speech mention in chunk 8, 10 or 11.

**Impact:** Players who cannot read the default UI cannot reach the settings that would help them, and sighted-only navigation is assumed. Zoomed-out play hides labels entirely rather than enlarging them.

**Recommendation:** Record explicitly whether TTS and screen-reader support are in or out of scope (SDL has no speech API; a platform-speech or a native-menu fallback would be needed). Let the first-launch page scale from a hotkey and the OS display scale, and always reachable by one key or button. Make minimum label size a primary accessibility option, not Advanced.

**Verification:** Grep of chunk 11, the phase spec and docs/design finds no TTS, screen-reader or speech-path mention (only in-game speech bubbles), so the omission is real. Chunk 11 line 52 does include high-contrast labels and selection outlines. Chunk 11 line 62 makes the first-launch page skippable in one click. Whether it can be opened by key or gamepad, and its text size, is unspecified. A scope statement on TTS is a fair ask. Treating this as a defect for a game whose UI is not built is speculative.

### X-ACCESSIBILITY-12: No high-DPI handling: the window is not created with a pixel density flag and text sizes are in swapchain pixels

- **Severity:** low · **Verdict:** confirmed · **Category:** accessibility · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/platform/window.cpp:289`

SDL_CreateWindow is called with SDL_WINDOW_RESIZABLE (and fullscreen) only, without SDL_WINDOW_HIGH_PIXEL_DENSITY, and nothing reads the display content scale. The UI scale is planned to follow window height over 1080 only.

**Evidence:** window.cpp `SDL_CreateWindow(..., SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN : 0))`; grep for pixel_density, content_scale and dpi finds nothing.

**Impact:** On macOS Retina and scaled Wayland or Windows displays the game renders at the logical size and the OS upsamples it: blurry text for low-vision players, who also rely on the OS display scale as their primary magnification. Pixel sizes of text ignore the user's system text scale.

**Recommendation:** Request high pixel density and read SDL_GetWindowDisplayScale; fold it into the UI and text scale defaults, and use the window's pixel size for the swapchain. Test the ratio on a scaled display.

**Verification:** The call is at line 289, not 300: SDL_CreateWindow(..., SDL_WINDOW_RESIZABLE | fullscreen flag), with no HIGH_PIXEL_DENSITY flag. Grep of src finds no pixel_density or content_scale use. The gap is real. The impact depends on the platform and is untested on HiDPI, so this is a low-severity backlog item.

### X-ACCESSIBILITY-13: Held-input model has no hold-or-toggle, sticky keys or tuneable dead zone, and its key bitmask will not fit a rebindable table

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** design · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `src/app/bindings.hpp:128`

Move and pan require holding keys; the stick dead zone is a fixed 0.2 (STICK_DEAD_ZONE) with no settings field. The held-key set is a u64 with a bit per InputCode (static_assert PadDpadRight < 64), and Handle() maps each physical code to exactly one control (std::ranges::find on the first entry).

**Evidence:** bindings.cpp Bit() and `static_assert(... < std::numeric_limits<u64>::digits)`; chunk 11 asks for all keys by scancode, primary and secondary inputs per control per device, and hold-or-toggle.

**Impact:** Players who cannot hold keys cannot move; a worn stick that rests beyond 0.2 constantly walks or flips the prompts to gamepad (DeviceInUse counts any axis past the dead zone as the device in use). The bitmask and single-control lookup have to be redesigned for chunk 11 rather than extended.

**Recommendation:** Plan the data model first: per-control held state, input to many controls, a dead-zone setting per stick, and a toggle mode on Move and DragCamera. Add the dead-zone value to Settings with the other accessibility fields.

**Verification:** STICK_DEAD_ZONE is a fixed constant (bindings.cpp:43,47,167), so no tuneable dead zone exists today, and chunk 11 does not list a dead-zone setting (grep finds none). Chunk 11 line 53 does plan hold-or-toggle for held inputs, and line 81 makes bindings data-driven with primary and secondary inputs, so the u64 bitmask redesign is anticipated. The dead-zone gap in the spec is a valid small addition. The rest is a known deferral to chunk 11.

### X-ACCESSIBILITY-14: Position-based key codes and the Alt modifier chord assume a US layout and two hands

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** accessibility · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/platform/window.hpp:129`

Keys are read by scancode (KeyMinus/KeyEquals 'by its place', WASD by position). Prompts have to map scancodes to the layout's label (SDL_GetKeyFromScancode); the specs say 'keycap with the key's name'. The 'select in combat' action needs Alt held while clicking, and the camera snap-back is a middle-click that dragged nothing.

**Evidence:** window.hpp KeyMinus doc 'The key right of 0 on a US keyboard'; cast.hpp SelectsInstead(control == Primary && (!combat || alt)); bindings.hpp doc comment on Drag::Release.

**Impact:** On AZERTY or Dvorak layouts the zoom keys and default movement sit in odd places and the prompts could show the wrong label if they use the scancode name; a player using one hand, a head pointer or a single switch cannot chord Alt+click or middle-click.

**Recommendation:** Draw prompts from the layout's key label. Offer every chorded or middle-button action a single-input alternative (a key, or a UI button), and list this in the Rebinds spec.

**Verification:** Scancode-based keys are a normal, intentional choice, and chunk 11 itself rebinds by scancode. The prompt-label concern is a valid implementation note but is unbuilt, so there is no current bug. The Alt+click for select-in-combat and the middle-click snap-back are single-input-alternative gaps that chunk 11's rebinding may cover. I did not verify the cast.hpp citation in depth. Low confidence in impact.

### X-ACCESSIBILITY-15: Display change revert timer is fixed at 15 seconds and its confirmation needs input from a possibly unseen screen

- **Severity:** low (reviewer said info) · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** low
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk11-settings-design.md:43`

Display changes revert after 15 seconds unless confirmed. The timer is not adjustable and the confirm prompt is a UI element on the possibly broken display.

**Evidence:** chunk 11 goal 2 and 'Calls made here': 'Display changes revert after 15 seconds'.

**Impact:** Slower players (motor, low vision, cognitive) may run out of time to confirm a resolution they can actually see, so they are bounced back. Keep the revert but make the time an accessibility option or let any key press in the dialog extend it.

**Recommendation:** Make the countdown longer by default (30 s) and extend it on any input; let Enter or A confirm without reading the text.

**Verification:** Chunk11 spec lines 42 and 198 fix the revert at 15 s, and it is not adjustable; checked the spec, no countdown option or extend-on-input rule appears. Whether the confirm prompt is on a broken display is by design: the revert exists for that case, and the prompt can be answered by keyboard or gamepad. Not recorded as a deliberate accessibility trade-off. Spec-level only, no code yet. A longer default or extend-on-input is a reasonable low-cost improvement.

### X-ACCESSIBILITY-16: Search in settings is Latin-keystroke only; gamepad has no text path

- **Severity:** low (reviewer said info) · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** low
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk11-settings-design.md:65`

The settings search box takes Latin keystrokes with pinyin keywords per row (no IME), and on a gamepad 'the shoulders and the category list reach everything search would'. Native Chinese characters cannot be searched and there is no on-screen keyboard.

**Evidence:** chunk 11 Design: 'Search without an IME (text input stays a phase non-goal)'.

**Impact:** Chinese-language players who do not know pinyin cannot search; gamepad players have only browsing. Acceptable for now but it should be written down against the accessibility priority, together with the planned seed-name and save-slot text entry that will hit the same limit.

**Recommendation:** Enable SDL text input and IME events when the first text field lands, and record the on-screen keyboard as needed for gamepad and save naming.

**Verification:** Spec lines 139-140 confirm Latin keystrokes plus English and pinyin keywords, with no IME. The spec explicitly calls text input 'a phase non-goal', so the omission is deliberate. The claim that gamepad has no text path is true for search, and the spec says the shoulders and category list cover it. The spec does not record an on-screen keyboard or IME need for later seed-name or save-name entry, so the gap against the accessibility priority is real but only a documentation gap. Impact is low.

### X-ACCESSIBILITY-17: Camera shake, hit-stop and other motion planned in design 0002 have no reduce-motion hook

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** accessibility · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/design/0002-engine-architecture.md:160`

The events channel lists 'camera shake' among its cosmetic events, and the planned GPU projectiles, particles and bloom add screen-wide motion. The only motion options implemented are damage-number rise versus bounce and the (planned) reduce-motion setting. Camera easing, fog motion, parallax and the chunk 8 per-window animation are covered only by a sentence in chunk 11.

**Evidence:** 0002 line 160 event table; settings.hpp NumberMotion; chunk 11 'camera's easing calm under reduce motion'.

**Impact:** When shake and screen effects arrive, nothing forces them through the setting, so a vestibular-sensitive player can be hit by motion that the specs say they can turn off.

**Recommendation:** Make reduce motion a field in the render and camera inputs now (like the glow hook), so every new motion effect must read it, and add a lint or test that lists the effects that ignore it. Refuse camera shake without a strength setting.

**Verification:** Design 0002 line 160 lists 'camera shake' among event consumers. But chunk 4 (line 175) and chunk 4b (line 241) both state 'Nothing shakes the camera', and chunk 11 (lines 163-165) wires reduce motion into the number bounce, camera easing and seal turn. The risk is hypothetical until shake exists. The hook-now recommendation is reasonable but not a current defect.

### X-ACCESSIBILITY-18: Accessibility rule 'settings never change difficulty' excludes game-speed and assist options

- **Severity:** info (reviewer said low) · **Verdict:** deliberate · **Category:** design · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk11-settings-design.md:107`

Chunk 11 forbids any gameplay setting that makes the game objectively easier or harder, and the memory file records the same rule. Players with motor or reaction-time limits typically need exactly such options (game speed, a longer dodge window, aim assist, auto-target, one-handed schemes) to play a bullet-heavy action game at all.

**Evidence:** chunk 11 Non-goals 'Difficulty options, or any gameplay setting that makes the game objectively easier or harder (the user's rule)'; decision 3.

**Impact:** With a pause only on the world map, no assist and no speed setting, the 'accessibility first' priority and the no-difficulty rule can contradict each other for the player who cannot meet the timing at all. The rule is a deliberate decision, so I flag the cost.

**Recommendation:** Reopen the rule narrowly: allow an opt-in 'assist' group (game speed, auto-aim, longer input buffer), kept apart from difficulty and clearly labelled, or state explicitly in the spec which assists are accepted and why. At minimum decide on a combat pause and auto-target before chunk 7's combat scenes ship.

**Verification:** Chunk 11 line 15 ('never difficulty') and line 107 (Non-goals: 'the user's rule') record this as the user's explicit rule, and the memory file repeats it. The finding admits it is deliberate. Whether assist options such as game speed or auto-aim count as difficulty is a product decision for the user, not a code defect. Decide before the combat scenes ship, as the finding suggests.
