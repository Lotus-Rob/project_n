# Review: x-cpp-safety

**Scope:** CROSS-CUTTING C++ correctness over src/: undefined behaviour, dangling references/views/spans, iterator invalidation, signed overflow, narrowing, unchecked indexing, enum/switch exhaustiveness, exception safety and noexcept, RAII gaps, move semantics mistakes, ODR/header hygiene, NOLINT suppressions and whether each is justified (grep NOLINT and pragmas).

**Findings:** 14 standing (0 critical, 0 high, 2 medium, 7 low, 5 info), 0 raised but refuted on verification.

**Parts:** x-cpp-safety.md, x-cpp-safety-part2.md

## Assessment

## Scope and coverage
Cross-cutting C++ safety review of `src/`. `src/` has about 42k lines, so I did not read every file. I read the core sim and persistence layer in full: archive, save file, replay, delta, state, ECS, jobs, handover, thread safety, logic, journey, paths, draw list, instances, random, display delay, command line, the YAML helpers and saves. I sampled the rest in targeted passes, following callers and callees as needed:
- grep sweeps: every NOLINT and pragma, every float→int cast, path→string conversions, statics and atomics, `try`/`catch`/`noexcept`, every SDL GPU acquire/submit/map, EnTT `get`/`view`
- selected excerpts of host, travel, combat, labels, text_layout, renderer, sprite_array, window and app

## Strengths
- **Untrusted input is handled very carefully.** `core::Reader` bounds-checks every read and caps allocations by the bytes actually left. Save files check magic, version, size ratio, the frame's own content size and a checksum. Delta and order loads reject every malformed shape. `Host::Load` checks how components fit together (`Consistent`, `CombatConsistent`, `IdsSuitDomain`, `WorldFits`, `RouteFits`, `LegSound`/`StaySound`), so EnTT `get<>` is never reached on a missing component. Replay and save fuzz targets exist.
- **Concurrency is disciplined.** There is one mutex-guarded handover with Clang thread-safety annotations, including negative capabilities. The job system uses fixed chunks, so results don't depend on the worker count, and it adds TSan-visible fork/join edges. Destruction order is designed on purpose: `App::m_logic` is declared last; `TextLayout::Impl::batch` is declared last so it finishes before the fonts go.
- **Numerics are deterministic.** Maths runs in f64 with exact reductions (`fmod`/`remainder` before `Sin`/`Cos`). f16 packing is written by hand. Integer arithmetic saturates. Many float→int conversions are checked in floating point before converting (`MakePacedLeg`, `QuantiseDepth`, `TileOf`, `CommitTicksFor`).
- **Header hygiene is clean.** No anonymous namespaces or non-inline definitions in headers, and no `using namespace`. Third-party includes are confined to facades.
- **NOLINT discipline is excellent.** All roughly 40 suppressions are narrow and each states its reason. Every one I checked is justified (details in the info finding). The project's own `.clang-tidy` and hardening flags are strong: `-Weverything -Werror`, `_GLIBCXX_ASSERTIONS` / libc++ hardening / MSVC STL hardening in debug, `_FORTIFY_SOURCE`, CFG, RELRO.

## Weaknesses and risks
1. **Release builds lose every invariant check.** `PN_ASSERT` compiles to nothing, and STL hardening is debug-only. Several asserts guard dereferences, so in release a broken invariant becomes silent UB (for example `end()->second`, or `m_free[--0]`) rather than an abort. Only about 5 of 109 asserts have an "assertions off" fallback.
2. **Cross-platform path encoding (Windows).** `path::string()` is used in about 15 places, including success-path logging inside the background save job and the font open passed to SDL. `log.cpp`'s comment already admits that this conversion throws on MSVC for a path outside the ANSI code page. Here that becomes a terminate on every save, or a fatal error at start. The `TTF_OpenFont` call also hands SDL ANSI bytes where it expects UTF-8.
3. **u32 tick overflow in `MakeTileLeg`.** It is guarded only by `PN_ASSERT`, and the data accepts `ticks_per_cost` up to 65535. Large procedural maps or extreme data wrap the arrive ticks. The result is out-of-bounds reads in `Where` and saves the game later refuses to load.
4. **No compiler check that enum dispatch is complete.** The `-Wswitch-default` / `-Wcovered-switch-default` clash pushed the project to if-chains, so adding a value to any of about 60 enum classes is never flagged. This affects replay field encoding and `DataOf`, for example.
5. **Fuzzing runs only under ASan.** Signed overflow and out-of-range float→int conversions reachable from save bytes (for example `CellAt` on a huge finite body position) are invisible to it.
6. **Exception policy vs reality.** The rule is "our code throws nothing", but the standard library still throws (`bad_alloc`, `path::string()`), and `bugprone-exception-escape` is turned off. A throw inside a `ParallelFor` body leaves a stack `ChunkTask` in use by other workers after it is destroyed, or terminates the program.
7. **FP-environment risk on the main thread.** The main thread runs logic chunks while it waits on low-priority work, but nothing checks or resets its floating-point environment there.

