# Review: x-untrusted-input

**Scope:** CROSS-CUTTING security/robustness: every place bytes from disk or the environment are parsed or trusted (saves, replays, YAML, images/sprites, fonts, manifest, settings, paths, command line, env vars). Hunt: OOB, integer overflow in sizes, unbounded allocation, path traversal, symlink/TOCTOU, decompression bombs, format-version confusion, partial writes.

**Findings:** 25 standing (0 critical, 0 high, 2 medium, 18 low, 5 info), 0 raised but refuted on verification.

**Parts:** x-untrusted-input.md, x-untrusted-input-part2.md, x-untrusted-input-part3.md

## Assessment

## Untrusted input across project_n (saves, replays, YAML, manifest, images, fonts, settings, paths, command line, environment)

**Overall health: good, with one serious exception.** Most of the code that reads untrusted bytes was clearly written with an attacker in mind, and a few blind spots remain. The worst problem has nothing to do with malformed input: it is how Windows file paths are encoded. It can stop the game from starting on Windows for a large share of the audience this genre targets.

### Strengths
- **Archive reader (`core::Reader`).** It is fail-sticky and checks bounds on every read. A vector reserves only as much as the remaining bytes could hold. Enums and bools are checked by the code that reads them, and each component has a `Valid()` hook. `LoadDomain`, `LoadDomainDelta` and `LoadDomainOrder` are careful: they catch duplicate IDs, missing entities, permutation checks and masks past the field count.
- **Save container.** The magic number, version, size checks and the zstd frame-content-size check all happen before anything large is allocated. A non-zstd checksum covers the contents. Writes go to a temporary file that is then renamed over the old save, and tests cover truncation, corruption, huge claimed sizes and wrong versions.
- **Host::Load.** It rejects anything left over after the state, and checks the world as a whole after reading: NPC placement, clock, fog bits, route soundness, relationship endpoints, and flashes or seals from the future.
- **Replay reader.** It refuses action kinds that are never recorded, checks held input and finite points, and requires hashes in order and nothing left over at the end.
- **YAML readers.** Every reader catches `YAML::Exception`. Numbers are read as f64 with finite and range checks, and most lists are capped.
- **Fuzzing.** There are three libFuzzer targets (save, replay, all YAML readers) with seed corpora that are replayed as tests. That is unusually good for a project at this stage.
- **SDL paths.** Paths handed to SDL are converted properly in `gfx/image.cpp` and `platform/paths.cpp` (`u8string`).

