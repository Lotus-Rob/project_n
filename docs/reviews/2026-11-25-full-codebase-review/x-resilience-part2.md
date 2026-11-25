# Review: x-resilience (part 2)

### X-RESILIENCE-11: Window and display handling: occlusion/minimise state can contradict itself, and a swapchain-less frame busy-waits

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** window · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/platform/window.cpp:330`

SDL_EVENT_WINDOW_EXPOSED, SHOWN and RESTORED all unconditionally set visible=true, and OCCLUDED/HIDDEN set it false, independent of the minimised flag. An EXPOSED or SHOWN arriving while the window is still minimised makes visible true again. When SDL_WaitAndAcquireGPUSwapchainTexture succeeds with a null swapchain (window not presentable) the frame runs copy-only and returns at once, so the loop polls without waiting (only the fully 'hidden' state waits on events).

**Evidence:** window.cpp Poll switch: case SHOWN `state.visible = true`, EXPOSED `state.visible = true`, MINIMIZED sets both flags; renderer.cpp Frame() comment 'Without a swapchain texture ... the copy still runs and nothing is drawn.'

**Impact:** Wayland and some X11 compositors send EXPOSED or SHOWN around minimise/restore, which could make the game render or spin at full CPU while minimised or occluded, and on a laptop drain the battery. Window resize to 0x0 on minimise (Windows) is not guarded by the camera or CullArea, which divide by the window size.

**Recommendation:** Derive visible from (shown && !minimised && !occluded) as separate flags. Make the null-swapchain path sleep or wait on events (reuse hiddenWaitNs). Guard width or height == 0 in camera and cull maths. Test with a synthetic event sequence.

**Verification:** Confirmed in window.cpp: SHOWN and EXPOSED set visible=true unconditionally, even while minimised, and OCCLUDED and HIDDEN set it false. The code shows no ordering guard. Whether the compositors really send EXPOSED while minimised is unverified. I did not check the null-swapchain busy-wait or the 0x0 division, so the compositor behaviour and those two points are speculative.

### X-RESILIENCE-12: No handling for a monitor or display change beyond logging; window and settings size are never validated against the display

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** window · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/platform/window.cpp:349`

