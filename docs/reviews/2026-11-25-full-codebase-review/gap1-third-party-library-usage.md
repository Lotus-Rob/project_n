# Review: gap1-third-party-library-usage

**Scope:** Review how the code uses the libraries that no unit has audited. (1) EnTT v4.0.0, through src/core/ecs.hpp, state.hpp, delta.hpp/.cpp and the view, storage and sort users in src/game (npcs, travel, world_state, combat, scenes/arena, host). Look for: entity version recycling versus core::stable_id and saved IDs, destroying or emplacing during view iteration, pool and storage iteration order after load versus a continuing run, signals or observers, misuse of v4 API semantics (default storage policies, in-place delete, the entity storage), and per-entity cost patterns. (2) quill 13.1 (the quill-wine fork), through src/core/log.cpp and log.hpp and platform/sdl_log.cpp. Look for: the queue type (bounded/blocking versus unbounded/dropping), hot-path log cost, the backend thread's start and stop relative to static destruction and SDL_Quit, behaviour under Wine, format-string safety with untrusted strings. (3) Tracy 0.14.1, through core/profile.hpp. Look for: zones and plots compiled out cleanly when off, zone names on hot inner loops, allocation tracking, only_localhost, lock and thread naming. Report misuse against each library's documented contract, with version-specific references. Earlier units already covered enkiTS exceptions, SDL3 GPU, SDL3_ttf threading, yaml-cpp and zstd. Don't repeat those. (Why this unit exists: The scopes reviewed SDL3 GPU, SDL3_ttf, enkiTS, yaml-cpp and zstd usage, but EnTT, the backbone of simulation state, saves and hashing, and quill, the logging that crash diagnosis depends on, never had a library-contract review. Only the asynchronous no-crash-flush problem was raised for logging.)

**Findings:** 21 standing (0 critical, 0 high, 2 medium, 11 low, 8 info), 0 raised but refuted on verification.

**Parts:** gap1-third-party-library-usage.md, gap1-third-party-library-usage-part2.md, gap1-third-party-library-usage-part3.md

## Assessment

## Third-party library usage: EnTT v4.0.0, quill 13.1 (quill-wine), Tracy 0.14.1

**Overall:** the EnTT usage is in good shape. It is careful and well tested, and it meets the determinism goal within one binary. The quill and Tracy integrations work and keep library includes out of the rest of the code. Their weak spots are defaults inherited from the package recipes that nobody chose on purpose: Tracy's enforced callstacks, fibers and always-on collection, and quill's polling interval, byte escaping and unchecked format strings. Each is cheap to fix.

### EnTT
**Strengths**
- The stable-ID design is sound. `core::StableId` is the only identity that crosses ticks, domains, saves and input. EnTT handles never persist, so entity version recycling (12-bit versions) and the different handle values after a load cannot leak into simulation state.
- Iteration order is treated as part of the state, and the handling matches EnTT v4's contract:
  - `sparse_set::sort_as` puts its first element at `begin()`, the last packed slot. `LoadStorageOrder` passes the reversed order for exactly that reason, and `SortCanonical` sorts with no ties because stable IDs are unique.
  - `registry::storage()` iterates pools but not the entity storage. Every component ever emplaced (Body … Afflictions, Group) is in a saved `ComponentList`, so after a load every storage has the same contents, order and size. Multi-component views therefore pick the same leading pool (the smallest).
  - `delta_tests`, `state_tests` and `ecs_tests` check that a loaded domain iterates and hashes like the saved one.
- No signals, observers, groups or in-place-delete storages are used. Structural changes during view iteration are avoided by copying (`TickAfflictions`, `Combat::Tick`, `Aura`), and storages are fetched before job forks (`Host::Publish`, `MonthStep::Run`, `Travel::SendOut`).

**Weaknesses**
- **Entity limit:** 32-bit `entt::entity` allows 2^20 live entities per registry. `Domain::Create` does not check the result, and in release builds running out gives undefined behaviour. Only the 100k cap on world people keeps the game below the limit today.
- **Thin facade:** `core::Registry` is the raw `entt::registry`, so the rule that job chunks make no structural changes is a convention only. Chunks receive mutable storages.
- **Asserts off in release:** `PN_ASSERT`-guarded `Create`/`Destroy` carry on in release builds and dereference `end()`.
- **Per-entity costs:** two node-based `unordered_map`s per entity. `Host::Publish` builds every entity's instance before culling it.

