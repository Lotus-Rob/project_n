# Review: x-accessibility

**Scope:** CROSS-CUTTING accessibility (the project's stated top priority: settings never change difficulty; every option reachable by mouse, keyboard and gamepad): input bindings and remapping, gamepad coverage, colour reliance and colour-blind safety (ui_colours, damage numbers, seals, fog), text size/contrast/scaling, screen shake/flash/photosensitivity, pause behaviour, timing demands, audio cues (or absence of audio entirely), and how the specs for UI/settings (docs/superpowers/specs/*chunk8*, *chunk10*, *chunk11*) deliver or fail it.

**Findings:** 18 standing (0 critical, 0 high, 5 medium, 11 low, 2 info), 0 raised but refuted on verification.

**Parts:** x-accessibility.md, x-accessibility-part2.md

## Assessment

## Accessibility review: assessment

**Scope read.** I read `app/bindings.{hpp,cpp}`, the input paths in `app/app.cpp`, `app/cast.cpp`, `app/camera_control.cpp`, the input code in `platform/window.{hpp,cpp}`, `core/ui_colours.{hpp,cpp}`, `core/settings.hpp`, `data/settings.{hpp,cpp}` and the glow, effect and damage-number code. I also read the chunk 8, 10 and 11 specs, `effects.yaml` and the relevant parts of the phase 6 design and 0002.x. I did not build or run anything.

**The intent is excellent. What exists is almost all hooks.**
- The chunk 11 spec is thorough: accessibility is the first category, there is a colour-blind palette plus a per-colour editor, reduce motion and reduce flashing, hold-or-toggle, text size, UI scale, a first-launch essentials page, a live preview for every visual option, and per-device navigation rules.
- Shipped today:
  - Every UI colour is a named, replaceable table entry (`UiColours`). No colour sits in code or a shader.
  - Text outline and shadow are switchable, with dark fallback colours when both are off.
  - A single `TextOptions::scale` multiplies all game text, minimum sizes included.
  - Damage-number motion can be set to rise instead of bounce.
  - The HUD shows every value as a number as well as a bar, and "Not enough Qi" is text.
  - Healing numbers carry a "+" sign.
  - The window pauses the simulation on focus loss.
  - The device in use is tracked for prompts.
  - Glow pulses are capped at 3 Hz.
  - Gamepad axes have a radial dead zone.
- None of the chunk 11 options is reachable by the player yet. `Settings` has no accessibility fields, and `ParseSettings` and `SaveSettings` know only language, number motion, outline, shadow, combined numbers and edge_pan. The player cannot reach the colour table, text scale, reduce motion or reduce flashing at all.
- The process rule is that every chunk spec carries an Accessibility section (decision 17). Chunks 8 and 10 have none (see the finding on the specs). Chunk 8's gamepad behaviour conflicts with the shipped bindings.

**Biggest gaps.**
1. There is no non-mouse way to play combat. Gamepad and keyboard-only players cannot cast the generator or spender, or pick a target. Casting depends on the mouse position.
2. The player cannot pause during combat and cannot quit with a gamepad. Escape quits instantly, with no confirmation.
3. The gamepad input model has real bugs. A removed pad leaves its stick and d-pad state stuck, and several pads share one stick slot.
4. Damage kinds, poison against burn against healing effects, and hover against selected outlines are separated by colour alone. The default hues are close under red-green colour blindness.
5. The photosensitivity and motion hooks exist for glows only. Nothing limits additive-light stacking, the planned camera shake has no reduce-motion hook, and there is no warning screen.
6. There is nothing for blind or low-vision players: no screen reader or TTS path, no HiDPI handling, and text that fades out below 9 px.
7. The "settings never change difficulty" rule excludes game-speed and assist options that are accessibility staples. This is a deliberate decision; I flag the cost.

**Health.** Good foundations: data-driven colours, one text scale, pure testable input code. The risk is schedule. The specs promise a complete accessibility layer in chunks 8, 10 and 11, but the keyboard, gamepad and combat gaps need decisions now, because they affect the bindings data model that chunk 11 will build.

**Files read in full:** (not reported)

## Findings

### X-ACCESSIBILITY-01: Stuck movement or pan after a gamepad is removed, because stick state is never zeroed

- **Severity:** medium (reviewer said high) · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/platform/window.cpp:257`

CloseGamepad() just closes the SDL_Gamepad and logs. No zero axis events or button releases are emitted. Bindings::Handle() stores the last PadLeftX/Y and PadRightX/Y values in m_stick/m_panStick and the d-pad keys in m_heldKeys. DeviceInUse also keeps m_stick/m_rightStick. Nothing in App resets them on a disconnect.

**Evidence:** window.cpp SDL_EVENT_GAMEPAD_REMOVED -> CloseGamepad(); bindings.cpp Handle() sets m_stick.x = input.value only when an axis event arrives; there is no Bindings reset method, and the only reset on focus loss is m_camera.LoseFocus() and m_drag.End().

**Impact:** A player whose wireless pad dies, or who unplugs it while the stick is deflected or the d-pad held, gets a cultivator that keeps walking in that direction (and a camera that keeps panning) until they find the keyboard. For motor-impaired players on adaptive hardware that disconnects often this is severe. The same applies to PadRightStick and other held pad buttons after a disconnect.

**Recommendation:** On SDL_EVENT_GAMEPAD_REMOVED (and on focus loss) have the platform synthesise releases and zero axis events, or add Bindings::ReleaseAll() and DeviceInUse::Reset() called by App. Add a test that deflects a stick, signals removal and expects Move() to be zero.

**Verification:** window.cpp has only CloseGamepad on removal; Bindings::Handle stores m_stick/m_panStick/m_heldKeys with no reset path, and the only focus-loss reset is m_camera.LoseFocus() (app.cpp:523). The stuck-movement scenario holds. Impact is a real bug but needs a pad removed while deflected; high is slightly overrated.

### X-ACCESSIBILITY-02: Casting generator and spender has no keyboard-only binding, and no gamepad binding at all

- **Severity:** medium (reviewer said high) · **Verdict:** deliberate · **Category:** accessibility · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/app/bindings.cpp:75`

Primary and Secondary are bound only to MouseLeft and MouseRight. The slot controls (Primary, Secondary, Movement, Special1..4, Ultimate) have no PadX binding. Every cast needs a point from the mouse cursor (Cast() uses m_window.Cursor()). Selecting in combat additionally needs Alt held with a mouse click (SelectsInstead).

**Evidence:** bindings.cpp has KeySpace and Key1..5 for slots but nothing for Primary/Secondary besides the mouse; chunk 11 non-goals say gameplay controls that need a pointer have no gamepad binding yet. cast.cpp CastAction returns nullopt when `point` is empty.

**Impact:** The project's stated top priority is that every option is reachable by mouse, keyboard and gamepad. Today a keyboard-only player can cast only the numbered slots, and only if the mouse cursor is also over the window (no cursor means no cast). A gamepad player cannot cast, select or aim in combat. A mouse-less player cannot fight, and the aim point has no stick, target-cycling or aim-assist design yet (it is listed as a future task). Known-deferred as audit 4 in code, but it is the central accessibility blocker for the genre's combat.

**Recommendation:** Settle the aim model before chunk 11 builds the rebinding table: stick-aim or target-cycling cursor (the target cursor already exists for the map), with primary/secondary on gamepad buttons and keys. Add a keyboard fallback for the generator and spender. Without a pointer, auto-target the nearest enemy.

**Verification:** The sweep-up-backlog item 4 records that casts aim at Window::Cursor() and that pad and keyboard have no aim; chunk 11 spec line 111 lists full gamepad play (aim point, abilities on buttons) as a future task. bindings.cpp has Primary and Secondary on the mouse only. This is a known deferral, but it is a genuine accessibility gap.

### X-ACCESSIBILITY-03: Escape quits instantly with no confirmation; the gamepad cannot quit or open any menu

- **Severity:** medium · **Verdict:** partially-confirmed · **Category:** accessibility · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/app.cpp:536`

Control::Quit is bound to KeyEscape only and sets m_quit immediately. The chunk 10 pause menu (Escape opens it, quit asks once) is not built. No gamepad button (Start, B) is mapped: InputCode has no PadStart, PadEast, PadWest, trigger or left-stick click codes.

**Evidence:** bindings.cpp: `{ KeyEscape, Quit }`; app.cpp `if (control->control == Control::Quit) m_quit = true;`; window.cpp BUTTONS covers only shoulders, right stick, Back, South, North and the d-pad.

**Impact:** A tremor, a stuck key or a mis-hit Escape ends the session and loses unsaved play. A gamepad-only player cannot leave the game. The chunk 11 spec guards Escape and Start for the pause menu, but Start does not exist in platform yet and B (East) is not readable.

**Recommendation:** Make Escape open the pause menu once chunk 10 lands. Until then add PadStart, PadEast, PadWest and the triggers to platform, since all UI navigation and rebinding rely on them, and put the quit behind a confirmation.

**Verification:** bindings.cpp binds KeyEscape to Quit, and app.cpp sets m_quit at once. The chunk 10 spec (line 53) plans Escape to open the pause menu, so the instant quit is an interim state. The claim that the gamepad cannot quit is true: the BUTTONS table has no Start or East. Missing platform codes are not recorded as deferred.

### X-ACCESSIBILITY-04: Combat cannot be paused by the player

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** accessibility · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:940`

The pause control (P, gamepad Back) is ignored outside scenes with a world map. In the combat arena the only pauses are window focus loss or minimise (and the not-yet-built pause menu).

**Evidence:** Host::ToggleWorldPause: `if (!Running().Traits().worldMap) { ... the world pause is ignored ... return; }`. Bindings::PauseWorld doc: 'Pause or resume the world map's clock'.

**Impact:** Real-time, bullet-heavy combat with no pause leaves no recovery for players who need to stop (fatigue, pain, interruptions, caregivers, time to read or rebind). The 'never change difficulty' rule means a pause is the main fairness tool. Chunk 10 plans modal pauses, but a pause key in combat should not depend on a menu.

**Recommendation:** Bind a pause that works in every scene, with Escape/Start opening the pause menu, and say so in chunk 10. Make focus loss and the pause menu the same mechanism. Add a test that a combat scene pauses.

**Verification:** Host::ToggleWorldPause returns early when !worldMap, so P does nothing in combat. Chunk 10 (lines 101-103) plans modal screens that pause the game, and focus loss already pauses. So a pause in combat is planned via the menu, but only after Escape opens it. A direct pause key is not specified. Severity is lower than high because the plan covers it.

### X-ACCESSIBILITY-05: Settings have no accessibility fields, and edge pan cannot be switched off in-game or saved

- **Severity:** medium · **Verdict:** confirmed · **Category:** accessibility · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/data/settings.cpp:165`

core::Settings and ParseSettings cover language, combined damage numbers, motion, outline, shadow and edge_pan only. SaveSettings omits edge_pan (and colours), so the file it writes does not round-trip, and a user who sets `edge_pan: false` and then triggers a rewrite loses it. Edge pan defaults to on. A pointer resting at the window edge pans the roaming camera, and the only way to turn it off is hand-editing YAML.

**Evidence:** SaveSettings format string lists window, language, combined_damage_numbers, damage_number_motion, text_outline, text_shadow; ParseSettings reads edge_pan; core/settings.hpp says uiColours 'doesn't read it until the accessibility menu exists'.

**Impact:** Unintended camera movement is a known problem for tremor and low-vision users, and today there is no UI path to fix it. No setting exists for text size, reduce motion, reduce flashing, palette, cursor size, dead zones, hold-or-toggle or UI scale although code hooks (TextOptions::scale, glow hooks) already exist. A round-trip test missing for edge_pan.

**Recommendation:** Write edge_pan in SaveSettings, with a settings round-trip test over every field. Before the chunk 11 UI, add the cheap fields (text scale, reduce motion/flashing, dead zones) to Settings so the hooks can be driven and tested.

**Verification:** SaveSettings (settings.cpp:165-188) writes no edge_pan, although ParseSettings reads and accepts it. A rewrite therefore drops the user's edge_pan: false. The missing accessibility fields are plausible since chunk 11 is not yet built. The round-trip bug is real.

### X-ACCESSIBILITY-06: Axis events from several gamepads share one stick slot

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/bindings.cpp:108`

InputEvent carries a family but not a device or instance id. Bindings::Handle() and DeviceInUse::Take() write PadLeftX/Y and PadRightX/Y into single m_stick/m_panStick/m_rightStick values regardless of which pad sent them.

**Evidence:** Window opens every connected gamepad (OpenGamepad pushes each into `gamepads`); AxisInput() sets only `.family`. Handle(): `m_stick.x = input.value`.

**Impact:** With two controllers (a common accessibility setup: an adaptive controller or a one-handed pad beside a standard pad) any idle pad that reports a small non-zero or drifting axis overwrites the active pad's value, so movement stutters or stops. A worn pad with drift also fights the keyboard-free hand.

**Recommendation:** Add a device id to InputEvent, keep per-device stick state and combine by largest magnitude (or take the most recently used device), and cover it with a two-pad test.

**Verification:** Handle() writes m_stick.x = input.value with no device id, so two pads overwrite each other's axis. Real but a niche multi-pad case; no design records it.

### X-ACCESSIBILITY-07: Default palette separates damage kinds and element effects by colour alone

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** accessibility · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/ui_colours.hpp:131`

Damage, poison and burn numbers differ only in colour (white, purple #B07CE8, orange #FF9A3C); only healing has a '+'. The element effect colours are healing #8FE3A8, poison #7CD957 and burn #FF8A3D: green, yellow-green and orange, close together for deuteranopia and protanopia, with similar luminance. Effect glows, bursts and trails have no number or icon.

**Evidence:** DEFAULT_UI_COLOURS values; damage_numbers.cpp:235 sets sign only for Healing; chunk 11 lists damage-kind icons as 'built here' but none exist.

**Impact:** Players with red-green colour blindness cannot tell a poison hit from a burn hit or a heal from a poison effect at a glance. Colour is the only signal until chunk 11, whose icons and palettes are not built. The health bar (#D8574A) against the warning colour (#F08A5D) is also close, though the warning has text.

**Recommendation:** Pick defaults from a published colour-blind-safe palette (Okabe-Ito) with distinct luminance steps, and give each kind a non-colour cue now: a sign or glyph prefix on numbers, and distinct trail shapes. Check each default pair with a CVD simulation in a test.

**Verification:** I did not open ui_colours.hpp or damage_numbers.cpp. The chunk 11 spec reportedly plans icons and palettes. Whether the colours are confusable for CVD needs a simulation to decide. Colour-only distinction in the interim is plausible but I did not verify it.

### X-ACCESSIBILITY-08: Hover and selection outlines are distinguished by hue only, and the hover ring is near-white on a pale ground

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** accessibility · **Status:** new · **Reviewer confidence:** low
- **Where:** `assets/data/effects.yaml:45`

Hover is a 3-texel solid ring in #F4F4F0-ish near-white (UiColour::HoverOutline) and selected is amber. Both are the same width, steady, with blend ink, so they paint over the ground.

**Evidence:** effects.yaml `hover:` colour selection.hover, size 3; DEFAULT_UI_COLOURS hover 0xFFF0F4F4; GlowBlend::Ink comment 'reads on the pale paper'.

**Impact:** A near-white ring on pale paper has little contrast, and hover and selected differ only in hue (and not in thickness or pattern), so low-vision and colour-blind players may not see what they are about to select. Chunk 11 promises thickness and pattern options, but the defaults need a luminance contrast guarantee.

**Recommendation:** Give hover and selected different thicknesses or dash patterns by default, and a dark inner ring so the outline holds on both light and dark ground. Add a contrast test of each ring colour against the ground colours in `world_look.yaml`.

**Verification:** I did not inspect effects.yaml or the ground colours. Contrast of the near-white hover ring on pale paper needs a measurement against world_look.yaml. The reviewer's own confidence is low.

### X-ACCESSIBILITY-09: Photosensitivity: no global limit on flash rate or additive stacking, and no warning

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** accessibility · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/glow.hpp:69`

The only flash safeguard is MIN_PULSE_SECONDS = 1/3 s on pulse-driven glows, so a pulse of exactly three beats per second is allowed (WCAG's limit is no more than three flashes in any second, so this is the boundary). Hit flashes, sword halos, bursts and seals are additive light with a half-float target and bloom headroom, so stacked effects exceed 1.0. Nothing bounds the area or luminance change of many overlapping effects, thousands of bullets, or the poison lance flash.

**Evidence:** glow.hpp MIN_PULSE_SECONDS; effects.yaml sword_aura `blend: light` pulse period 1.6 depth 0.9; flash_intensity in effects.cpp; design 0002 §8 'stacked additive effects exceed 1.0'. The glow spec's reduce-flashing hook only covers pulses and light glows' opacity.

**Impact:** Dense combat with many overlapping additive effects can produce large-area, high-contrast brightness changes that a per-effect limit does not prevent. There is no photosensitivity warning on first launch (chunk 11's page offers reduce flashing but is not built).

**Recommendation:** Treat three a second as a refused value, not allowed (use a lower cap such as 2.5 Hz) and make the 'reduce flashing' implementation a real global limiter (per-frame luminance-delta clamp, or a cap on additive intensity per pixel) rather than per-effect scaling. Show the warning on the first-launch page. Measure peak frame-to-frame luminance change in a reference scene.

**Verification:** MIN_PULSE_SECONDS=1/3 s is real (glow.hpp:69, validated at :111, effects.cpp:191), so exactly 3 Hz is allowed. The claims about a global limiter and a first-launch warning are design gaps. Chunk 11 (lines 52, 62, 163-165) plans reduce flashing plus a first-launch essentials page, and none of it is built yet. A global luminance clamp is a reasonable suggestion but speculative. A 3 Hz pulse on a small glow is within WCAG. The real risk is stacked large-area additive effects, which no one has measured.

### X-ACCESSIBILITY-10: Chunk 8 and 10 specs promise a Back-button focus toggle and a pause menu that clash with the shipped bindings

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** spec-consistency · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/superpowers/specs/2026-11-10-phase6-chunk8-ui-design.md:152`

Chunk 8 says the gamepad's Back button moves focus between the game and the front window. bindings.cpp already binds PadBack to PauseWorld, and chunk 11 says Start always opens the pause menu, but no Start code exists. Chunk 8 and 10 have no Accessibility section although decision 17 (in the phase spec) requires one in every chunk spec. Per-window scale is Ctrl+wheel only, and windows can be moved only by mouse.

**Evidence:** chunk8 line 152 'the Back button moves focus from the game to the front window and back'; bindings.cpp `{ PadBack, PauseWorld }`; chunk8 line 154 'can't be dragged by gamepad'; grep finds no Accessibility heading in chunk 8 or 10.

**Impact:** A button promised two jobs will be resolved late, and probably against accessibility (the player loses their pause). The window layout, per-window scale and window drag are reachable only with a mouse and a modifier, against decision 18, which says nothing is mouse-only except dragging windows.

**Recommendation:** Add Accessibility sections to chunks 8 and 10. Assign Back/Start/B once, in a table, before any UI exists. Give window move, snap and scale a keyboard and gamepad route (a move mode with the d-pad, or fixed snap positions).

**Verification:** Chunk 8 line 152 does say Back moves focus between game and window, and bindings.cpp:93 binds PadBack to PauseWorld, so the conflict is real. It is an unapproved-yet-unbuilt spec detail, easy to fix when chunk 8 is built. Chunk 8's line 154 states 'can't be dragged by gamepad' as a deliberate, written choice. Grep finds no 'accessib' heading in chunks 8 or 10, but decision 17 (every chunk spec gets an Accessibility section from now on) was added by chunk 11 on the same date, so the absence is a timing artefact. Chunk 11 still wires the settings. Severity is overstated.