## Overall health
Good to very good for a codebase of this size. The core sim and persistence layer is unusually robust. Most of the risk sits where hardening stops: release builds, Windows string encoding, data-driven numeric limits as maps grow, and tooling gaps (UBSan in fuzzing, enum exhaustiveness).

**Files read in full:** src/core/types.hpp, src/core/archive.hpp, src/core/save_file.hpp, src/core/save_file.cpp, src/core/replay.hpp, src/core/replay.cpp, src/core/frame.hpp, src/core/handover.hpp, src/core/thread_safety.hpp, src/core/jobs.hpp, src/core/jobs.cpp, src/core/ecs.hpp, src/core/ecs.cpp, src/core/stable_id.hpp, src/core/state.hpp, src/core/delta.hpp, src/core/logic.cpp, src/core/journey.cpp, src/core/paths.cpp, src/core/draw_list.cpp, src/core/instances.cpp, src/core/display_delay.cpp, src/core/command_line.cpp, src/core/random.hpp, src/core/saves.cpp, src/core/assert.hpp, src/core/math.hpp, src/game/components.hpp, src/data/internal/yaml.hpp, src/data/yaml.cpp, src/gfx/internal/gpu.hpp, src/gfx/internal/resources/sprite_array.cpp (most), src/game/host.cpp (lines 400-940 and excerpts), src/game/travel.cpp (excerpts), src/game/combat.cpp (excerpts), src/game/npcs.cpp (excerpts), src/gfx/text_layout.cpp (lines 290-470), src/gfx/labels.cpp (lines 30-200, 410-510), src/app/app.cpp (excerpts), src/app/app.hpp (members), src/platform/window.cpp (excerpts), src/core/log.cpp (excerpts), src/core/names.cpp (excerpts), src/data/names.cpp (excerpts), src/data/settings.cpp (excerpts), src/data/world_map.cpp (excerpts), src/main.cpp (excerpts), src/AGENTS.md, xmake.lua (flags), .clang-tidy, tests/fuzz/save.cpp, tests/xmake.lua (fuzz section)

## Findings

### X-CPP-SAFETY-01: path::string() can throw on Windows; a success-path log inside the background save job turns every save into std::terminate

- **Severity:** medium · **Verdict:** confirmed · **Category:** exception-safety / portability · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/saves.cpp:19`

`std::filesystem::path::string()` converts to the ANSI code page on MSVC and throws `std::system_error` for characters outside it. log.cpp:50-53 already says so ("on Windows a path outside the ANSI code page throws here") and wraps its own call in try/catch. About 15 other call sites do not: saves.cpp:19 logs `slot.string()` after a successful write inside `Jobs::Background`, an enkiTS task with no handler. settings.cpp:196/215, logic.cpp:383/388, main.cpp:89, app.cpp:100/103/269 and data/yaml.cpp:10/14/24 do the same on the main or logic thread. save_file.cpp:33 calls it in FileError.

**Evidence:** saves.cpp:16-24: `m_jobs->Background([slot = m_slot, ...] { ... PN_LOG_INFO("saved the game to {}", slot.string()); ...})`; log.cpp:50-53 comment; settings.cpp:196 `PN_LOG_INFO("no settings yet: wrote the defaults to {}", path.string())`.

**Impact:** Take a Windows player whose per-user data folder (%APPDATA%, from SDL_GetPrefPath) contains characters outside the system code page, for example a Chinese or emoji user name on a Western-locale Windows. The game exits as "fatal" at start from LoadSettings. If it gets past that, the save file is written and then the log line throws inside an enkiTS worker, which terminates the process. The game targets a Chinese-speaking audience.

**Recommendation:** Add one helper (for example `core::PathText(const path&)` returning the UTF-8 bytes of `u8string()`) and use it for every log or format of a path. Forbid `.string()` in lint, the way std::print is forbidden. Consider an `activeCodePage` UTF-8 application manifest for Windows builds.

**Verification:** saves.cpp:19 logs slot.string() inside Jobs::Background with no handler. log.cpp:50-53 itself says a path outside the ANSI code page throws on Windows, and a UTF-8 manifest is the stated remedy. Windows-only, and only for non-ANSI profile paths. I did not count the other call sites. Not recorded as a deferral.

### X-CPP-SAFETY-02: Font path passed to SDL_ttf as ANSI bytes, not UTF-8

- **Severity:** medium · **Verdict:** confirmed · **Category:** portability · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/text_layout.cpp:452`

