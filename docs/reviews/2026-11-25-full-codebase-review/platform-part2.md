# Review: platform (part 2)

### PLATFORM-11: Reconnect: a pad present at start or pad events on a disconnected ID

- **Severity:** low (reviewer said info) · **Verdict:** partially-confirmed · **Category:** input · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/platform/window.cpp:101`

CloseGamepad uses SDL_GetGamepadFromID(which) on REMOVED, but axis or button events already queued for a removed pad still produce inputs, and a pad opened twice (ADDED can repeat for the same ID after SDL_InitSubSystem re-run) is pushed twice into the vector.

**Impact:** A stuck stick value after unplug (the last axis event before removal may be non-zero and no zeroing event follows), so the camera drifts or the player keeps walking until another pad event arrives. A duplicate open leaks one handle slot until destruction.

**Recommendation:** On removal, emit zero-valued axis events and release events for the pad. Guard OpenGamepad with SDL_GetGamepadFromID.

**Verification:** OpenGamepad (window.cpp:234-243) pushes without checking for an existing handle, and CloseGamepad is at :257. A duplicate ADDED for the same ID is unlikely, since SDL emits ADDED once per device. The stuck-axis-after-unplug claim is plausible: no zero events are synthesised on removal, but SDL generally emits a final axis-to-rest event only if the hardware sent one. I did not verify consumers' handling. The duplicate-open leak is speculative; the stuck-value concern is the real part.

### PLATFORM-12: Paths: user data in roaming AppData, assets folder never checked, no override for tests or portable mode

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/platform/paths.cpp:19`

UserDataDirectory uses SDL_GetPrefPath(nullptr, 'project_n'), which is %APPDATA% (Roaming) on Windows and so syncs in domains. AssetsDirectory returns base/'assets' without checking that it exists. There is no environment or command-line override.

**Impact:** Roaming profile sync of logs and saves (large replays) is bad on managed Windows. A missing assets folder (a bad install, or a run from an unusual build folder) surfaces later as a vague data-load error rather than at the point of cause. Tests and `measure` cannot redirect the data folder, so test runs write into the real user's save folder. Linux flatpak and snap work through SDL, but there is no XDG_STATE_HOME split for logs.

**Recommendation:** Add an override (env var PROJECT_N_DATA or CLI flag), check the assets folder in AssetsDirectory with a clear message, and consider a local-app-data folder for logs and caches.

**Verification:** paths.cpp:19-35 matches the description. UserDataDirectory uses SDL_GetPrefPath(nullptr, "project_n"), and AssetsDirectory returns base/"assets" with no existence check. No override exists. SDL_GetPrefPath uses %APPDATA% (Roaming) on Windows, which is true. The missing-assets error does surface later in the data load, which already stops the game with an error, so the harm is small. The claim that tests write to the real save folder was not verified. Design preference, low.

### PLATFORM-13: Paths tests are thin: UserDataDirectory untested, Windows ARM/macOS bundle and UTF-8 not covered

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** tests · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/platform/paths_tests.cpp:15`

One test covers AssetsDirectory (parent holds platform_tests). UserDataDirectory has no test (created, writable, absolute). FromUtf8 non-ASCII conversion has no test since it is in an anonymous namespace. The macOS bundle Resources branch is never checked.

**Impact:** A regression in the UTF-8 handling (a user named with non-ASCII characters on Windows) or the pref folder creation would not be caught. Per-OS CI runs would, but only for the one covered assertion.

**Recommendation:** Add a test that UserDataDirectory exists and is writable (with the override from the previous finding). Expose FromUtf8 or test through a path with a non-ASCII SDL_HINT-driven base.

**Verification:** grep for UserData under tests/unit/platform returned nothing, so UserDataDirectory has no test. FromUtf8 is in an anonymous namespace and is untested. The macOS Resources-branch claim was not checked, because paths.cpp as read has no macOS branch (it uses SDL_GetBasePath only). So that part is refuted. The remainder is a minor coverage gap.

### PLATFORM-14: Impl destructor logs after SDL_Quit and when gamepad subsystem init failed

- **Severity:** info · **Verdict:** unverifiable · **Category:** minor · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/platform/window.cpp:207`

The destructor calls SDL_Quit() then PN_LOG_INFO. The log routing function is installed via SDL, and SDL_Quit resets log output functions only in some versions. On a failed SDL_CreateWindow Open returns the error and the destructor logs 'SDL stopped' without ever logging a window.

**Impact:** Possible lost or misrouted last log line if SDL_Quit resets the callback. Cosmetic.

**Recommendation:** Log before SDL_Quit.

**Verification:** The destructor (window.cpp:226-227) does call SDL_Quit() and then PN_LOG_INFO. The log callback is installed by SDL_SetLogOutputFunction (sdl_log.cpp:35). Whether SDL_Quit resets it, or only the priorities, depends on the SDL3 version, and reading the repo cannot decide that. Whether PN_LOG goes through SDL's log at all was also not checked. Moving the log line before SDL_Quit is trivial and safe. The 'without ever logging a window' wording is just the intended 'SDL stopped' variant on line 227.

### PLATFORM-15: Per-event FamilyOf lookup and raw-axis events with no deadzone or normalised clamp for -32768