### Weaknesses
- **Windows path encoding (high).** `std::filesystem::path::string()` is used both for an SDL path (the font) and inside log arguments. On MSVC this either gives text in the ANSI code page, which is not UTF-8, or throws `std::system_error`. The results:
  - The font fails to open when the install folder is not plain ASCII (for example `D:\游戏\`).
  - Startup aborts when the Windows profile path cannot be represented in the code page (settings logging, data folder logging).
  - A finished save inside the enkiTS background job can call `std::terminate`.
  - `log.cpp` already documents this hazard for Quill, but the other call sites ignore it, and no test uses a non-ASCII path.
- **Allocation amplification still left after the archive's careful reservation.**
  - `Combat::Load` value-initialises the volley targets up to `Remaining()/8` entries before reading them.
  - `Travel::LoadDelta` rebuilds a full journey from every ~41-byte plan before it checks that members exist and are unique. A plan can grow into tens of kilobytes of route.
  - The replay reader's `ReadTicks` (already known as Audit 2).
  - The save cap of 1 GiB uncompressed, reachable from a ~32 KiB file, multiplies all of these.
- **Asset trust is uneven.** The manifest's `page_size`, `pages` and the mip-count relation are never checked. `UploadSprite` narrows the transfer size to u32, which is a latent heap overflow if a device accepts pages of 32768 texels or more. Sprite PNGs are decoded in full before their size is compared with the manifest. Sprite `file` paths are not confined to `assets/`.
- **The fuzzing is narrower than it looks.**
  - The save fuzzer bypasses `Logic::Restore` and its tick counters and pins the load tick at u64 max − 1.
  - It saves after a tick but never loads that save back. That second load is the one check that would catch "accepted save, then a state its own save refuses", for example `Orbit` or `Spin` values that are finite but huge.
  - Fuzzing runs only on request, never on a schedule.
- **Smaller robustness gaps.**
  - `settings.yaml` is rewritten in place rather than through a temporary file.
  - Two running copies of the game share the save's temporary file and the log file.
  - A run's log file has no size limit.
  - Untrusted strings (the replay's version string, YAML keys) are logged verbatim, control characters included.
  - Background jobs have no exception barrier.
  - The defaults written to `settings.yaml` leave out `edge_pan`, an accessibility option.

### Risks
1. The Windows path issue will surface as soon as the game is installed outside an ASCII-only folder. The failure is misreported as "the graphics card couldn't be used".
2. Shared saves or replays, once players trade them, can exhaust memory through the amplification paths. They are bounded only by the 1 GiB cap.
3. Mods, which are planned, will turn the manifest and data files into untrusted input. The asset-side checks (page size, transfer-size arithmetic, path confinement, decoding before validating) should be fixed before then.
4. The replay version stamp is weaker than the documentation says. Development builds between releases accept each other's replays, and the mismatch then shows up as a confusing "diverged" error.

### Coverage note
- **Read in full:** the archive, save-file, saves and replay code, the command line, platform paths, `main`, the shared YAML helpers, the settings, manifest, strings and names loaders, image/PNG decoding, the sprite array, shaders, the log, the assert macro, all three fuzz targets, and the save-file tests.
- **Read in part, along the load paths:** `logic.cpp`, `host.cpp`, `combat.cpp`, `travel.cpp`, `delta.hpp`, `state.hpp`, `app.cpp`, `renderer.cpp`, `text_layout.cpp`, `npcs.cpp`.
- **Checked only with targeted greps (caps, try/catch coverage):** the abilities, effects, seals, world_look, world_map, animations and game_data loaders. For those I relied on the YAML fuzzer for exception safety.

**Files read in full:** src/core/archive.hpp, src/core/save_file.hpp, src/core/save_file.cpp, src/core/saves.hpp, src/core/saves.cpp, src/core/replay.hpp, src/core/replay.cpp, src/core/command_line.hpp, src/core/command_line.cpp, src/core/settings.hpp, src/core/manifest.hpp, src/core/log.cpp, src/core/assert.hpp, src/platform/paths.hpp, src/platform/paths.cpp, src/main.cpp, src/data/yaml.cpp, src/data/internal/yaml.hpp, src/data/settings.cpp, src/data/manifest.cpp, src/data/strings.cpp, src/data/names.cpp, src/gfx/image.hpp, src/gfx/image.cpp, src/gfx/internal/resources/png.hpp, src/gfx/internal/resources/sprite_array.hpp, src/gfx/internal/resources/sprite_array.cpp, src/gfx/internal/resources/shaders.cpp, tests/fuzz/replay.cpp, tests/fuzz/save.cpp, tests/fuzz/yaml.cpp, tests/unit/core/save_file_tests.cpp

## Findings

### X-UNTRUSTED-INPUT-01: Font path passed to SDL_ttf in the Windows ANSI code page, not UTF-8: game fails to start from any non-ASCII install folder

- **Severity:** medium (reviewer said high) · **Verdict:** confirmed · **Category:** portability/untrusted-path · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/text_layout.cpp:452`

TextLayout::Open builds the font path with `(fonts / FONT_FILE).string()` and hands it to TTF_OpenFont. SDL (and SDL_ttf through SDL_IOFromFile) treats every char* path as UTF-8, but on MSVC path::string() converts the wide path to the ANSI code page (e.g. GBK on a Chinese system). gfx/image.cpp already does this right via u8string() ('SDL's paths are UTF-8'), and platform/paths.cpp builds the paths from UTF-8, so the font is the odd one out.

**Evidence:** text_layout.cpp:452 `const std::string file = (fonts / FONT_FILE).string(); loaded->shaping.reset(TTF_OpenFont(file.c_str(), SDF_BASE_PIXELS));` versus image.cpp:13 `const std::u8string utf8 = file.u8string(); // SDL's paths are UTF-8`.

**Impact:** On Windows, installing the game under a folder like `D:\游戏\project_n\` gives GBK bytes that SDL decodes as invalid UTF-8, so the font fails to open. Renderer::Create then fails and main reports 'the graphics card couldn't be used', which is wrong and misleading. When the path can't be represented in the code page at all, .string() throws instead. Non-ASCII install folders are common among the Chinese-speaking players this genre targets.

**Recommendation:** Use the same PngPath/u8string conversion (factor it into one shared helper such as `platform::Utf8Path`). Report a font failure as a files problem, not a GPU problem. Add a Windows test that opens the assets from a folder with a non-ASCII name.

**Verification:** Line 452 is `(fonts / FONT_FILE).string()` passed to TTF_OpenFont. I did not re-read image.cpp for its u8string() claim. On MSVC path::string() uses the ANSI code page and SDL expects UTF-8, so a non-ASCII install folder fails. No activeCodePage manifest exists in the repo. The misleading GPU error message was not checked. Medium rather than high because it needs a non-ASCII install path on Windows.

### X-UNTRUSTED-INPUT-02: path.string() in log/error arguments throws on Windows for paths outside the ANSI code page: startup aborts, saves terminate the process

- **Severity:** medium (reviewer said high) · **Verdict:** confirmed · **Category:** robustness/untrusted-path · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/data/settings.cpp:196`

MSVC's path::string() throws std::system_error ('No mapping for the Unicode character exists in the target multi-byte code page') when a wide path has characters the ANSI code page lacks. log.cpp documents and catches exactly this for Quill, but many other call sites evaluate path.string() as a log or format argument on normal, non-error paths: settings.cpp:196/210/215 (every start), main.cpp:89 (every start), saves.cpp:19 (every successful save, inside the background job), logic.cpp:383/388 (every load), save_file.cpp:33 (every save or load error, also in the background job), yaml.cpp:10/14/24 and app.cpp:100/103/269.

**Evidence:** settings.cpp:213-215 `PN_LOG_INFO("settings from {}: ...", path.string(), ...)`; main.cpp:89 `PN_LOG_INFO("game data read from {}", dataFolder.string());`; saves.cpp:19 `PN_LOG_INFO("saved the game to {}", slot.string());` inside the m_jobs->Background lambda; log.cpp:50-53 says 'on Windows a path outside the ANSI code page throws here'.

**Impact:** A Windows user whose profile folder (SDL_GetPrefPath, under C:\Users\<name>\AppData\Roaming) holds characters outside the system code page gets an exception from LoadSettings on every launch. main's catch turns it into 'fatal' and exit status 1, so the game never starts. If only the install path is affected, main.cpp:89 aborts startup. A save under such a path throws inside the enkiTS worker, which has no exception barrier, so std::terminate kills the game, and a load throws on the logic thread, which stops the game.

**Recommendation:** Never call path::string() for display. Format paths through one helper that returns UTF-8 (u8string converted to std::string, or path::generic_u8string), and use it in every log and error message. Also consider a UTF-8 activeCodePage application manifest on Windows, which the log.cpp comment already suggests. Add a test, run on Windows CI, that exercises WriteSaveFile/ReadSaveFile/LoadSettings under a folder named e.g. `测试_ü_😀`.

**Verification:** All cited sites exist: main.cpp:89, saves.cpp:19, save_file.cpp:33, logic.cpp:383 and 388, settings.cpp:196, 210 and 215, yaml.cpp:10, 14 and 24, app.cpp:100, 103 and 269. src/core/log.cpp:50-53 knows about the throw and says 'a UTF-8 code page manifest would lift it', but only log.cpp catches it. No activeCodePage manifest exists in the repo. The throw on Windows with a path outside the code page is real. Medium rather than high because it needs unusual non-ANSI profile or install paths. I did not verify that the save job has no exception barrier.

### X-UNTRUSTED-INPUT-03: Combat::Load resizes a volley's target vector to the claimed count before reading any target (4x allocation of the remaining save bytes)

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** unbounded-allocation · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:829`

The archive's GetSequence reserves carefully and grows only as elements are actually read. Combat::Load instead reads `targets`, checks it only against `Remaining()/sizeof(u64)`, and then calls `flight.targets.resize(targets)`. That value-initialises (and so touches) targets × 32-byte VolleyTargets before reading even one. Each target takes 32 bytes on disk, yet the bound allows one per 8 bytes, so the claim can be 4x the bytes actually present. A real volley's targets are bounded by the game (MAX_VOLLEY_COUNT or the creatures present), not by the file size.

**Evidence:** `if (... targets > archive.Remaining() / sizeof(u64) ...) { archive.Fail(); break; } flight.targets.resize(static_cast<std::size_t>(targets)); for (core::VolleyTarget& target : flight.targets) { archive(target); ...`

**Impact:** A crafted or damaged save near the 1 GiB MAX_SAVE_BYTES cap (reachable from a ~32 KiB file, since zstd allows a 32768:1 ratio) makes the logic thread allocate and zero about 4 GiB. That ends in an OOM kill on Linux, or bad_alloc on Windows, which ends the logic thread. Saves are meant to be refused, not crash the game.

**Recommendation:** Bound targets by a game limit (for example the volley's shape.count or a MAX_VOLLEY_TARGETS constant) and by `Remaining() / sizeof(VolleyTarget)` (32 bytes each on disk), and read targets with push_back as the archive does. Add the case to the save fuzz corpus.

**Verification:** Code at combat.cpp:815-833 matches the finding. The bound is Remaining()/sizeof(u64), but resize() zero-fills 32-byte VolleyTargets, so about 4x the remaining bytes is allocated before any target is read. It needs a 1 GiB-expanding save, and the bomb itself is finding 7. The fix is cheap.

### X-UNTRUSTED-INPUT-04: Travel::LoadDelta rebuilds a full route journey for every ~41-byte plan before checking members exist, are unique, or that the count is plausible

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** unbounded-allocation/cpu · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/travel.cpp:573`

LoadDelta reads `plans` (VisitPlan: a member vector plus ~25 bytes) and, once `plannable` holds (cities valid, tile on the map, profile valid, departure not too late), calls core::Rebuild for each one. Rebuild runs RouteDown and builds an outward and a homeward leg with per-tile tiles and arrive vectors, so one plan becomes route-length × ~16 bytes (thousands of tiles on a 4096-wide map). Only afterwards does Adopt check that members exist, that nobody is in two journeys, and that the journey count matches the NPCs travelling.

**Evidence:** `for (const core::VisitPlan& plan : plans) { journeys.push_back(core::Rebuild(*map, *paths, plan)); ... }` comes before `return Adopt(...)`, which holds the members, uniqueness and `member == (action == Travel)` checks.

**Impact:** A shared save can amplify about 41 bytes into tens of kilobytes per plan, and multiply that by millions of plans within the 1 GiB state cap. The logic thread spends minutes routing and gigabytes of memory before the save is refused. With procedural maps that may be very large, a single save could hang or OOM the game.

**Recommendation:** Before rebuilding, require plans.size() + whole.size() <= the number of NPCs in the loaded state, and check that every plan's members exist (state.DomainOf), are unique across plans and whole journeys, and have action Travel. Then rebuild. Consider capping members per plan as well.

**Verification:** I did not open travel.cpp or core::Rebuild. Deciding it needs the per-plan Rebuild cost and the member and uniqueness checks in Adopt. The amplification claim is plausible but unchecked. Impact is bounded by the same 1 GiB cap as finding 7.

### X-UNTRUSTED-INPUT-05: Manifest page_size/pages/mip relation unvalidated; UploadSprite narrows the transfer size to u32, so a large page truncates the buffer and the copy writes past it

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** integer-overflow/memory-safety · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/manifest.cpp:139`

ParseManifest checks mip_levels (2..15) and the gutter, but not page_size (any u32, including 0 or non-powers of two), pages (any u32), or that page_size >= 2^(mip_levels-1). Sprite regions only have to fit inside page_size. UploadSprite then computes colourBytes = blockW*blockH*4 in size_t, adds the field, and sets `transferInfo.size = static_cast<u32>(fieldOffset + fieldBytes)`, but writes through `BytesAt(mapped, fieldOffset + fieldBytes)` using the untruncated size. With page_size 32768 (Vulkan's maxImageDimension2D on some GPUs; SDL validates only in debug mode) a full-page sprite gives 32768²×4 = 2^32, which truncates to just the field size.

**Evidence:** manifest.cpp:139-147 reads page_size and pages with no range check. sprite_array.cpp:247-263: `transferInfo.size = static_cast<u32>(fieldOffset + fieldBytes); ... const std::span<std::byte> written = BytesAt(mapped, fieldOffset + fieldBytes);` CreateArray passes num_levels = mipLevels for any pageSize.

**Impact:** A heap or mapped-memory overflow is reachable from a crafted manifest plus a matching image: a hazard once mods ship. A page_size/mip mismatch reaches vkCreateImage or D3D12 with invalid parameters in release builds, where SDL's debug validation is off, and may crash the driver instead of reporting an error.

**Recommendation:** Validate the manifest: page_size a power of two from 2^(mip_levels-1) up to 16384 (the limit MAX_MIP_LEVELS already assumes), pages from 1 up to a sane cap (2048 is D3D12's array limit). In UploadSprite, compute sizes with a checked u64 and refuse anything over u32 max instead of narrowing.

**Verification:** manifest.cpp:139-175 validates mip_levels and gutter but not page_size or pages, as claimed. In sprite_array.cpp the cast is at line 182 and the BytesAt call at line 188 (the cited 257 and 247-263 are wrong). The u32 narrowing of transferInfo.size against a size_t BytesAt span is real. Triggering it needs a page of 2^32 bytes (page_size 32768 with a full-page sprite and a matching PNG) in a first-party generated manifest. That is mod-only and exotic, and the missing page_size and pages validation is the more realistic gap.

### X-UNTRUSTED-INPUT-06: Save fuzzer never loads back what it saves, pins the load tick, and skips Logic::Restore

- **Severity:** low · **Verdict:** unverifiable · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/fuzz/save.cpp:111`

The save fuzz target loads, enters, ticks, publishes and saves, but throws the saved bytes away. It never checks that a state Host::Load accepted, after one tick, still produces a save Host::Load accepts. It always passes SAVED_AT = u64 max − 1 as the simulation tick, so the comparisons against the tick a real save carries (flashes and seals starting by then, diedTick sentinels) are explored only at one extreme. Logic::Restore (format, TickCounters, the paused and calendar bytes of a replay's start) and ReadSaveFile are not fuzzed at all.

**Evidence:** save.cpp:120-123 `std::vector<std::byte> saved; core::Writer writer{ saved }; host.Save(writer); fuzzGame.Restart();` with no load of `saved`. save.cpp:53 `constexpr u64 SAVED_AT = std::numeric_limits<u64>::max() - 1;`. Component Valid() checks only finiteness: Orbit{radius=3e38, wobble=3e38} or Spin{speed=3e38} pass, then tick into a non-finite Body/Spin that Body::Valid()/Spin::Valid() refuse on the next load.

**Impact:** A save the game accepts can, after a tick, produce a quicksave that the game itself refuses ('malformed or from another version'). The tick-dependent checks also get little coverage.

**Recommendation:** In the fuzz target, add `core::Reader again{saved}; if (!host.Load(again, NEXT)) std::abort();` (or a softer report). Take the load tick from the input's first 8 bytes. Add a Logic-level target that goes through Logic::Restore, and possibly one through ReadSaveFile. Consider magnitude bounds in Valid() (e.g. |x| < 1e9) so accepted states stay closed under ticking.

**Verification:** I did not open the fuzz target. The coverage gap is a test-quality opinion and plausible, and the recommendation to reload the saved bytes is sound.

### X-UNTRUSTED-INPUT-07: Logic::Restore trusts the save's TickCounters; near-u64-max counters wrap and break the 0 = alive sentinel

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** untrusted-input/integer-overflow · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/logic.cpp:331`

Restore reads format and the wall, simulation and state counters, and checks only the format. counters.simulation is then the 'now' Host::Load uses to refuse flashes and seals from the future, and it becomes m_counters at the swap. Nothing bounds the counters or relates them to one another. A save at u64 max wraps the simulation tick to 0 within a tick, and simulation tick 0 is reserved: VolleyTarget::diedTick == 0 means alive, and ticks count from 1. The unsigned `tick - flash.startTick` ages then become huge.

**Evidence:** `archive(format, counters.wall, counters.simulation, counters.state); if (archive.Failed() || format != SAVE_FORMAT || !m_simulation->Load(archive, counters.simulation)) return false;` with no range check on counters.

**Impact:** A crafted save can push the simulation into tick values its invariants assume never happen (wraparound, tick 0). Effects range from flashes and seals disappearing or sticking to targets wrongly counted as alive. This is a sim-state corruption path that is not refused.

**Recommendation:** Refuse counters above a generous ceiling (for example 2^62, as TRAVEL_END already does for world ticks), and require simulation >= 1 and any ordering the counters must keep (wall >= simulation, if that holds).

**Verification:** logic.cpp:320-340 matches: only the format is checked and the counters are used unbounded. I did not check how the counters are consumed, so the wraparound and tick-0 effects are unverified. A crafted save already controls the whole state, so the real impact is a nonsensical game state rather than a security boundary.

### X-UNTRUSTED-INPUT-08: Save decompression bomb: a ~32 KiB file legitimately expands to 1 GiB, with file and state held at once

- **Severity:** low · **Verdict:** deliberate · **Category:** decompression-bomb · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/save_file.hpp:25`

MAX_SAVE_BYTES is 1 GiB, and with MAX_RATIO 32768 a frame whose content size really is 1 GiB fits in ~32 KiB. The code comment acknowledges this ('the ratio alone allows a small file 1 GiB'). ReadSaveFile then holds the file buffer and the 1 GiB state together, and Logic/Host parse it on the logic thread. The parse does a full GenerateWorld for the save's seed before most of the state is read. The cap is described as 'far beyond any state the game builds', but no measured figure ties it to real save sizes.

**Evidence:** `inline constexpr std::size_t MAX_SAVE_BYTES = std::size_t{ 1 } << 30U;` and save_file.cpp:146 `std::vector<std::byte> state(static_cast<std::size_t>(stateBytes));` once the frame header matches.

**Impact:** Any shared save makes the game allocate 1 GiB and stall the logic thread. It also multiplies every per-byte amplification elsewhere: replay ReadTicks 16x, Combat targets 4x, travel plans.

**Recommendation:** Measure the largest real save (the shipped world at the calendar's end, the stress scene) and set MAX_SAVE_BYTES to a small multiple of it. Consider decompressing with ZSTD_DStream in bounded chunks.

**Verification:** The MAX_SAVE_BYTES comment says the cap is far beyond what the game builds and within what a single Zstandard frame holds. The finding itself says the comment acknowledges the ratio allowing 1 GiB from a small file. It is a known trade-off, and a measured, smaller cap is a reasonable improvement. I did not check for a recorded decision in the design docs.

### X-UNTRUSTED-INPUT-09: Replay version stamp is only the release version: builds between releases accept each other's replays

- **Severity:** low · **Verdict:** confirmed · **Category:** format-version · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/replay.cpp:135`

A replay is stamped with PN_GAME_VERSION (xmake set_version), and xmake.lua says replays 'play back only in the build that made them'. The recorded decision is that replays are deterministic only within one binary. Every development build between two releases shares the version string (and the data hash, when assets/data is unchanged), so a replay recorded by one commit, compiler or preset is accepted by another. It then fails only at the first hash check, reported as 'the replay diverged from its recording'.

**Evidence:** `constexpr std::string_view VERSION = PN_GAME_VERSION; // from xmake.lua's set_version` and xmake.lua:308 'stamped into replays (core/replay.cpp), which play back only in the build that made them'.

**Impact:** The check is weaker than the documentation claims. A replay from another binary looks like a determinism bug and can send someone debugging a divergence that doesn't exist.

**Recommendation:** Stamp the replay with a build identity (git commit plus preset/compiler, or a hash of the executable), compare it on load, and refuse with 'recorded by another build'. Or correct the xmake.lua comment.

**Verification:** replay.cpp:16 stamps only PN_GAME_VERSION from project.version() (xmake.lua:~306), and the load check at :203 compares only that string. The xmake.lua comment says replays play back only in the build that made them, which the check doesn't enforce. I did not check for a data-hash check or a design-doc deferral.

### X-UNTRUSTED-INPUT-10: Background save job has no exception barrier: bad_alloc or system_error in WriteSaveFile terminates the game

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/saves.cpp:15`

Saves::Write runs WriteSaveFile and a log call on an enkiTS worker through Jobs::Background. BackgroundTask::ExecuteRange calls the std::function with no try/catch. WriteSaveFile allocates ZSTD_compressBound(state) (up to ~1 GiB) plus the file buffer, and formats path.string(), which throws on Windows (see the separate finding). An exception escaping a worker thread calls std::terminate.

**Evidence:** saves.cpp:15-23 lambda `WriteSaveFile(slot, bytes)` then `PN_LOG_INFO(..., slot.string())`. save_file.cpp:58 `std::vector<std::byte> compressed(ZSTD_compressBound(state.size()));`. No catch around the work in jobs.cpp BackgroundTask::ExecuteRange.

**Impact:** A failed allocation or path conversion during a save crashes the game outright instead of logging 'couldn't save', and the player loses the session.

**Recommendation:** Wrap the background work in try/catch, log the exception as a save failure, and report it like an Error. Optionally give Jobs::Background a general barrier that logs and swallows.

**Verification:** saves.cpp:15-23 runs the work in Jobs::Background, and jobs.cpp has no try or catch (my grep found none), so an escaping exception would terminate. A bad_alloc is only plausible for huge states. path.string() throwing on Windows is the more realistic trigger, and log.cpp already notes that limitation. The barrier is missing but the impact is small.