### quill
**Strengths**
- Format strings must be literals because quill stores them in `static constexpr` metadata, so untrusted strings (SDL messages, YAML warnings, paths) can only be arguments. Format injection is impossible.
- The queue is the default UnboundedBlocking one: nothing is dropped, and a logging thread blocks only at 2 GiB.
- `Backend::start` registers `atexit(stop)` after constructing its singletons, and all frontends (logic `jthread`, enkiTS, SDL after `SDL_Quit`) finish before `main` returns. The queue therefore drains on normal exit.
- Assertion failures flush before `abort`. The Wine file-sink patch fails loudly if the upstream line changes.

**Weaknesses**
- Quill 13 does **not** check format strings at compile time, although the header says the macros work "like `std::format`". A heuristic pass found no mismatches today.
- The default `check_printable_char` turns all UTF-8 into `\xNN` (Chinese text, non-ASCII user paths).
- The backend wakes every 100 µs for the whole run.
- `Start` runs outside `main`'s try block.
- Nothing caps the log file's size within one run.
- `flush_log` after the backend has stopped would hang.

### Tracy
**Strengths**
- Zones compile out cleanly. The off-build macros are `static_cast<void>(0)`, and `TRACY_ENABLE` only reaches releasedbg through the `tracy_on` package. `only_localhost` is set.
- Zone placement follows the "no zones on per-item functions" rule: job chunks, not items, carry zones.

**Weaknesses**
- The xmake-repo recipe's defaults define `TRACY_CALLSTACK=1` and `TRACY_FIBERS`. Every zone and message therefore captures a callstack, which inflates per-zone cost and skews `measure`/`profile` numbers and the documented 0.25 µs per zone.
- `on_demand` is off, so releasedbg runs with no viewer connected queue profile data in memory indefinitely.
- No thread is named, so `measure` infers threads from zone file names. Locks are not instrumented.
- The off-build macros don't type-check their arguments, so mistakes surface only in the releasedbg build (a documented gotcha that is cheap to remove).

### Risks
The highest-leverage fixes:
1. Turn off `enforce_callstack`/`fibers` and take a new `measure` baseline.
2. Add a compile-time format check to `PN_LOG_*`.
3. Guard `Domain::Create` against an exhausted entity space before procedural worlds grow.
4. Let UTF-8 through quill's printable-character filter.

**Files read in full:** src/core/ecs.hpp, src/core/ecs.cpp, src/core/state.hpp, src/core/delta.hpp, src/core/delta.cpp, src/core/log.hpp, src/core/log.cpp, src/core/profile.hpp, src/core/assert.cpp, src/platform/sdl_log.cpp, src/platform/sdl_log.hpp, src/main.cpp, src/game/components.hpp, src/game/components.cpp, src/game/world_state.hpp, src/game/world_state.cpp, src/game/npcs.cpp, src/game/travel.cpp, src/game/combat.cpp, src/game/host.cpp, src/game/scene.cpp, src/game/scenes/arena.cpp, src/game/scenes/stress.cpp, src/game/scenes/world.cpp, src/game/scenes/cells.cpp, src/game/scenes/painted.cpp, src/game/scenes/arena_layout.cpp (spawn/respawn half), tests/unit/core/ecs_tests.cpp, tests/unit/core/log_tests.cpp, tests/unit/platform/sdl_log_tests.cpp, tests/unit/log_startup/early_tests.cpp, tests/unit/log_startup/fallback_tests.cpp, tests/unit/log_startup/main.cpp, xmake.lua (requires and game_pch section), xmake/repo/packages/q/quill-wine/xmake.lua, xmake/repo/packages/t/tracy-libdl/xmake.lua, ~/.xmake/repositories xmake-repo packages/t/tracy/xmake.lua, quill 13.1 headers: core/FrontendOptions.h, backend/BackendOptions.h, Backend.h (start/stop), LogMacros.h (QUILL_LOGGER_CALL), Logger.h (log_statement, flush_log), Frontend.h (create_or_get_logger), EnTT v4.0.0 headers: entity/registry.hpp (assure, storage, create, destroy, view, sort), entity/sparse_set.hpp (sort_n, sort_as), entity/storage.hpp (entity storage generate/next), entity/entity.hpp (traits), config/config.h, Tracy 0.14.1 Tracy.hpp (ZoneNamed, TracyMessage) and tracy-libdl package manifests