DISPLAY_CHANGED and DISPLAY_CURRENT_MODE_CHANGED only call ReadDisplay, which logs. The window opens at settings.windowWidth/Height (up to MAX_SIZE) without checking it fits the display (a laptop undocked from a 4K monitor). Fullscreen uses SDL_WINDOW_FULLSCREEN with no mode choice or fallback. Refresh rate changes go nowhere (the 60 or 165 Hz monitors the project's own notes mention).

**Evidence:** window.cpp Poll display-change cases; Window::Open passes the settings size straight to SDL_CreateWindow; settings.cpp MAX_SIZE bound only.

**Impact:** A window larger than the display opens off-screen with unreachable controls, on a settings file written on a larger monitor. No signal to the renderer that the swapchain format or size may have changed, which can matter with HDR toggles.

**Recommendation:** Clamp the opened size to the display's usable bounds and fall back to centred windowed. Re-query the display refresh to retune pacing, if it is used anywhere.

**Verification:** Window::Open takes settings.windowWidth and windowHeight directly. The settings are bounded only by MIN and MAX_SIZE, with no display check. The display-change events only call ReadDisplay. The off-screen window scenario is plausible, but the window manager usually constrains it. I did not verify the fullscreen mode fallback.

### X-RESILIENCE-13: Start-up opens the window before checking assets and data, and shows a message box only for start-up errors; the dialog omits the log location

- **Severity:** low · **Verdict:** confirmed · **Category:** user-facing-errors · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/main.cpp:53`

Run() opens the window first, then locates assets, loads the manifest, data, and so on. Any failure shows a window that was just created. ReportStartFailure's dialog text is '{what}:\n{error.message}', where error messages contain raw developer text such as file paths, YAML line numbers or 'run ./dev atlas'. They do not say where the log file is.

**Evidence:** main.cpp Run(): Window::Open first; FailToStart('the game's data couldn't be read', ...); window.cpp ReportStartFailure format string.

**Impact:** A flash of an empty window before the error on a broken install. The player is not told the log folder, which is the first thing support will ask for. 'the game's data couldn't be read: effects.yaml line 12: ...' is developer-grade text. Message box failure (headless or no video) falls back to a log line the user never sees.

**Recommendation:** Do the data and asset checks before opening the window where possible. Append 'Details are in <log path>' to the dialog. Offer to open the logs folder. Keep developer hints for the log.

**Verification:** main.cpp Run() calls Window::Open first, then AssetsDirectory, LoadManifest and the rest. Failures go through FailToStart and ReportStartFailure. The window-first order is as described. I did not read the dialog format string or check whether it names the log location. The 'flash of an empty window' is plausible but minor.

### X-RESILIENCE-14: Logging quality: asynchronous file log with no crash flush, no run-identifying header, and the DEBUG level always on in release

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** logging · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/log.cpp:71`

The root logger is set to Debug level unconditionally, so every release build writes Debug lines to the log file (including per-event lines such as 'swords first drawn ...' and 'scene change ... timeline ...'). No build version, git hash, OS, GPU name or driver version is logged at start-up beyond SDL, driver name and display. Logging to the file is best-effort and failure to open (read-only folder, full disk) silently falls back to console only, which for a GUI player means no log at all. Logs are not flushed periodically.

**Evidence:** log.cpp `logger->set_log_level(quill::LogLevel::Debug)`; main.cpp logs 'project_n starting' without the version though build_info.hpp.in exists; renderer.cpp logs the GPU driver but not the adapter name.

**Impact:** Support has to ask for version, OS and GPU separately. Debug volume in release can grow log files (rotation is per launch, not by size). If the GPU or driver is the cause, the log doesn't name it. The code mostly follows the 'log outcomes' preference, but several DEBUG lines are step logs.

**Recommendation:** Log version, commit, OS, CPU and GPU name and driver at the start of every run. Make the level a build default (Info in release, Debug in debug/releasedbg) with a --log-level or settings override. Add periodic or on-warning flush so that the file survives a hard kill.

**Verification:** log.cpp:68 sets the file logger to Debug level unconditionally. Line 42 filters the console to Info. So release builds do write Debug to the file, as claimed (the cited line is 68, not 71). I did not check the start-up header contents, the flush behaviour or the fallback when the file can't be opened. Those parts are unverified.

### X-RESILIENCE-15: Frame-time extremes: only labels and the tick catch-up are bounded; camera, damage numbers, cursor and animations take raw wall deltas

- **Severity:** low · **Verdict:** refuted · **Category:** frame-time · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/app/view.hpp:47`

FrameClock::Advance clamps to FRAME_STEP_LIMIT_NS, and TickClock caps catch-up at MAX_CATCH_UP ticks, but other per-frame consumers advance by their own wall deltas. A 5-second stall (a debugger break, laptop sleep or resume, a GPU hang, an 800 ms scene upload) is handled by TickClock dropping ticks, yet the app-side smoothing (camera, CameraControl, edge-pan) is only tested for 'hitch mustn't carry the cursor past its aim' in TargetCursor.

**Evidence:** view.hpp FrameClock doc; target_cursor.cpp:75 has a hitch guard; camera_control and the damage-number code were not found to clamp dt (not exhaustively verified). tick_clock.hpp MAX_CATCH_UP drops wall time beyond that.

**Impact:** After resume-from-sleep, a camera pan or zoom with momentum, or a fade, can jump by seconds of simulated input in one frame. Low risk, but there is no central 'frame delta cap' policy, so each new consumer must remember to add one.

**Recommendation:** Expose a single clamped frame delta from the app loop (the frame clock's step limit) and use it everywhere on the render side. Add a test that feeds one 10 s delta to each.

**Verification:** The central policy exists. view.hpp:23 defines FRAME_STEP_LIMIT_NS (100 ms) and FrameClock::Advance clamps to it (view.hpp:47-53). camera_control.cpp:13 clamps panning to the same limit. TickClock caps catch-up. The cursor has its own guard. The finding says clamping was 'not exhaustively verified', and what I checked contradicts it. I did not check damage-number aging, which is driven by tick-based render time.

### X-RESILIENCE-16: Logic-thread failure is signalled by an unsynchronised-looking flag and the main loop never stops the logic thread promptly

- **Severity:** info · **Verdict:** refuted · **Category:** thread-safety · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/app/app.hpp:306`

RunLogic sets m_logicFailed on exception and the main loop polls it each iteration. If the type is a plain bool rather than std::atomic<bool> (declaration not read here), that is a data race. After a logic failure the 'return 1' path destroys App, whose destructor joins the logic thread, which is already dead.

**Evidence:** app.cpp:301-309 `m_logicFailed = true;` and :326 `if (m_logicFailed) { return 1; }`

**Impact:** Probably benign in practice (the thread has exited before the join), but a TSan run would flag it if the field is not atomic. The logic thread also stops ticking with the window still alive for up to one frame.

**Recommendation:** Confirm m_logicFailed is std::atomic<bool>. If it is not, make it so.

**Verification:** app.hpp:306 declares `std::atomic<bool> m_logicFailed = false;`, so there is no data race. The finding's conditional ('if it is not atomic') does not apply. I did not check the claim about the destructor joining a thread that is already dead.
