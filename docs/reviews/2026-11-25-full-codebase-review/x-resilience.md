# Review: x-resilience

**Scope:** CROSS-CUTTING failure handling over src/: missing/corrupt assets, GPU device creation failure or loss, out of disk on save, unwritable paths, window minimised/alt-tab/monitor change, extreme frame times, logging quality (the project wants outcomes logged, not steps), crash reporting, user-facing error messages.

**Findings:** 14 standing (0 critical, 0 high, 6 medium, 8 low, 0 info), 2 raised but refuted on verification.

**Parts:** x-resilience.md, x-resilience-part2.md

## Assessment

Scope read: main.cpp, core/log.{hpp,cpp}, core/assert.cpp, platform/{window,paths,sdl_log}.cpp, core/{save_file,saves,jobs,repeated_failure}.cpp/hpp, the swapchain/frame/renderer-create/scene-load paths in gfx, App::Run/Present/LoadScene/RunLogic/WatchData, Logic save/load/replay paths, settings and YAML IO.

Strengths. Start-up failures are handled well. Every load step returns std::expected, and ReportStartFailure logs the failure and shows a message box. Save files are well defended: magic, version, size bounds, a zstd frame-size check before allocating, a checksum, and a write to a tmp file followed by a rename. Logging is decent: Quill on a background thread, ten rotated files, SDL logs routed into it, asserts flushed before abort, RepeatedFailure collapsing per-frame failures to a start and an end line, and one catch-all around both the main thread and the logic thread. The window, minimise and focus handling pauses the game, drops drags, and waits on events while hidden. Settings fall back to defaults with logged warnings.

Weaknesses. Runtime failures, as opposed to start-up failures, are consistently log-only. There is no crash handler (no signal, SEH or terminate hook). GPU device loss and swapchain failures are never treated as fatal or recoverable. The save write is not durable (no fsync, no previous-copy backup) and a failed save is invisible to the player. A mid-run fatal error (scene load failure, logic thread exception) quits with exit code 1 and no user-facing message. Exceptions thrown on enkiTS workers or background jobs reach std::terminate with nothing logged.

Overall health for a pre-1.0 game: good start-up and file-format hygiene, with the runtime resilience layer (crash capture, user-visible errors, device-loss policy, durable saves) still missing. These matter once there are players.

**Files read in full:** (not reported)

## Findings

### X-RESILIENCE-01: No crash handler: fatal signals, SEH and std::terminate lose queued log lines and leave no crash record

- **Severity:** medium (reviewer said high) · **Verdict:** confirmed · **Category:** crash-reporting · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/log.cpp:34`

Log::Start uses quill::Backend::start(), not a signal-handler variant. Only PN_ASSERT flushes the log before aborting. There is no std::set_terminate, no SIGSEGV/SIGABRT/SIGFPE handler on Linux/macOS, no SetUnhandledExceptionFilter or minidump on Windows, and no crash-report or dump folder.

**Evidence:** grep finds the only abort in core/assert.cpp (AssertFailed, which calls log::Root()->flush_log() then std::abort()). main.cpp catches C++ exceptions only on the main thread; App::RunLogic catches them only on the logic thread.

**Impact:** A segfault, a stack overflow, std::bad_alloc inside noexcept code, or an uncaught exception on a worker/background thread kills the process. The last milliseconds of log output (the most useful part, since Quill is asynchronous) are lost, and the player has no crash artefact to send. Diagnosis is limited to whatever reached disk already.

**Recommendation:** Use Quill's signal-handler-enabled backend start (Backend::start_with_signal_handler) or install a handler that flushes the log. Install std::set_terminate that logs the active exception and flushes. On Windows add a minidump writer. Write a crash marker into the logs folder and tell the player on next start where the log is.

**Verification:** core/log.cpp:39 and :84 call plain quill::Backend::start(); grep for set_terminate, SetUnhandled and signal( over src finds nothing. Only PN_ASSERT flushes. Core claim holds. High is too much for a pre-1.0 game with no crash-report plan on record, so medium.

### X-RESILIENCE-02: Exceptions inside enkiTS tasks and background jobs are not caught: std::terminate with nothing logged

- **Severity:** medium (reviewer said high) · **Verdict:** confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/jobs.cpp:105`

BackgroundTask::ExecuteRange runs m_work() with no try/catch, and Pending::Task::ExecuteRange / ChunkTask do the same for chunk bodies. The save lambda in Saves::Write allocates large vectors (compressBound plus a copy of the whole file) and does filesystem and stream operations. std::bad_alloc, or a std::filesystem call that throws (path::string on Windows for non-ANSI paths, remove() overloads), propagates out of a worker thread.