## Findings

### GAP1-THIRD-PARTY-LIBRARY-USAGE-01: Tracy builds define TRACY_CALLSTACK=1 and TRACY_FIBERS: every zone and message captures a callstack

- **Severity:** medium · **Verdict:** confirmed · **Category:** performance/profiling-accuracy · **Status:** new · **Reviewer confidence:** high
- **Where:** `xmake.lua:266`

`add_requires("tracy-libdl v0.14.1", {alias = "tracy_on", configs = {tracy_enable = true, only_localhost = true}})` sets only two of the recipe's configs. The xmake-repo tracy recipe defaults to `enforce_callstack = true`, `callstack_depth = 1` and `fibers = true`. The installed package manifest's exported defines are `TRACY_CALLSTACK=1`, `TRACY_ENABLE`, `TRACY_NO_CALLSTACK_INLINES`, `TRACY_ONLY_LOCALHOST` and `TRACY_FIBERS`. In Tracy 0.14.1's Tracy.hpp, `ZoneNamed(varname, active)` expands to `tracy::ScopedZone varname(&loc, TRACY_CALLSTACK, active)`, and `TracyMessage` also passes `TRACY_CALLSTACK`. So every `PN_PROFILE_ZONE()` sends a ZoneBeginCallstack (one backtrace each), and the Tracy thread then resolves symbols for them. TRACY_FIBERS adds per-event fiber bookkeeping.

**Evidence:** Package manifest (tracy-libdl v0.14.1, tracy_enable=true): defines = { "TRACY_CALLSTACK=1", "TRACY_ENABLE", "TRACY_NO_CALLSTACK_INLINES", "TRACY_ONLY_LOCALHOST", "TRACY_FIBERS" }. Tracy.hpp:144 `#define ZoneNamed( varname, active ) ... tracy::ScopedZone varname( &..., TRACY_CALLSTACK, active )`. Recipe: `add_configs("enforce_callstack", {default = true})`, `local callstack_depth = package:config("enforce_callstack") and package:config("callstack_depth") or 0; if callstack_depth > 0 then package:add("defines", "TRACY_CALLSTACK=" .. callstack_depth)`.

