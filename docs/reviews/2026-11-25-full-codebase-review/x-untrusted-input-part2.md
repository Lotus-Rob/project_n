# Review: x-untrusted-input (part 2)

### X-UNTRUSTED-INPUT-11: settings.yaml is rewritten in place (truncate then write), and the defaults omit the edge_pan setting

- **Severity:** low · **Verdict:** confirmed · **Category:** partial-write/docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/data/settings.cpp:164`

SaveSettings goes through internal::WriteText, which opens with ios::trunc and writes in place, not through the temporary-file-and-rename pattern WriteSaveFile uses. ParseSettings reads `edge_pan` (and NoteUnknownKeys lists it), but SaveSettings never writes it. The self-documenting defaults file therefore never mentions an option that the accessibility-first goal relies on.

**Evidence:** yaml.cpp:19-27 `std::ofstream output{ path, std::ios::binary | std::ios::trunc };`. settings.cpp:113-119 lists "edge_pan" and settings.cpp:137 reads it, but the format string at 167-177 has no edge_pan line.

**Impact:** A crash or full disk during the write leaves a truncated settings.yaml, which falls back to defaults with a warning; a menu-driven settings screen will write often. Players can't find the edge-pan option without reading the code.

**Recommendation:** Write settings through a temporary file and rename, sharing a helper with save_file.cpp. Add `edge_pan: {}  # true or false` to SaveSettings and a test that every key ParseSettings reads appears in the defaults.

**Verification:** yaml.cpp WriteText opens with trunc and writes in place, with no temporary file or rename. SaveSettings' format string ends at text_shadow with no edge_pan line, although ParseSettings reads edge_pan (settings.cpp:137). Both halves hold. A truncated file falls back to defaults with a warning, so the impact is small.

### X-UNTRUSTED-INPUT-12: No single-instance guard: two running games share quicksave.pns.tmp, replay.pnr.tmp and the rotating log

- **Severity:** low · **Verdict:** confirmed · **Category:** toctou/concurrency · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/save_file.cpp:78`

The temporary file name is fixed (`path + ".tmp"`) and is opened with truncation, not exclusively. Two instances (easy to start by double-clicking) saving at once write the same temporary file, and one renames a mixed file over the save. Quill's rotating file sink in append mode with rotation on creation also races across processes.

**Evidence:** `std::filesystem::path temporary = path; temporary += ".tmp"; std::ofstream output{ temporary, std::ios::binary | std::ios::trunc };`

**Impact:** The old save is lost and replaced by a file the checksum rejects. Log rotation can delete or interleave another instance's log.

**Recommendation:** Use a unique temporary name (process ID plus a random suffix) opened exclusively, or hold a lock file or named mutex in the user data folder so a second instance warns and exits.

**Verification:** save_file.cpp:78-83 uses the fixed name path+'.tmp' opened with trunc. Nothing prevents two instances from sharing it, and I found no single-instance guard. It needs two instances saving at the same moment, which is rare. The log-rotation race was not checked but is plausible.

### X-UNTRUSTED-INPUT-13: Untrusted strings are logged verbatim, control and escape characters included (console and log injection)

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** log-injection · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/replay.cpp:323`

The replay's version string (up to 255 arbitrary bytes from the file) goes into the error message, and from there to the console sink. YAML keys, names and values (NoteUnknownKeys, manifest sprite names, settings values) are echoed in warnings in the same way. None of them is escaped.

**Evidence:** `std::format("recorded by version {} of the game, not {}", version, VERSION)` where `version` is read byte by byte from the replay; yaml.cpp:249 `std::format("there is no {} {}", kind, key)`.

**Impact:** A crafted replay or data file can forge log lines (embedded newlines) or send ANSI escape sequences to a developer's terminal. Diagnostic logs from players can no longer be trusted.

