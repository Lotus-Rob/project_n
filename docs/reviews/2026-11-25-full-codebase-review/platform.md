# Review: platform

**Scope:** src/platform/* (window, paths, clock, sdl_log). Consider Windows/macOS/Linux differences, HiDPI, multi-monitor, gamepad, IME. Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 19 standing (0 critical, 0 high, 1 medium, 12 low, 6 info), 0 raised but refuted on verification.

**Parts:** platform.md, platform-part2.md

## Assessment

The platform module is small (about 790 lines), clean and well documented. It keeps SDL out of the headers and handles the Impl lifetime carefully. The key mapping by scancode is right for AZERTY and Dvorak, the display-change logging has a pure, tested core, and SDL logs are routed into the game's logger. The unit tests are stronger than usual: they push synthetic SDL events through the dummy driver.

Weaknesses are about the real platform differences, which no test can reach.
- HiDPI is not handled. The window is opened without a pixel-density flag, and the game treats window coordinates as pixels everywhere.
- There is no SDL app metadata, no window minimum size, no text input or IME, and no modifier or trigger input.
- With several gamepads connected, their axes are merged into one stream.
- `visible` and `PausesGame` disagree for a window that is occluded or hidden but still focused.
- `Window` is not a safe singleton: two live windows would call SDL_Quit twice.
- The paths tests are thin.

No critical bugs. Overall health is good for a prototype. The medium items are the ones to fix before a Windows or macOS release.

Findings are ranked by likelihood of biting; none are exploitable. Everything is from reading the code. I could not run anything.

**Files read in full:** (not reported)

## Findings

### PLATFORM-01: No HiDPI support: window opened without SDL_WINDOW_HIGH_PIXEL_DENSITY, window size used as pixel size

- **Severity:** medium · **Verdict:** confirmed · **Category:** platform · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/platform/window.cpp:289`

SDL_CreateWindow is called with only RESIZABLE (and FULLSCREEN). WindowState width/height are window coordinates (SDL_GetWindowSize), and app.cpp/gfx use them both for mouse mapping and as the projection and swapchain size. There is no SDL_GetWindowSizeInPixels, display scale or pixel density anywhere in src.

**Impact:** On macOS Retina and on Wayland or Windows with display scaling, the swapchain is the logical size and the OS upscales it, so the game is blurry. SDL_WINDOW_HIGH_PIXEL_DENSITY is not set. If it is added later, the coordinate-equals-pixel assumption breaks and mouse picking and the HUD (which uses state.height as pixels) go wrong. Accessibility-first also needs a UI scale. The DisplayMode log does not record density or scale either.

**Recommendation:** Decide the policy now: request HIGH_PIXEL_DENSITY and carry a separate pixelWidth/pixelHeight and content scale in WindowState (update on SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED and DISPLAY_SCALE_CHANGED), keeping mouse coordinates in window units. Add a test with a pushed pixel-size event.

**Verification:** SDL_CreateWindow uses only RESIZABLE/FULLSCREEN. app.cpp uses state.width/height (window units) for mouse mapping and sizes. No pixel-density handling and no mention in docs. The claim holds. It is a missing feature with no recorded decision, so medium is fair.

### PLATFORM-02: Multiple gamepads merge into one input stream with no device identity

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** input · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/platform/window.cpp:108`

Every connected pad is opened (OpenGamepad pushes all of them) and AxisInput/PadButtonInput emit events with only a family and no joystick ID. Axis events are absolute values.

**Impact:** Two pads connected (or a pad plus a phone or Steam virtual pad, common on Linux) interleave absolute axis values. A resting pad's 0 overwrites the active pad's deflection, so the stick judders or cancels. Button release from one pad can release another's press. Hot-plug also leaves no way to say which pad is player 1.

**Recommendation:** Carry the SDL_JoystickID in InputEvent, or track a single active pad (last one used) and ignore the others until it disconnects. Test with two pushed axis events from different `which`.

**Verification:** AxisInput and PadButtonInput use only the family and code. The event's `which` is dropped, and OpenGamepad opens every pad. Absolute axis values from several pads therefore interleave. This needs two pads at once, so it is rare. Severity is low to medium.

### PLATFORM-03: visible=false (occluded/hidden) while focused does not pause the game but makes Poll block

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** logic · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/platform/window.cpp:342`

HIDDEN and OCCLUDED set visible=false. PausesGame only considers focused and minimised. While !visible, Poll uses SDL_WaitEventTimeout(hiddenWaitNs), which app.cpp sets to TICK_NS.

**Impact:** A window that is occluded (macOS covered by another space, Wayland suspended, some Windows cases) yet focused keeps simulating. Each frame then blocks up to a tick waiting for an event, so frame cadence and `m_clock` catch-up behave oddly. The visible flag also latches false if a platform sends OCCLUDED without a later EXPOSED or SHOWN. WINDOW_EXPOSED sets visible=true even while minimised (minimised stays true), giving the inconsistent state visible && minimised.

**Recommendation:** Derive visible from minimised, hidden and occluded flags kept separately, and decide explicitly whether occluded pauses. Test the combinations (minimise, expose, restore).

**Verification:** The code matches the description. HIDDEN and OCCLUDED set visible=false, EXPOSED sets visible=true even while minimised, and PausesGame (window.hpp:166) uses only focused and minimised. The impact is speculative: whether SDL sends OCCLUDED while focused varies by platform, and the later EXPOSED or SHOWN probably clears the flag. The visible && minimised combination is real but mostly harmless.

### PLATFORM-04: hiddenWaitNs/1e6 cast to i32 can wrap negative, meaning wait forever

- **Severity:** low · **Verdict:** confirmed · **Category:** integer · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/platform/window.cpp:314`

static_cast<i32>(hiddenWaitNs / SDL_NS_PER_MS) on a u64. Values of 2^31 ms (about 24.8 days) or more wrap to a negative number, and SDL_WaitEventTimeout treats a negative timeout as infinite.

**Impact:** Unreachable with today's caller (TICK_NS), but the API takes any u64 and is documented as 'up to hiddenWaitNs'. A caller passing UINT64_MAX would hang a hidden window forever and never see a quit request.

**Recommendation:** Clamp to INT32_MAX before the cast. Add a one-line test.

**Verification:** The cast is static_cast<i32>(hiddenWaitNs / SDL_NS_PER_MS) with no clamp, so values of 2^31 ms or more wrap. The only caller passes TICK_NS, so this is latent, not a live bug.

### PLATFORM-05: No SDL_SetAppMetadata / app id: window class, Wayland app_id and pref path identity are SDL defaults

- **Severity:** low · **Verdict:** confirmed · **Category:** platform · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/platform/window.cpp:274`

Nothing calls SDL_SetAppMetadata or sets SDL_HINT_APP_NAME or APP_ID. SDL_GetPrefPath is called with a null org.

**Impact:** On Linux the Wayland app_id and X11 WM_CLASS are generic ('SDL_App'), so compositors cannot match a .desktop file or window rules, and taskbar grouping and icon fail. Windows shows the generic window class, and macOS and screensaver-inhibit dialogs use a generic name. The pref folder has no organisation/publisher part, which cannot be changed later without moving saves. Also no window icon is set.

**Recommendation:** Call SDL_SetAppMetadata (name, version, identifier) before SDL_Init, and set an icon. Pick the pref-path organisation now, before saves exist.

**Verification:** grep finds no SDL_SetAppMetadata. paths.cpp:19 calls SDL_GetPrefPath(nullptr, "project_n"), so the org is null. The save-path identity is a fair point, and pre-1.0 changes are allowed anyway.

### PLATFORM-06: No minimum window size; resizable window can reach 0x0 or tiny sizes

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/platform/window.cpp:289`

The window is RESIZABLE with no SDL_SetWindowMinimumSize. WindowState width/height are copied from RESIZED events unchecked. Consumers clamp with std::max(...,1) in some places only.

**Impact:** Dragging the window very small, or a minimise reporting 0x0 (Windows), gives width/height of 0 or 1 to the camera, HUD and swapchain code. Division by zero or zero-sized swapchain and render targets are possible in code that does not clamp. The call sites that were clamped show the problem was noticed piecemeal.

**Recommendation:** Set a minimum window size (and keep it in sync with the UI's minimum layout). Optionally clamp WindowState to at least 1x1 and document it.

**Verification:** There is no SDL_SetWindowMinimumSize and RESIZED data is copied unchecked. app.cpp:868 and :872 clamp with std::max(..., 0), which still allows 0, and I did not check the 0-size handling in gfx. Real SDL windows rarely report 0x0 except when minimised, and a minimised window pauses the game. The risk is plausible but unproven.

### PLATFORM-07: Clock zero is not stable across SDL_Quit/SDL_Init

- **Severity:** low · **Verdict:** unverifiable · **Category:** correctness · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/platform/clock.cpp:16`

NowNs wraps SDL_GetTicksNS, whose epoch SDL resets when the library quits (SDL_Quit tears down the ticks base). Window's destructor calls SDL_Quit. NowNs is also used before any SDL_Init (app.cpp m_startNs).

**Impact:** The header promises a monotonic clock shared by every thread, but a second Window (tests, a future restart or recreation of the window) may make NowNs jump backwards. Stored deadlines (m_startNs, m_nextBurstNs, the clock NextNs) would then wait or never fire. Only the unit tests currently open several windows, but WaitUntilNs(0)-style assumptions rely on it.

**Recommendation:** Use std::chrono::steady_clock with a process-static epoch (or SDL_GetPerformanceCounter) and keep SDL only for the precise delay. Document the guarantee. Check SDL's behaviour on the pinned version.

**Verification:** NowNs is a bare SDL_GetTicksNS, and Impl's destructor calls SDL_Quit (window.cpp:226). Whether SDL3 resets the ticks base on quit depends on the pinned SDL version. In my recollection the base is not reset. Reading the code cannot settle this. A test that calls NowNs across Quit and Init would.

### PLATFORM-08: Window is not guarded against more than one live instance; SDL_Quit in Impl destructor

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** lifetime · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/platform/window.cpp:226`

Each Impl calls SDL_Init(VIDEO) in Open and SDL_Quit() in its destructor. SDL_Quit tears down every subsystem regardless of reference counts. The header says closing 'stops SDL'.

**Impact:** If gfx, audio or a future module also initialises SDL subsystems (audio is planned), destroying the Window kills them with SDL_Quit instead of SDL_QuitSubSystem. A second concurrently open Window would break the first on destruction. Moved-from Windows have a null impl, so Poll, Cursor and Handle dereference null (documented as 'empty' but unchecked).

**Recommendation:** Use SDL_QuitSubSystem for the subsystems this class initialised, or make SDL's lifetime a separate owner object. Assert the single-instance rule.

**Verification:** Impl's destructor calls SDL_Quit, and SDL_Init(VIDEO) in Open (line 280) pairs with it, so a second live Window would be broken by the first one's destruction. Only one Window is used at a time, and the unit tests open them in sequence. The audio and gfx subsystems may be affected later. The point about moved-from Windows dereferencing null is a normal moved-from-object contract and not a defect.

### PLATFORM-09: No text input / IME support and no modifier or trigger inputs

- **Severity:** low · **Verdict:** deliberate · **Category:** input · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `src/platform/window.cpp:182`

InputOf handles keys, mouse buttons, wheel, axes and buttons only. SDL_StartTextInput, TEXT_INPUT and TEXT_EDITING are never used. Shift, Ctrl and other modifier keys, the triggers, left-stick click, Start/Guide, X/B face buttons, horizontal wheel, touch and mouse motion are not mapped. Only Alt is sampled on mouse buttons.

**Impact:** Any future text field (character names, save names, search, chat in an NPC game) cannot take IME input (CJK, which the cultivation genre audience will use) or dead keys. The UI is currently keyboard-hardcoded, so remapping is deferred. Pad triggers are dropped, so Xbox or PS pads miss the natural cast and dash inputs. Mouse-only or gamepad-only accessibility needs the missing buttons and a cursor emulation path.

**Recommendation:** Plan an input layer with raw text events plus SDL_SetTextInputArea, and design the text-input policy when the UI module lands. Map the remaining pad inputs with the rebinding work. Make the InputCode table data-driven.

**Verification:** InputOf (window.cpp:~165-180) maps only the listed inputs, and the mapping is hardcoded as the finding says. docs/design/0002.3-input-data-scenes.md:58 and :241-242 defer rebinding and bindings-as-data to later. A text field and IME have no consumer yet, so they are a future need rather than a defect. The claim about unmapped triggers, Start and X/B is only partly checked: the visible button table includes NORTH, shoulders and the dpad. Rated low/info.

### PLATFORM-10: Alt state sampled at poll time, not at event time

- **Severity:** low · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/platform/window.cpp:119`

MouseButtonInput reads SDL_GetModState() while draining the queue, not the modifier state of the moment the click happened. SDL's mouse button events carry no modifiers.

**Impact:** If Alt is pressed or released between the click and the next Poll (a frame, longer with hitches), an alt-click (a feature used for something in app) is misread. Under a stall the window of error is the stall length. Also a click delivered during the hidden-wait path. The test sets the mod state by hand and so cannot show this.

**Recommendation:** Track modifier key down and up from the key events in Poll in queue order, and attach that state to the mouse event.

**Verification:** MouseButtonInput (window.cpp:97) reads SDL_GetModState() at drain time, so it is not the modifier state at event time. The mismatch only arises when Alt changes between the click and the poll, which is rare, and the effect is a misread alt flag. Impact is overstated for typical frame lengths, so low.