`TextLayout::Open` builds `const std::string file = (fonts / FONT_FILE).string();` and passes `file.c_str()` to `TTF_OpenFont`, which, like every SDL API, reads its path as UTF-8. On Windows `.string()` gives ANSI code-page bytes, or throws as in the save-path finding. gfx/image.cpp:13 and platform/paths.cpp:11-13 already convert through `u8string` for exactly this reason.

**Evidence:** text_layout.cpp:452-457; compare gfx/image.cpp:13 `const std::u8string utf8 = file.u8string(); // SDL's paths are UTF-8`.

**Impact:** Installing the game under a folder with non-ASCII characters, such as `C:\Users\José\Games`, makes the font fail to open (Latin-1 bytes are not valid UTF-8) or throw. Renderer creation then fails and the game does not start. Only Windows is affected; on Linux and macOS `string()` is already UTF-8.

**Recommendation:** Use the same u8string conversion as gfx/image.cpp (share one helper) for the font path.

**Verification:** text_layout.cpp:452 uses (fonts / FONT_FILE).string() and passes it to TTF_OpenFont. The finding says gfx/image.cpp:13 converts through u8string for SDL, and I did not re-read that file. Windows-only, hit by non-ASCII install paths.

### X-CPP-SAFETY-03: MakeTileLeg wraps the u32 arrive ticks in release builds; Where() then reads out of bounds and the game refuses its own saves

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** integer-overflow / scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/journey.cpp:299`

`MakeTileLeg` adds `cost * ticksPerCost` into a u64 and then stores `static_cast<u32>(ticks)`. The only guard is `PN_ASSERT(ticks <= u32 max)`, which is compiled out in release. world.yaml accepts `ticks_per_cost` up to 65535 (data/world_map.cpp:427-428) and step costs up to MAX_WALK_COST (1000, plus danger). So about 66 rock steps at the highest pace, or with the shipped pace of 16 a route costing more than 268M units (a very large procedural map and a high-power NPC with saturating reach), overflows. The wrapped `arrive` is no longer increasing.

**Evidence:** journey.cpp:283-286 `ticks += u64{ cost } * ticksPerCost; PN_ASSERT(ticks <= std::numeric_limits<u32>::max(), ...); leg.arrive.push_back(static_cast<u32>(ticks));`. `Where()` (journey.cpp:342-347) runs `upper_bound` on `leg.arrive` and indexes `leg.arrive[next - 1]`. `LegSound` (travel.cpp:176-184) refuses arrive ticks that don't strictly increase.

**Impact:** In release, `upper_bound` on the unsorted arrive array can return 0, so `from = next - 1` wraps to SIZE_MAX and `leg.arrive[from]` / `LegPoint` read out of bounds. Travellers teleport or the game crashes. A save made in that state is later refused by `LegSound`, so it can't be loaded again. Maps are meant to grow per seed, so this limit will be hit.

**Recommendation:** Make the limit part of the contract. Either have MakeTileLeg/Visit return optional and skip the visit (as MakePacedLeg does), or bound `ticks_per_cost * MAX_WALK_COST * max route length` when reading the data. Add a unit test with a high-pace profile.

**Verification:** The cited line is wrong: the wrap is at journey.cpp:297-300 in MakeTileLeg, where PN_ASSERT(ticks <= u32 max) is compiled out in release and static_cast<u32> wraps. The mechanism is real, and ticks_per_cost goes up to u16 max with step costs up to MAX_WALK_COST. Memory/sweep-up-backlog.md item 13 records this as a known deferral: unreachable with the shipped map, low priority until maps are user-made, fix is to bound it where the map is built. The 'maps will grow' impact is plausible (procedural maps), but the OOB-read chain through Where() and LegSound was not traced.

### X-CPP-SAFETY-04: Release builds turn memory-safety invariants into silent UB: PN_ASSERT compiles out and STL hardening is debug-only

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** undefined-behaviour / design · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/assert.hpp:28`