**Impact:** Every releasedbg zone costs a stack capture, which inflates exactly what `measure` and `profile` report (frame percentiles, the main thread's share, per-zone self times). src/AGENTS.md's Gotcha ('a zone costs its caller about 0.25 µs') measures this overhead, not Tracy's normal cost. That pushes decisions about where zones go, and the gate numbers in MEASURE_EARLIER, off the true cost. Job chunks with zones run on every worker, so the distortion grows with the worker count.

**Recommendation:** Set `enforce_callstack = false` and `fibers = false` in both tracy requires (keep them identical so the lock holds both). Use `ZoneScopedS`-style callstacks only where wanted. Then re-measure the per-zone cost and the `measure` baselines, and update the Gotcha.

**Verification:** Installed tracy-libdl v0.14.1 manifest exports TRACY_CALLSTACK=1 and TRACY_FIBERS with tracy_enable. Tracy.hpp:144 passes TRACY_CALLSTACK to ScopedZone, and line 220 passes it to TracyMessage. xmake.lua:266 sets only tracy_enable and only_localhost, and the local recipe override only adds dl. Zone overhead in captures is inflated. No doc records callstacks as deliberate. The src/AGENTS.md:46 0.25 us figure was measured under this config.

### GAP1-THIRD-PARTY-LIBRARY-USAGE-02: Host::Publish builds every entity's instance, pose and glow before DrawList::Add culls it

- **Severity:** medium · **Verdict:** confirmed · **Category:** performance/wasted-work · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1829`

For every Look entity, the publish loop does several sparse lookups (`looks.get`, `ids.get`, `bodies.get`, `spins.contains/get`, `dyings.contains/get`), then `DrawnPoseOf`, `DissolveAt`, `DemoRoleOf` (a linear find over the demo glows), `DemoGlow` and `core::MakeInstance` (a manifest lookup that builds the full Instance), once for the shadow and once for the body. Only then does it call `m_drawList.Add`, whose first statement discards the instance when `instance.current` lies outside the view plus CULL_MARGIN (draw_list.cpp:91-95).

**Evidence:** host.cpp:1829-1868 builds both instances unconditionally; draw_list.cpp:89-96 `const Vec2 position = instance.current; if (position.x < m_view.min.x - CULL_MARGIN || ...) return;`.

**Impact:** The stress scene publishes about 100k entities per tick. At normal zoom most are off-screen, yet each one pays the full instance build. This is the per-entity cost the 'avoid wasted work' rule (hot paths compute only what is drawn) is meant to prevent, and it grows with crowd sizes.

**Recommendation:** Test the body's current position against the area (the same margin, exposed by DrawList as `Visible(point)`) right after `bodies.get(entity)`, and skip the rest for culled entities. Keep `Add`'s check as a safety net, or add an `AddVisible` that skips it.

**Verification:** host.cpp:1829-1868 builds the pose, dissolve, glow lookup and two MakeInstance calls before DrawList::Add (src/core/draw_list.cpp:89-96, which is in core, not gfx), whose first action is the view-margin cull. The cited code matches. This breaks the avoid-wasted-work rule on a roughly 100k-entity path. It is parallelised across chunks, so the effect is a cost rather than a stall.

### GAP1-THIRD-PARTY-LIBRARY-USAGE-03: PN_LOG_* format strings are not checked at compile time (quill 13 stores them as a plain const char*)

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** correctness/logging · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/log.hpp:34`

log.hpp says the macros take 'a format string and its arguments like `std::format`', which suggests std::format's compile-time checking. Quill 13.1 does no such check. `QUILL_LOGGER_CALL` stores `fmt` in a `static constexpr quill::MacroMetadata` and calls `log_statement(&metadata, args...)`, with no `format_string<Args...>` anywhere. The format runs later on the backend thread. A wrong placeholder count or a bad spec (`{:016x}` on a string) is only reported at runtime, as `[Could not format log statement. message: ..., error: ...]` (BackendWorker.h:2436) plus a line on stderr from `error_notifier`.

**Evidence:** LogMacros.h:400-410 `QUILL_DEFINE_MACRO_METADATA(QUILL_FUNCTION_NAME, fmt, tags, log_level); quill_macro_logger_->template log_statement<...>(&quill_macro_metadata_, ##__VA_ARGS__);`. Logger.h has no static_assert or format_string on the arguments. A heuristic scan of all PN_LOG_* call sites (placeholder count against argument count) found no mismatches today.

**Impact:** A typo in a rarely hit message (an error or warning path, usually the one needed to diagnose a crash or a refused save) compiles and loses its content exactly when it matters. Neither `lint` nor the tests catch it.

**Recommendation:** Add a compile-time check inside the macros, for example `if constexpr (false) { static_cast<void>(std::format(fmt, __VA_ARGS__)); }` (never evaluated, but it instantiates std::format_string's consteval check). Alternatively use a small consteval helper that counts `{}` against `sizeof...(args)` if some argument types only have fmtquill formatters. Fix the header comment either way.

**Verification:** log.hpp macros forward straight to QUILL_LOG_*, so the format is a runtime backend check, which I did not read in Quill's source. I could not find the 'like std::format' wording in the header, so the comment claim is weak. The guard suggestion is sensible, but the author's scan found no mismatches today, so impact is latent.

### GAP1-THIRD-PARTY-LIBRARY-USAGE-04: Domain::Create ignores EnTT's 2^20 live-entity limit, which is undefined behaviour in release builds

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** scaling/ub · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/ecs.cpp:8`

`core::Entity` is EnTT's default 32-bit `entt::entity`: a 20-bit index (`entity_mask = 0xFFFFF`) and a 12-bit version. Once 1,048,575 entities are live in one registry, the entity storage's `next()` returns `entt::null`. Its only guard is `ENTT_ASSERT(entt != null, "No more entities available")`, which is compiled out under NDEBUG. `Domain::Create` then emplaces a StableId on `entt::null` and records it in `m_entities`. Nothing in ecs.hpp or state.hpp documents the limit.

**Evidence:** entity.hpp:44 `entity_mask = 0xFFFFF`; storage.hpp:965-979 `from_placeholder(): ENTT_ASSERT(entt != null, ...)`, `next()` loops until `entt != null` fails; ecs.cpp `const Entity entity = m_registry.create(); m_registry.emplace<StableId>(entity, stableId);`.

**Impact:** Today `MOST_WORLD_PEOPLE` (100,000) and the stress scene (about 100k active entities) stay well below the limit. The project's direction (procedural per-seed maps that 'may be very large', ToI-scale populations, groups materialising into NPCs, sects and items as entities) can cross it. In release builds that silently corrupts the registry: emplace on null, later lookups return garbage, and possibly a crash or a desync, with no diagnostic.

**Recommendation:** In Domain::Create, `PN_ASSERT(entity != entt::null, ...)` and handle exhaustion with an error that the caller (generation or load) turns into a refusal. Document the limit in ecs.hpp. If larger worlds are planned, set `ENTT_ID_TYPE` to `std::uint64_t` as a public define on game_pch, the same in every translation unit since EnTT enforces this with detect_mismatch, and re-measure memory.

**Verification:** ecs.cpp:8-12 does emplace on the result of create() unchecked, and EnTT's 20-bit index limit with an ENTT_ASSERT is real. MOST_WORLD_PEOPLE is 100'000 (world_map.hpp:57), about 10x below the limit, and the stress scene is about 100k. This is a latent scaling concern, not a present bug, so low.

### GAP1-THIRD-PARTY-LIBRARY-USAGE-05: Tracy collects from start-up with no viewer connected (on_demand off), so releasedbg runs buffer profile data without bound

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** resource-usage/profiling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `xmake.lua:266`

The recipe's default `on_demand = false` applies, so `TRACY_ON_DEMAND` is not defined. Tracy then records from process start and keeps every event queued in memory until a server connects. src/AGENTS.md documents `./dev run --mode releasedbg` with the viewer attached live as a normal workflow. A releasedbg run started without the profiler, or left running after the viewer disconnects, keeps accumulating zones (several per frame plus one per job chunk on every worker, each with a callstack, see the TRACY_CALLSTACK finding).

**Evidence:** Package manifest configs: `on_demand = false`; exported defines contain no TRACY_ON_DEMAND.

**Impact:** Memory grows steadily during long releasedbg sessions, such as soak-testing a world for hours or leaving the game running, which skews memory observations and can end in OOM. `profile` and `measure` are unaffected because they attach a capture from the start.

**Recommendation:** Either enable `on_demand` (and make `profile`/`measure` connect before the measured stretch, which they already do with `--quit-after`), or document in src/AGENTS.md that releasedbg runs without a viewer grow without bound.

**Verification:** The manifest has no TRACY_ON_DEMAND, so Tracy queues events until a viewer connects. That holds for releasedbg only, which is a profiling mode, and the other presets compile Tracy out. Realistic impact is small and the memory growth was not measured. Documenting it is enough.

### GAP1-THIRD-PARTY-LIBRARY-USAGE-06: No thread is named for Tracy or the OS; measure guesses threads from zone source files

- **Severity:** low · **Verdict:** confirmed · **Category:** observability · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/profile.hpp:202`

profile.hpp has no thread-naming macro, and nothing in src calls `tracy::SetThreadName`, `pthread_setname_np` or `SetThreadDescription` for the logic `std::jthread` (app.cpp:272) or the enkiTS workers. Only quill's backend names itself (`QuillBackend`). Tracy shows these threads by numeric ID, and tools/project_n.py `measure` has to infer them: `LOGIC_FILE = "src/core/logic.cpp"  # names the logic thread`, `JOBS_FILE = "src/core/jobs.cpp"  # names the job system's workers`.

**Evidence:** rg finds no SetThreadName or setname in src; project_n.py:2516-2517 and 2637-2647 attribute threads by the file of their zones.

**Impact:** Captures are harder to read, and measure's thread attribution is a heuristic. A refactor that moves a zone to another file, or a worker that runs a logic-file zone, mislabels the per-thread busy numbers that the smoothness table reports.

**Recommendation:** Add `PN_PROFILE_THREAD(name)` (Tracy's `tracy::SetThreadName` when on, nothing when off) to profile.hpp. Call it at the top of RunLogic and from enkiTS's thread-start callback (`enki::TaskSchedulerConfig::profilerCallbacks.threadStart`). Then let measure use Tracy's thread names.

**Verification:** grep of src finds no SetThreadName or setname call. Thread attribution in measure is inferred from zone source files, as the finding says. This is a real observability gap but not a defect.

### GAP1-THIRD-PARTY-LIBRARY-USAGE-07: Disabled PN_PROFILE_* macros drop their arguments unchecked, so mistakes surface only in releasedbg

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** tooling/build-hygiene · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/profile.hpp:217`

Outside releasedbg, `PN_PROFILE_MESSAGE(text)` expands to `static_cast<void>(0)` and never looks at `text`. A bare literal (no `.data()`) or a mistyped variable compiles in debug and release and fails only in `check`'s releasedbg build or in `measure`. src/AGENTS.md records this as a Gotcha. Likewise `PN_PROFILE_ZONE()` declares a variable with the fixed name `pn_profile_zone` only in releasedbg, so a second zone in one scope (a redeclaration) or a zone in a nested scope (-Wshadow under -Weverything, which is an error) also breaks only that build.

**Evidence:** profile.hpp:217-219 `#define PN_PROFILE_MESSAGE(text) static_cast<void>(0)`; :208 `#define PN_PROFILE_ZONE() ZoneNamed(pn_profile_zone, true)`; src/AGENTS.md Gotchas: 'PN_PROFILE_MESSAGE takes a std::string or std::string_view, never a bare literal ... only check's releasedbg build or measure fails on it'.

**Impact:** Per-task gates (`test`, `lint` on the native debug preset) pass, and the error turns up later at the branch head. That is the kind of late surprise the gates are meant to catch.

**Recommendation:** Make the off variant type-check its argument without evaluating it, e.g. `#define PN_PROFILE_MESSAGE(text) static_cast<void>(sizeof((text).data()) + sizeof((text).size()))`. Give the zone variable a line-unique name (`TracyConcat(pn_profile_zone_, __LINE__)`) so nested zones don't shadow. Then remove the Gotcha.

**Verification:** profile.hpp:27-29 stubs the off macros as static_cast<void>(0) and drops their arguments. src/AGENTS.md:45 records the PN_PROFILE_MESSAGE consequence as a known Gotcha, and design doc 0001 item 28 records the empty stub deliberately. The Gotcha is therefore known and deliberate. The nested-zone shadowing claim is plausible: the fixed name pn_profile_zone is shadowed in a nested scope. A zone declared twice in one scope is a redeclaration.

### GAP1-THIRD-PARTY-LIBRARY-USAGE-08: quill's default check_printable_char escapes all UTF-8: Chinese text and non-ASCII paths are logged as \xNN

- **Severity:** low · **Verdict:** confirmed · **Category:** logging/i18n · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/log.cpp:39`

`quill::Backend::start()` uses the default `BackendOptions`. Their `check_printable_char` accepts only `' '..'~'`, `\n`, `\t` and `\r`, and rewrites every other byte as its hex value whenever a statement has a string argument. Every byte of a UTF-8 multibyte sequence is outside that range.

**Evidence:** BackendOptions.h:42-45 `return (c >= ' ' && c <= '~') || (c == '\n') || (c == '\t') || (c == '\r');` and :320 default. Affected sites include text_layout.cpp:350 `PN_LOG_WARNING("the font has no U+{:04X} (in \"{}\"), so it isn't drawn", character, text)` with game text (the game has Chinese names and localisation plans), :303 `"{}" wasn't laid out`, log.cpp:74 `logging to {}` and settings.cpp paths under a non-ASCII user folder, and data-loader warnings quoting YAML values.

**Impact:** Logs that carry localised text or user paths become unreadable byte soup, for example `C:\Users\J\xC3\xB6rg\...` or the very missing-glyph text the warning is about. That slows down the diagnosis of localisation and installation problems.

**Recommendation:** Pass `BackendOptions` with `check_printable_char = [](char c) { return static_cast<unsigned char>(c) >= 0x80 || (c >= ' ' && c <= '~') || c == '\n' || c == '\t' || c == '\r'; }` (control characters are still escaped), to both `Backend::start` calls. Log paths through a UTF-8 helper (`path.u8string()`) instead of `.string()`, which also throws on Windows for characters outside the ANSI code page.

**Verification:** The installed quill BackendOptions.h:42-45 and :320 default check_printable_char accepts only ' '..'~', \n, \t and \r. log.cpp calls Backend::start() with default options, so UTF-8 bytes in string arguments are escaped. log.cpp's own comment at the .string() call acknowledges the Windows path limitation. The default is real and the impact is limited to log readability.

### GAP1-THIRD-PARTY-LIBRARY-USAGE-09: An assertion failure calls flush_log, which hangs if quill's backend has already stopped

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** robustness/shutdown · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/assert.cpp:12`

`AssertFailed` logs and then calls `log::Root()->flush_log()` before `std::abort()`. Quill's `flush_log` queues a flush event and spins until the backend handles it. The Logger.h docs say it 'should only be called when the backend worker is running', and warn against calling it from static destructors. The backend stops in quill's `std::atexit` handler. An assertion that fires during static destruction, or in any code that runs after atexit handlers (a late SDL callback, a static object's destructor), would wait forever instead of aborting.

**Evidence:** Logger.h:613-620 warnings; Backend.h:73 `std::atexit([]() { Backend::stop(); });`; assert.cpp:12-13.

**Impact:** An assertion that should crash quickly during shutdown would hang the process instead. CI shows that as a timeout and players as a game that never closes, and the critical message is never written.

**Recommendation:** Flush only `if (quill::Backend::is_running())` (available in quill 13). Otherwise write the message straight to stderr before aborting.

**Verification:** assert.cpp:12 does call log::Root()->flush_log() unconditionally before abort, with no is_running guard. The hang needs an assertion firing after quill's atexit stop, which is a narrow window. The guard is cheap and the hang risk is real in principle, but I did not check quill's flush_log behaviour when the backend has stopped, so impact is unverified.

### GAP1-THIRD-PARTY-LIBRARY-USAGE-10: log::Start runs outside main's try block, and quill's start-up can throw

- **Severity:** low · **Verdict:** confirmed · **Category:** error-handling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/main.cpp:163`

`core::log::Start(...)` is the first statement of main, outside the try block that wraps `Run`. Inside Start, `quill::Backend::start()` (thread creation, signal-mask and singleton checks: `QUILL_THROW(QuillError{...})`) and `create_or_get_sink<ConsoleSink>` sit outside log.cpp's own try, which covers only the file sink. Root()'s early-logger lambda calls `Backend::start()` too. The repo rule is 'our code throws nothing', but quill does.

**Evidence:** main.cpp:163 before `try {` at :210; log.cpp:39-41 outside the try at :48; Backend.h:138/164 `QUILL_THROW(QuillError{...})`.

**Impact:** A failed backend start (thread limit, sandbox) ends in std::terminate with no message anywhere, so the one failure that disables logging is itself never reported.

**Recommendation:** Catch around the quill calls in Start and Root, fall back to writing to stderr, and let the game continue without a logger, or report through `platform::ReportStartFailure`.

**Verification:** log.cpp:39-41 Backend::start() and create_or_get_sink<ConsoleSink> sit outside the try, which wraps only the file sink (lines 48-64). main.cpp:210 try comes after Start is called. A quill throw there would end in terminate. Very unlikely in practice.