**Recommendation:** Escape non-printable bytes (for example with std::format's `{:?}` debug format in C++23, or a small helper) when logging text from untrusted files. Clamp the length too.

**Verification:** replay.cpp:205 formats the file-read version string into an error message unescaped, and that message reaches the console and log. The version is at most 255 bytes. This is a local, single-user game, so the log-forging impact is minimal. I did not verify each other cited site (yaml.cpp:249).

### X-UNTRUSTED-INPUT-14: A run's log file has no size limit

- **Severity:** low · **Verdict:** confirmed · **Category:** unbounded-disk · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/log.cpp:54`

The RotatingFileSink rotates on creation and keeps 10 files, but no maximum file size is set. The logger level is Debug and the file sink has no filter, so all Debug output lands in the file, including per-tick debug lines such as dropped actions during scene changes, selections and use endings.

**Evidence:** `config.set_open_mode('a'); config.set_rotation_on_creation(true); config.set_max_backup_files(KEPT_FILES - 1);` and no set_rotation_max_file_size. logger->set_log_level(quill::LogLevel::Debug).

**Impact:** A long session, or a bug that logs every tick, can grow one log file without bound in the user's AppData or home folder.

**Recommendation:** Set set_rotation_max_file_size (for example 16 MiB) and consider an Info filter on the file sink in release builds.

**Verification:** log.cpp:54-58 sets rotation on creation and a backup count, but no max file size. The logger level is Debug (log.cpp:67) and the file sink has no filter. Only the console sink is filtered to Info. A single run's file is therefore unbounded.

### X-UNTRUSTED-INPUT-15: Sprite PNGs are decoded in full before their size is checked against the manifest

- **Severity:** low · **Verdict:** confirmed · **Category:** decompression-bomb · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/internal/resources/sprite_array.cpp:212`

DecodeSprite calls SDL_LoadPNG and SDL_ConvertSurface to RGBA, then compares w and h with sprite.width and sprite.height. A small PNG declaring huge dimensions (say 16384×16384 of zeros, a few hundred KB) is decompressed and converted to a 1 GiB surface, twice over with the conversion, on a job thread before it is refused. A scene's sprites decode in parallel.

**Evidence:** `std::expected<Surface, core::Error> decoded = DecodePng(assets / sprite.file); ... if (std::cmp_not_equal(rgba->w, sprite.width) || ...)`

**Impact:** A memory spike or OOM from a bad or modded asset. It is shipped content today, but mods are planned.

**Recommendation:** Read the IHDR width and height (the first 24 bytes) or use SDL's header-only query before decoding, and refuse a mismatch first. Cap total decoded bytes per scene load.

**Verification:** The code around sprite_array.cpp:212 decodes the PNG and converts it to RGBA before comparing against the manifest dimensions. The manifest is a shipped, generated asset today, so the exposure is only through future mods. I did not check how SDL bounds PNG dimensions.

### X-UNTRUSTED-INPUT-16: Hasher::Put assumes scalars of at most 8 bytes (shift past 64 bits is UB)

- **Severity:** low (reviewer said info) · **Verdict:** confirmed · **Category:** ub-hardening · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/archive.hpp:107`

The Scalar concept admits any arithmetic type, but Hasher::Put folds the bytes into a u64 with `u64{raw.at(i)} << (8 * i)`. For sizeof(T) > 8 (long double on x86-64 Linux, __int128) the shift reaches 64 bits or more, which is undefined behaviour. None is used today.

**Evidence:** `for (std::size_t i = 0; i < raw.size(); ++i) { bits |= u64{ raw.at(i) } << (BITS_PER_BYTE * i); }`

**Impact:** A future component field of such a type would silently hash wrongly or invoke UB and break replay hashes.

**Recommendation:** Add `static_assert(sizeof(T) <= sizeof(u64))` to the Scalar branch, or restrict Scalar to fixed-width types.

**Verification:** archive.hpp:107-111 shifts by 8*i for every byte of sizeof(T). With sizeof(T) > 8 the shift reaches 64 or more, which is UB. A static_assert would be a cheap guard. Nothing uses such a type today.

### X-UNTRUSTED-INPUT-17: Unknown command-line arguments are silently ignored

- **Severity:** low (reviewer said info) · **Verdict:** unverifiable · **Category:** robustness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/main.cpp:27`

ChooseScene takes the first `--<scene>` it recognises, and every flag parser looks only for its own name. Misspelled flags (`--fulscreen`, `--worl`, `--quit-afer 5`) start the default arena with no warning. The value of one flag can also be taken as another flag (`--zoom --world`).

**Evidence:** `for (const std::string_view arg : args) { if (... arg.starts_with("--") ? game::SceneNamed(arg.substr(2)) : std::nullopt) ...` and HasFlag-style lookups, with no pass over leftover arguments.

**Impact:** Profiling or measurement runs driven by tools/project_n.py can quietly measure the wrong scene or settings.

**Recommendation:** Parse argv once into known flags and values, and fail or warn on anything left over.

**Verification:** I did not open main.cpp. Whether leftover arguments are rejected anywhere, and how flag values are parsed, would be settled by reading main.cpp and the flag parsers. The impact on measure runs depends on how tools/project_n.py builds the argument list.

### X-UNTRUSTED-INPUT-18: DataWatch iterates a directory_iterator in a range-for, whose operator++ throws

- **Severity:** low (reviewer said info) · **Verdict:** confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/app/app.cpp:112`

List() passes an error_code to the iterator's constructor, but the range-for advances with operator++, which throws filesystem_error on a readdir failure instead of reporting through the error_code. It runs on the main thread inside App::Run (debug builds only), and main's catch turns the exception into a fatal exit.

**Evidence:** `for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{ folder, error }) {`

**Impact:** A debug session exits because the watched data folder hit a transient error (a network share or removable drive), instead of logging 'can't be read'.

**Recommendation:** Iterate with an explicit `it.increment(ec)` loop, or wrap List in try/catch.

**Verification:** app.cpp:112 passes error_code only to the directory_iterator constructor, and the range-for then advances with the throwing operator++. A readdir failure mid-iteration would throw. It is debug-only and an unlikely trigger.

### X-UNTRUSTED-INPUT-19: Saved player route accepted with non-adjacent tiles (teleporting route)

- **Severity:** low (reviewer said info) · **Verdict:** confirmed · **Category:** validation · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/travel.cpp:176`

LegSound, used by RouteFits for the player's saved route, checks tile indices are on the map, arrive ticks increase, the first is 0, and the shape and profile are valid. It does not check that consecutive tiles are neighbours or that the arrive spacing matches the step costs.

**Evidence:** `return leg.shape <= core::LegShape::TileRoute && leg.profile < map.Profiles().size() && core::IsFinite(leg.origin) && !leg.tiles.empty() && ... std::ranges::all_of(leg.tiles, [tiles](u32 tile) { return tile < tiles; }) && ...`

**Impact:** An edited save can make the player 'walk' across the map in a few ticks and, through ExploreRoute, reveal fog anywhere. This is single-player save editing, but it is a state that live play never produces.

**Recommendation:** Check that consecutive tiles are 8-neighbours (or rebuild the route from its endpoints and compare, as journeys do).

**Verification:** LegSound (travel.cpp:175-184) checks tile bounds, arrive monotonicity, profile and finiteness. It does not check adjacency of consecutive tiles or arrive spacing. This only matters for edited saves, which is a single-player concern.

### X-UNTRUSTED-INPUT-20: Replay ReadTicks reserves 16 bytes per remaining byte for tickStarts and held input

- **Severity:** low · **Verdict:** deliberate · **Category:** unbounded-allocation · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/core/replay.cpp:88`

`ticks <= Remaining()` bytes is allowed, and then `tickStarts.reserve(ticks)` (8-byte size_t) and `held.reserve(...)` (8-byte HeldInput) run before any tick is read: 16x the remaining replay bytes, up to the 1 GiB container cap.

**Evidence:** `if (ticks > archive.Remaining()) archive.Fail(); tickStarts.reserve(archive.Failed() ? 0 : static_cast<std::size_t>(ticks)); held.reserve(tickStarts.capacity());`

**Impact:** A crafted replay can reserve about 16 GiB.

**Recommendation:** Each tick takes at least 2 bytes. Bound ticks by Remaining()/2 and reserve min(ticks, a modest cap), growing as ticks are read, as the archive's GetSequence does.

**Verification:** The code is as described, but the line is wrong: it is ReadTicks at lines 85-89, not 207. `ticks > Remaining()` is the only bound. It then reserves `tickStarts` (8 B) and `held` (HeldInput, which I did not measure; 8 B is the finding's figure) per claimed tick before any tick is read. That is about 16 B per remaining byte, and a 1 GiB decompressed replay gives about 16 GiB of reserve. No guard exists elsewhere in StartPlaying. The sweep-up-backlog.md line 90 records it as a known deferral. It proposes the same fix, Remaining()/2 with no up-front reserve, and says to test it first with a forged header. Impact is a bad_alloc caught by the logic thread's catch-all instead of a refused replay. That requires a crafted local replay file, so low severity. The status field's `known-deferred` matches.