In release `PN_ASSERT` is `static_cast<void>(sizeof(!(condition)))`, and xmake.lua only enables `_GLIBCXX_ASSERTIONS` / libc++ / MSVC STL hardening in debug. Many asserts directly precede a dereference or index that is only safe if the assert holds. Only about 5 of 109 assert sites have a "with assertions off" fallback (for example Travel::Start, Host::EnterScene), so the treatment is inconsistent.

**Evidence:** State::Destroy (state.hpp:131-135) and Domain::Destroy (ecs.cpp:16-19) use `found->second` after asserting `found != end`. Handover::PopFree (handover.hpp:241-244) does `m_free[--m_freeCount]` after asserting the count is above 0. Replay::Next (replay.cpp:258-263) indexes `m_tickStarts[m_next]`. Logic::ReloadData (logic.cpp:445-447) dereferences `last->data`. MakeTileLeg is covered in its own finding. Domain::Create (ecs.cpp:9-13) keeps going with two entities sharing one StableId.

**Impact:** Any logic bug reaching these paths in a shipped build corrupts memory or the state silently instead of aborting with a log line. That makes player crash reports much harder to symbolise and diagnose. It is the opposite of what the release symbols setup (xmake.lua:66-70) is for.

**Recommendation:** Add a cheap always-on `PN_VERIFY` (log, then abort) for invariants that guard memory safety (index bounds, end() dereferences, counts used as indices), and keep `PN_ASSERT` for costly checks. Alternatively, enable `_GLIBCXX_ASSERTIONS` / libc++ `_LIBCPP_HARDENING_MODE_FAST` in release, which is cheap.

**Verification:** assert.hpp:28 does compile PN_ASSERT out in release. xmake.lua:148-156 adds _FORTIFY_SOURCE=2 in release and releasedbg, while the STL hardening defines are debug-only and commented 'development builds'. PN_ASSERTS is on in releasedbg. So this is a deliberate performance trade-off, not an oversight, though nothing is recorded about it as a decision. The suggestion to harden at least the memory-safety invariants is reasonable. I did not check the asserts at state.hpp:131, ecs.cpp:16 or handover.hpp:241.

### X-CPP-SAFETY-05: Fuzz targets only build under AddressSanitizer: UB reachable from save and replay bytes goes undetected

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** testing / undefined-behaviour · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/xmake.lua:132`

The libFuzzer targets (save, replay, yaml) build only with `--sanitizer address`. Signed overflow, float→int conversions out of range, misaligned access and invalid enum or bool loads are only caught by UBSan, and there is no UBSan fuzz configuration. The save fuzzer reaches code that is UB today on finite but huge values (CellAt below) and relies on cross-function invariants to avoid others: for example `BegunStretches` computes `tick - leg.startTick` with no lower bound on `startTick` in LegSound/StaySound, and only an `Underway()` guard in the caller keeps it safe.

**Evidence:** tests/xmake.lua:132 "libFuzzer targets ... linux-clang with AddressSanitizer only"; journey.cpp:132 `const i64 elapsed = tick - leg.startTick;`; travel.cpp:176-184 LegSound bounds only `startTick <= max - arrive.back()`.

**Impact:** UB on untrusted input passes the fuzzer. In release, UB can be optimised into wrong results or crashes, and determinism can break between compilers.

**Recommendation:** Build the fuzzers with `-fsanitize=address,undefined -fno-sanitize-recover=undefined`, which libFuzzer supports, or add a UBSan corpus-replay run to `check --sanitizers`. Bound journey and leg start ticks below on load (for example `>= -TRAVEL_END`) so they don't depend on caller guards.

**Verification:** tests/xmake.lua:130-132 builds the fuzz targets only when sanitizer==address on linux-clang. That is confirmed. UBSan combined with libFuzzer is a valid improvement and the comment says check --sanitizers covers the corpus. I did not verify the BegunStretches/LegSound lower-bound claim in detail. This is a test-infra gap, not a bug.

### X-CPP-SAFETY-06: CellAt converts an unbounded float to u32 (UB) and wraps the cell index

- **Severity:** low · **Verdict:** confirmed · **Category:** undefined-behaviour · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/npcs.cpp:133`