**Evidence:** core/jobs.cpp:105 `m_work();` ; core/saves.cpp:19 lambda calls WriteSaveFile with no handler. Only RunLogic and main have try/catch.

**Impact:** A failing save (low memory, odd path) terminates the whole game instead of logging 'couldn't save'. This is worse than the std::expected design intends. Chunk bodies throwing on worker threads likewise abort silently, and the logic thread's catch never sees them.

**Recommendation:** Wrap work in BackgroundTask::ExecuteRange and in chunk execution with catch(...), log CRITICAL, and either store a std::exception_ptr to rethrow at the join (so RunLogic's handler sees it) or report failure to the caller. At minimum wrap the save lambda.

**Verification:** jobs.cpp:44, 81 and 127 call chunk bodies and m_work() with no try/catch. main.cpp and RunLogic catch only on their own threads, so a throw on an enkiTS worker reaches std::terminate. The save lambda does allocation and filesystem work, so bad_alloc is possible. WriteSaveFile may catch internally via error_code overloads, which I only partly checked, so the real chance of a throw is low. Medium.

### X-RESILIENCE-03: GPU device loss or persistent frame failure is never treated as fatal or recovered; the game spins forever with a dead window

- **Severity:** medium (reviewer said high) · **Verdict:** partially-confirmed · **Category:** gpu-resilience · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/renderer.cpp:1313`

Renderer::Acquire and Renderer::Impl::Report only log the first failure through RepeatedFailure; Present() never returns false on an acquire, draw or submit failure. There is no device-lost handling, no swapchain recreation, no failure counter that escalates to quit-with-message, and no fallback.

**Evidence:** renderer.cpp Acquire(): `if (!acquired) { m_impl->Report(...); return; }`. Draw() returns early. Frame() 'still submits' and returns Combine(drawn, submitted), but Report only logs 'frames are failing'. App::Run only exits when Present returns false, which only LoadScene/prepare produce.

**Impact:** After a driver reset, TDR, GPU hot-unplug, or an eGPU/laptop GPU switch, SDL_WaitAndAcquireGPUSwapchainTexture or submit fails every frame. The main loop then runs with a black or frozen window, no user message, and (when acquire fails before waiting on vsync) potentially a hot loop burning a core. The simulation keeps ticking invisibly. The player must kill the process.

**Recommendation:** Count consecutive failed frames (RepeatedFailure already counts). After N or a time threshold, show the 'graphics card stopped working' dialog and exit cleanly, saving first if possible. Better: rebuild the device and renderer, since asset and sprite state is reloadable. Also throttle the loop (sleep/poll) while acquire fails.

**Verification:** renderer.cpp:1570-1575 shows Acquire only reports on failure and returns, and the failure path is RepeatedFailure-throttled logging. I found no device-lost handling or escalation to quit. The 'hot loop' point is unverified: SDL's wait-and-acquire normally paces. Real device loss is rare, and nothing in the docs records a decision on it. Medium.

### X-RESILIENCE-04: Fatal runtime failures (scene load failure, logic thread exception) exit with status 1 and no message to the player

- **Severity:** medium · **Verdict:** confirmed · **Category:** user-facing-errors · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/app.cpp:806`

Start-up failures go through ReportStartFailure (log plus message box), but a scene that can't be loaded mid-run (LoadScene returns false, so PresentFrame returns false and App::Run returns 1) and a logic thread failure (m_logicFailed, so App::Run returns 1) only log CRITICAL.

**Evidence:** app.cpp:807 `PN_LOG_CRITICAL("the scene {} couldn't be loaded..."); return false;` and `if (m_logicFailed) { return 1; }` with no platform::ShowError. main.cpp's catch blocks also only log.

**Impact:** A corrupt or missing sprite PNG (DecodeSprite error), an out-of-memory during upload, or a logic exception makes the window vanish. The user sees a crash with no hint, and does not learn that a log exists or where. Any save made since the last quick-save is also lost, since there is no emergency save.

**Recommendation:** Route these through ReportStartFailure-style reporting (ShowError with the log folder path). Include the log path in the dialog text. Consider an emergency save of the last good snapshot on a logic failure.

**Verification:** app.cpp:806-808 only calls PN_LOG_CRITICAL and returns false. ShowError is called only through ReportStartFailure from main.cpp:61 (start-up). m_logicFailed also only logs. No dialog or emergency save exists, so the claim holds.

### X-RESILIENCE-05: Save write is not durable: no fsync, no previous-save backup, and overwrite only by rename

- **Severity:** medium · **Verdict:** confirmed · **Category:** save-integrity · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/save_file.cpp:79`

WriteSaveFile writes slot.tmp, flush()es the stream and renames it over the slot. flush() only moves data to the OS cache, so there is no fsync/FlushFileBuffers on the file and none on the directory. The previous good save is destroyed by the rename, so a bad new save cannot fall back to it.

**Evidence:** save_file.cpp lines ~79-95: ofstream write, `output.flush()`, then std::filesystem::rename(temporary, path). No sync call and no .bak.

**Impact:** On power loss or a crash shortly after rename, journalling filesystems can expose a zero-length or partial save file, even though the rename was atomic. Checksum and size checks make it load as 'broken' and the player loses the only save, because there is one slot and no backup. The one save slot makes this a single point of failure.

**Recommendation:** Sync the tmp file before the rename (fsync via a POSIX/Win32 wrapper) and ideally the parent directory. Keep the previous save as quicksave.pns.bak (or rotate N) and let Load fall back to it when the primary is broken.

**Verification:** save_file.cpp:83 does output.flush() and line 90 does std::filesystem::rename. There is no fsync and no .bak. The risk is real but narrow: a crash or power loss right after the rename. Medium-low.

### X-RESILIENCE-06: Save and load failures are log-only: the player is never told a save failed (disk full, unwritable folder) or a load was refused

- **Severity:** medium · **Verdict:** confirmed · **Category:** user-facing-errors · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/saves.cpp:19`

The async save job logs success or failure but nothing reaches the UI or HUD. Logic::Load returns false and logs 'not loaded'. Save with an empty slot ('no folder for saves') only warns.

**Evidence:** saves.cpp:21-26 `PN_LOG_ERROR("{}", written.error().message)` inside the background lambda; Logic::Load logs and returns false; hud.cpp shows no save/load status line (grep for 'saved' finds nothing in app/hud).

**Impact:** With a full disk, a read-only profile folder, antivirus locking the .tmp, or OneDrive-redirected folders, F5 silently does nothing and the player believes they saved. A refused load (wrong version) also shows nothing. For a sandbox RPG with long-lived worlds, this is data loss by silence. It also ties into the accessibility priority, since no feedback is offered through any channel.

**Recommendation:** Add a transient, accessible notification (text plus sound/gamepad rumble as options) for 'saved', 'couldn't save: reason' and 'couldn't load: reason'. Carry the async save result back to the main thread through a handover event. Distinguish the 'folder missing' case from 'disk full' in the message.

**Verification:** saves.cpp:19-26 only logs success or failure inside the background lambda. Saves::Write with an empty slot only warns. Nothing sends the result back to the UI. Because the project ranks accessibility first and the user would see no feedback on a failed save, medium is right.

### X-RESILIENCE-07: One missing or corrupt sprite makes the whole scene fatal; no placeholder or degraded path

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** asset-resilience · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/renderer.cpp:1497`

ContinueScene aborts the whole scene load on the first failed decode or upload (`impl.load.reset(); return std::unexpected`). The manifest is trusted at start-up, but the individual PNGs are only opened lazily when their scene loads, so a damaged install fails late. DecodePng does no dimension cap before SDL_ConvertSurface, so a hostile or corrupt PNG can request a huge allocation.

**Evidence:** renderer.cpp ContinueScene `if (!uploaded) { ... impl.load.reset(); return std::unexpected{...}; }`; image.cpp DecodePng; sprite_array.cpp DecodeSprite checks dimensions only after the full decode.

**Impact:** A single bad file (partial download, disk corruption, antivirus quarantine) is not detected at start-up, only when that scene is entered, then ends the game (see the finding on silent exits). Mod or user-replaced art would be unsafe. The error text 'run ./dev atlas' is a developer instruction shown in a player-facing context.

**Recommendation:** Verify every manifest file exists and has the right size at start-up (cheap stat/header read) so damage is reported before play. Optionally substitute a magenta placeholder sprite and log rather than abort. Read the PNG header and check the dimensions before decoding. Keep developer hints out of dialog text.

**Verification:** ContinueScene does fail the whole scene on the first bad decode, and that leads to the silent exit in finding 3. There is no placeholder path. The claim that the manifest is not checked up front is plausible, but I did not verify it. The PNG dimension-cap point is speculative, because the assets are the game's own committed files and hostile input is not a realistic threat. Low, and it mostly folds into finding 3.

### X-RESILIENCE-08: Save()/Write() blocks the logic thread when a previous background job (save or replay write) is still running; Load reads synchronously on the logic thread

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/jobs.cpp:206`

Jobs::Background calls WaitForBackground() first, so a second save (or a replay write finishing right after a save) blocks the caller until the previous job completes. Only one background slot exists for saves, replays and any other background work. Saves::Read also waits for the background and then does disk I/O plus decompression inline on the logic thread.

**Evidence:** jobs.cpp:206-211 `WaitForBackground(); ... AddTaskSetToPipe`. saves.cpp Read(): `m_jobs->WaitForBackground(); return ReadSaveFile(m_slot);`. FinishRecording calls m_replays->Write, then a Save shortly after queues behind it.

**Impact:** On a slow or stalled disk (network home folder, HDD, antivirus scan) the logic thread stalls for the whole write, and the tick clock drops ticks after a hitch. Mashing F5 or stopping a recording right after a save causes visible hitches. A hung write at shutdown shows as a hang with no timeout.

**Recommendation:** Give the writer its own serialised queue (or one slot per kind) so Write returns immediately and coalesces to the newest state, and run reads off the logic thread, with the game paused or a 'loading' state. Add a bounded wait at shutdown with a log line.

**Verification:** jobs.cpp:206 shows Background() calls WaitForBackground() first, and Saves::Read waits then reads synchronously. A single slot is confirmed. The stall only occurs when a second background job starts while the first is still running, which is rare given saves are manual. Reads on load are expected to block. The shutdown-hang claim is speculative. Low.

### X-RESILIENCE-09: Save and replay temp file name is fixed (slot.tmp): leftovers and concurrent writers are unhandled

- **Severity:** low · **Verdict:** confirmed · **Category:** save-integrity · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/save_file.cpp:79`

The temp path is always path+'.tmp', and a stale one from a crashed run is simply truncated and reused. The single-slot serialisation relies on Jobs::Background waiting. If another process (a second game instance, a cloud-sync tool) holds the file, the open fails and the write is reported only as 'the write or flush failed' with no OS error text.

**Evidence:** save_file.cpp `temporary += ".tmp"`; `if (!output) return FileError(..., "the write or flush failed")` carries no errno/strerror.

**Impact:** Low: the error text is not diagnostic (disk full vs permission vs sharing violation are indistinguishable in the log), and two instances sharing the user folder can corrupt each other's temp file. There is no single-instance guard.

**Recommendation:** Include errno/GetLastError or std::error_code in the message (open with an error-checked API, or check std::filesystem::space() to report 'disk full'). Consider a single-instance lock on the user-data folder.

**Verification:** Checked save_file.cpp. The temp path is fixed as path+.tmp, opened with trunc. The failure message is the constant 'the write or flush failed' with no OS error text. There is no single-instance guard that I saw. The impact is minor and the claim holds.

### X-RESILIENCE-10: Settings written non-atomically and incompletely; any invalid value discards all settings

- **Severity:** low · **Verdict:** confirmed · **Category:** settings · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/yaml.cpp:19`

WriteText truncates the target in place (no tmp and rename). SaveSettings does not write edge_pan although ParseSettings reads it. ParseSettings returns an error for an out-of-range window size, and LoadSettings then substitutes every default, including language and accessibility options like text_outline and text_shadow, while leaving the file as it is.

**Evidence:** yaml.cpp WriteText ofstream trunc; settings.cpp SaveSettings format string has no edge_pan line; settings.cpp: window bounds check returns std::unexpected for the whole file.

**Impact:** A crash while the first-run defaults are being written leaves a truncated file (parsed as defaults, so fairly benign). A player with accessibility settings (outline, shadow, language zh) who mistypes a window size silently loses all of them for the session, with only a log warning. The edge_pan omission means a regenerated settings file hides an option.

**Recommendation:** Clamp or ignore only the bad key and warn, keeping the other settings. Write settings via tmp and rename. Write every key SaveSettings knows. Show a one-time in-game or dialog notice when a settings file was rejected.

**Verification:** All three facts hold. WriteText truncates in place. SaveSettings writes no edge_pan, although ParseSettings reads it (settings.cpp:137). An out-of-range window size returns std::unexpected for the whole file (settings.cpp ~145), so the other settings are discarded. I did not check the LoadSettings fallback or whether a notice is shown. Keep it low.