- **Severity:** info · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/platform/window.cpp:148`

Every axis motion event calls SDL_GetGamepadTypeForID (a locked device-list search). Sticks emit up to hundreds of events per second per axis. Deadzone is left to consumers, and -32768/32767 is clamped, which is fine.

**Impact:** Small wasted work per event; the family can be cached per joystick ID on connect in the Impl.

**Recommendation:** Cache the family in the gamepad list when it is opened.

**Verification:** AxisInput and PadButtonInput call FamilyOf(which) on every event (window.cpp:~131, 155). The facts hold. SDL_GetGamepadTypeForID is cheap and the event rate is low, so this is a micro-optimisation with no measured cost.

### PLATFORM-16: Window loses focus: held pad stick or buttons are not released

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** input · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/platform/window.cpp:356`

FOCUS_LOST only sets focused=false. SDL resets keyboard and mouse state on focus loss, but gamepad axes and buttons stay held, and no synthetic zero events are sent. Pad events keep arriving unfocused (background gamepad input is enabled by SDL's default hint on desktop is off).

**Impact:** The game pauses on focus loss (so the sim is frozen), but a stick deflected at that moment resumes as deflected when focus returns, until the player moves the stick. Mostly harmless; an edge case.

**Recommendation:** Document that the consumer must clear gamepad state on focus change, or emit zeros here.

**Verification:** FOCUS_LOST only sets state.focused=false (window.cpp:~355). No pad release is emitted, as stated. The finding itself calls the effect mostly harmless and rates its confidence low. Whether consumers clear the pad state on a focus change was not checked.

### PLATFORM-17: Window tests cannot catch real-driver behaviour; Open/fullscreen/display logic is tested only on the dummy driver

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** tests · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/platform/window.cpp:298`

All window tests run on SDL's dummy video driver. Fullscreen open, SDL_GetWindowSize after a fullscreen create (Wayland reports the size asynchronously, so state.width may be the requested size until a RESIZED event), multi-monitor display placement and the video-driver hint on Linux are untested. Open() reads the size once right after creation.

**Impact:** A fullscreen start on Wayland or macOS spaces can start with the wrong width/height for the first frame or frames. The `reference` and `measure` runs on one machine hide this.

**Recommendation:** Rely on the RESIZED and PIXEL_SIZE events (already handled) and add a test that fullscreen Open followed by a pushed RESIZED updates the state. Document the first-frames caveat.

**Verification:** Open reads SDL_GetWindowSize once (line 298) and SDL_EVENT_WINDOW_RESIZED updates it (line 330), so the single read and the RESIZED handler both exist. The claim that PIXEL_SIZE events are handled is wrong: grep finds no PIXEL_SIZE case in window.cpp. The Wayland first-frame risk is speculative and cannot be decided by reading. A real-driver run would decide it. A dummy-driver test of a pushed RESIZED event is feasible.

### PLATFORM-18: SDL log routing: priorities forced to DEBUG, overriding SDL_LOGGING; callback is noexcept but logger may throw or block

- **Severity:** info · **Verdict:** confirmed · **Category:** logging · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/platform/sdl_log.cpp:36`

SDL_SetLogPriorities(DEBUG) discards the user's SDL_LOGGING environment configuration. LogFromSdl is noexcept and is called from any thread (SDL audio, joystick, GPU worker threads). PN_LOG_* writes through quill (asynchronous, so fine), but any throw terminates.

**Impact:** A release build writes SDL debug chatter to the log file by default. Users cannot raise it to VERBOSE for a bug report. Low risk otherwise.

**Recommendation:** Apply DEBUG only when SDL_LOGGING is unset, or make the level a setting. The tests cover mapping thoroughly except the category and thread-safety aspects.

**Verification:** RouteSdlLogs unconditionally calls SDL_SetLogPriorities(DEBUG) (line 36), so an SDL_LOGGING environment variable cannot change the level. No docs or code mention SDL_LOGGING. Routing everything to the logger is plausibly intentional. The thread-safety and throw concern is speculative, since PN_LOG goes through quill. The impact is low.

### PLATFORM-19: Wheel input drops horizontal scroll and uses fractional notches with no accumulation policy

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** input · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/platform/window.cpp:132`

WheelInput passes wheel.y only (x ignored) and wheel.y is a float: smooth trackpads (macOS, Windows precision touchpads) produce many small fractional values. integer_y is ignored.

**Impact:** Zoom steps expressed 'per notch' behave differently per device: a trackpad zooms in tiny increments and a high-resolution wheel may send 1/120ths. Documented as 'notches turned' but consumers may assume integers. Horizontal scroll (shift-scroll, tilt wheels) is unavailable for UI lists.

**Recommendation:** Document the fractional range and have the camera's zoom use value as a continuous quantity (check app), or accumulate to whole notches in the platform layer.

**Verification:** WheelInput uses wheel.y only, as a float, and flips its sign for FLIPPED direction. Horizontal scroll (x) is dropped. The only consumer is Control::Zoom in src/app/bindings.cpp (lines 147-148), which passes input.value through as a continuous value. The doc in window.hpp says 'notches turned'. Fractional values are therefore not a bug if zoom treats them as continuous. I did not check the camera's handling of the value. The dropped x scroll is real but no UI list consumes a wheel yet.