`row = static_cast<u32>(std::max(point.y / CELL_SIZE, 0.0F))` has no upper clamp, unlike the column on the line above. A finite y ≥ 256 × 2^32 makes the float→u32 conversion UB, and y ≥ about 1.7e10 overflows `row * MAP_COLUMNS` into a wrong cell. Bodies loaded from saves are only checked as finite (Body::Valid). Deactivate (npcs.cpp:272) calls CellAt on every active NPC when a scene is cleared, and the result is not validated for homeless NPCs (NpcsPlaced only checks with a map).

**Evidence:** npcs.cpp:131-135; components.hpp:36-38 `Valid(): IsFinite(previous) && IsFinite(current)`.

**Impact:** A crafted or corrupted save of a dev scene with an NPC at y = 1e30 triggers UB on the next scene change. Not reachable in normal play.

**Recommendation:** Clamp the row to a maximum (for example `(u32 max / MAP_COLUMNS) - 1`) in floating point before converting, as TileOf does, or reject far-away bodies in Host::Load.

**Verification:** npcs.cpp:131-135 clamps the column but not the row: static_cast<u32>(std::max(y/CELL_SIZE, 0.0F)) has no upper bound. A huge finite y is UB, and a smaller overflow wraps row*MAP_COLUMNS. It needs a crafted save, since Body::Valid is only an IsFinite check as the finding says. Low severity fits.

### X-CPP-SAFETY-07: A throw inside a ParallelFor body is a use-after-free on the forking thread and a terminate on workers

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** exception-safety · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/jobs.cpp:226`

`Jobs::Run` builds `ChunkTask task` on the stack and waits on it. If a chunk running on the forking thread throws (std::bad_alloc from a vector growing in a publish or month-step chunk, `path::string()` as above), the exception unwinds through enkiTS's WaitforTask and destroys `task` while workers are still running its other chunks: use-after-free. On a worker the exception escapes enkiTS's thread function: std::terminate. App::RunLogic catches std::exception and reports "logic thread failed" as if the failure were clean. `bugprone-exception-escape` is disabled in .clang-tidy, so tooling never flags this.

**Evidence:** jobs.cpp:222-233 (`ChunkTask task{...}; AddTaskSetToPipe(&task); WaitforTask(&task, ...)`); app.cpp:281-308 try/catch around Game; .clang-tidy `-bugprone-exception-escape`.

**Impact:** Out of memory or a stray standard-library throw during a parallel phase corrupts memory before the "graceful" logic-thread failure path runs.

**Recommendation:** Make the contract explicit: mark `ParallelFor` bodies `noexcept` (wrap the call in a noexcept lambda so a throw terminates deterministically), or catch inside ExecuteRange, record the exception, finish the join and rethrow afterwards.

**Verification:** jobs.cpp:222-232 holds ChunkTask on the stack and waits via enkiTS WaitforTask. A throw from a chunk body is not handled, and the finding says the .clang-tidy bugprone-exception-escape is disabled. The use-after-free on the forking thread depends on enkiTS internals that I did not check. The worker-side terminate is likely. The trigger is bad_alloc or similar, so it is rare. Making bodies noexcept is a reasonable contract.

### X-CPP-SAFETY-08: The main thread runs simulation chunks, but nothing checks or restores its floating-point environment

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** determinism · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/app/app.cpp:265`

The main thread sets the FP environment once in App's constructor, because "waiting on its low-priority work, it may run the logic's chunks". enkiTS's WaitforTask runs tasks of every priority by default, so Pending::~Pending, the scene-load waits and TextLayout batches all let the main thread run the logic's high-priority ParallelFor chunks. But `FloatingPointEnvironmentIsSet()` is only asserted on the logic thread (logic.cpp:34), never on the main thread or workers. The main thread is the one that calls SDL, GPU drivers and SDL_ttf, which are the libraries known to change MXCSR (FTZ/DAZ).

**Evidence:** app.cpp:263-265; logic.cpp:34; math.hpp:66-69 ("a library may change it").

**Impact:** If a driver or SDL call flips FTZ or DAZ on the main thread, the chunks it happens to run compute subnormals differently. State hashes then diverge between runs of the same binary (a replay mismatch), depending on which thread took the chunk.

**Recommendation:** Call SetFloatingPointEnvironment (cheap) at the start of each ChunkTask::ExecuteRange, or before every wait on the main thread. Also add the debug assertion to ExecuteRange.

**Verification:** Main thread sets the env once (app.cpp:265, comment confirms it may run chunks) and the assert is only in the logic loop (logic.cpp:34), as claimed. Whether a driver/SDL call actually flips FTZ/DAZ is speculative; the concern is a real unguarded gap but hypothetical. Re-setting in ExecuteRange is a reasonable cheap hardening.

### X-CPP-SAFETY-09: LabelPass::CellOf converts a possibly-NaN float straight to size_t

- **Severity:** low · **Verdict:** confirmed · **Category:** undefined-behaviour · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/gfx/labels.cpp:35`

`static_cast<std::size_t>(std::clamp(std::floor(pixels / LABEL_GRID_PIXELS), 0.0F, cells - 1))`: std::clamp passes NaN through unchanged (both comparisons are false), and converting NaN to an integer is UB. Box edges come from projected anchors and text widths. A NaN anywhere upstream (a degenerate camera, a zero-size layout) reaches this. The same pattern appears through `lround` (only implementation-defined, not UB) in PackUnorm16 and fog_fade.

**Evidence:** labels.cpp:34-36.

**Impact:** With a NaN, the computed index can land outside m_cells, an out-of-bounds read or write in the label grid. Unlikely in practice.

**Recommendation:** Use `!(x >= lo) ? lo : (x > hi ? hi : x)` (which sends NaN low), or check `std::isfinite` first, in one shared helper for float-to-index conversions.

**Verification:** CellOf does clamp(floor(x/G),0,cells-1) then casts to size_t. std::clamp returns NaN unchanged, so a NaN box edge gives UB conversion. Callers at labels.cpp:416-419 feed box edges unguarded. Needs a NaN upstream, so unlikely; it is only a render label grid, not sim state.

### X-CPP-SAFETY-10: Enum dispatch is never checked for completeness: the switch-warning clash forces if-chains everywhere

- **Severity:** info (reviewer said medium) · **Verdict:** deliberate · **Category:** enum-exhaustiveness · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `src/AGENTS.md:39`

The Gotcha "A switch over every value of an enum fails either way under Clang's -Weverything ... Use ifs" removes the compiler's `-Wswitch` exhaustiveness check from about 60 enum classes. Adding a value later compiles silently, and every if-chain or ternary over that enum quietly treats it as the fallback case.

**Evidence:** Host::DataOf (host.hpp:465-467): `return kind == CreatureKind::Fox ? m_data.fox : m_data.cultivator;`, so a new CreatureKind gets the cultivator's data. Creature::Valid (components.hpp:107) hard-codes `kind <= CreatureKind::Fox`. replay.cpp ReadFields/WriteFields (43-72) are if-chains over ActionKind: a new kind carrying `point` or `slot` would be recorded with no payload and diverge on playback. Host::TakeActions (host.cpp:919-938) silently ignores unknown kinds.

**Impact:** New abilities, creature kinds, action kinds and seal or effect kinds are all on the roadmap (cultivation realms, sects, more combat). Each addition risks wrong data, wrong save validation or replay format bugs, with no compiler help.

**Recommendation:** Turn off the stylistic `-Wswitch-default` (or `-Wcovered-switch-default`) project-wide and keep `-Wswitch`/`-Wswitch-enum`. Write exhaustive switches with no default and `std::unreachable()` after them. Alternatively, put the enum's count in a static_assert next to each dispatch table.

**Verification:** src/AGENTS.md:39 records the -Wswitch-default vs -Wcovered-switch-default clash and says to use ifs. The finding's own status says 'contradicts-recorded-decision'. The concrete examples such as Host::DataOf's ternary are plausible but I did not read them. A static_assert on enum counts near each dispatch is a sound mitigation. The Gotcha does not stop a project-wide change of warning flags.
