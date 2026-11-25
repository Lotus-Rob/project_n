# Review: x-determinism

**Scope:** CROSS-CUTTING over all of src/ (and tests that claim to prove it): determinism of the simulation, saves and replays. Hunt: floating point in sim state (FMA/contraction, libm calls, x87, fast-math flags in xmake.lua), unordered container iteration, pointer-keyed ordering, uninitialised padding hashed or saved, threads affecting sim results, RNG stream discipline, time/wall clock leaking into sim, platform-dependent integer widths, std:: algorithms with implementation-defined results (std::sort stability, std::hash, distributions), locale.

**Findings:** 14 standing (0 critical, 0 high, 1 medium, 8 low, 5 info), 0 raised but refuted on verification.

**Parts:** x-determinism.md, x-determinism-part2.md

## Assessment

## Determinism of the simulation, saves and replays (cross-cutting over src/)

**Overall: careful and above average for a game at this stage.** The engine's guarantee is "the same binary, the same saved state and inputs, the same ticks on any machine". Most of what that needs is built properly, and much of it is tested.

### Strengths
- **RNG discipline is good.** Every sim draw uses `core::Random(seed, stateCounter, stableId, purpose, draw)`, a SplitMix-style hash with a golden-value test. There is no `std::*_distribution`, no shared stream and no `rand`/`mt19937`. Draws key on stable IDs, never EnTT handles. Lemire's reductions are hand-written, so they don't depend on the standard library.
- **Transcendental functions are our own.** `core::math` is built only from exactly rounded operations. `lint` bans libm transcendentals in `core/` and `game/` with a reasonable regex. Every remaining libm call in the sim (`sqrt`, `round`, `fmod`, `remainder`, `ceil`, `nextafter`, `ldexp`, `frexp`) is exact. No fast-math: XMake modes give -O3 or /Ox, and `/fp:precise` is the MSVC default.
- **Parallel work can't leak scheduling into results.** Fixed-size chunks with one enkiTS item each; per-chunk output vectors merged in chunk order; commands merged by a stable sort on (target, source); the shared `Paths` cache is warmed before forking. Tests compare 1 against 4 workers on 50k NPCs (`shipped_world_tests`, `travel_tests`, `npcs_tests`, `jobs_tests`).
- **Orders are total.** Every sort in the sim breaks ties completely: `Relationships::Trim`'s comparator is a strict total order, and journeys are keyed by unique leaders. Unordered containers in the sim are lookup-only.
- **Saves and hashing are field by field.** Archives go through `Fields()` with no raw struct copies, so padding is never hashed or saved. Little-endian is static-asserted. No `long`/`size_t` in archived state.
- **Storage order survives a save.** `SaveDomainOrder`/`LoadStorageOrder` carry EnTT's history-dependent storage order across save and load. Exactness tests compare a game that saved, one that never did and one loaded from the save.
- **The tick protocol is thought through.** Bursts end at calendar, save, load and recording ticks; frozen ticks and the state counter are defined; pausing held during a scene change is replayed. Wall time only shapes bursts, which are invisible to the state (`Publish` is effectively read-only on sim state).

### Weaknesses and risks
1. **Delta saves trust bit-exact regeneration without checking it.** A save is now a delta against a world regenerated at load. Nothing verifies that the regenerated base matches the one the save was written against. The only guards are a hand-bumped `GENERATOR_VERSION` (1) and the data `BaseHash`, and dev-scene scratch bases have no version at all. Saves and replays carry no build identity: every commit is "0.1.0". A generation change without a version bump, a different compiler, or arm64 FMA contraction therefore applies the delta to a different base. The world loads silently wrong instead of being refused. Storing a hash of the base the save was written against costs one cached hash.
2. **Replays can't refuse a different binary.** The design promises same-binary determinism, but replays check only the version string and the game-data hash. Another commit, preset, mode or platform is accepted and then reported as a divergence instead of being refused.
3. **The simulation reads the render atlas.** `Manifest` sprite `footY` decides combat targeting (chest and head height). It is not in `DataHash`, so a regenerated atlas or a different assets folder changes the sim under the same binary and data hash. It is also a layering smell: the sim depends on art.
4. **The floating-point environment is set once and hardly checked.** It is set at thread start. Only the logic thread checks it, through a `PN_ASSERT` that release builds compile out. The main thread runs month-step chunks while it waits on its own joins and on decodes, and it also calls GPU drivers. One library or driver that sets FTZ/DAZ there would make month steps differ between runs without any alarm. The design doc says workers are checked; they aren't.
5. **Smaller issues:**
   - The design says contraction can't make a binary disagree with itself; that is stronger than LTO and constant folding guarantee.
   - Float-to-integer UB in `CellAt` on huge but finite saved positions.
   - The `Paths` cache is written lazily from parallel chunks, protected only by a warm-up convention.
   - f32 world coordinates quantise slow movement on very large procedural maps.
   - Hashing the whole state every 60 wall ticks while recording a replay may cause hitches on a 50k-NPC world.
   - There is no golden-value test that would catch an unannounced generator or code-generation change.

### Overall health
Determinism within one binary on one machine is solid and well tested. Machine-to-machine is mostly sound, apart from the floating-point environment gap. Where it is fragile is across builds and assets: delta saves, replay identity, and atlas-dependent combat can all go silently wrong rather than being refused. None of these is a crash. The delta-save issue is the one most likely to bite once saves start moving between versions or machines. These need fixing before the stated "reproducible bug reports" use of replays can be relied on.

**Files read in full:** src/core/random.hpp, src/core/math.hpp, src/core/math.cpp, src/core/jobs.hpp, src/core/jobs.cpp, src/core/schedule.hpp, src/core/stable_id.hpp, src/core/ecs.hpp, src/core/ecs.cpp, src/core/state.hpp, src/core/archive.hpp, src/core/delta.hpp, src/core/delta.cpp, src/core/replay.cpp, src/core/save_file.cpp, src/core/logic.cpp, src/core/paths.hpp, src/core/paths.cpp, src/core/journey.cpp, src/core/volley.cpp, src/core/fog.cpp, src/core/game_data.hpp, src/core/tick_clock.hpp, src/game/random_purposes.hpp, src/game/npcs.hpp, src/game/npcs.cpp, src/game/combat.cpp, src/game/combat.hpp, src/game/host.hpp, src/game/host.cpp, src/game/travel.hpp, src/game/travel.cpp, src/game/world_state.cpp, src/game/components.cpp, src/game/scenes/world.cpp, src/game/scenes/arena.cpp, src/game/scenes/arena_layout.cpp, src/app/game.cpp, src/build_info.hpp.in, xmake.lua, tests/support/exactness.hpp, tests/unit/core/random_tests.cpp, tests/unit/core/math_tests.cpp, tests/unit/core/jobs_tests.cpp, tests/unit/game/replay_tests.cpp, tests/unit/game/shipped_world_tests.cpp, tests/unit/app/game_tests.cpp, docs/design/0002-engine-architecture.md, docs/design/0002.2-replays.md

## Findings

### X-DETERMINISM-01: Delta saves apply to a regenerated base that is never verified: generator or code changes, or another compiler or platform, corrupt the world silently

- **Severity:** medium · **Verdict:** partially-confirmed · **Category:** determinism/saves · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:307`

A save of the game's world is a delta against `GenerateWorld(seed, data)`, which is regenerated at load (`ReadGameWorld`, host.cpp:307). Deltas address the base positionally: removed NPCs by their place in the base's sorted stable-ID list, journeys gone by base journey index, changed fields by mask over the base's values. The save records only seed, `GENERATOR_VERSION` (a hand-maintained constant, world_state.hpp:30, still 1) and `GameData::BaseHash()`, which covers the map and names but not the code. Nothing records a hash of the generated base itself. Dev-scene scratch saves (`ReadScratch`, host.cpp:366, `ScratchBase` at host.cpp:731) have no version of any kind: `BuildScratch` output (arena layout, `SpawnArenaNpcs`, `worldNpcs`) can change freely. The save file header (save_file.cpp: MAGIC, SAVE_FILE_VERSION, sizes, checksum) and `SAVE_FORMAT`/`ARENA_FORMAT` carry no build identity, and xmake.lua's version is 0.1.0 for every commit.

**Evidence:** host.cpp:164 `archive(world.state.Seed(), GENERATOR_VERSION, baseHash, world.state.Ids().next);`. host.cpp:288 compares only `baseHash != data.BaseHash()`, then host.cpp:307 `GenerateWorld(base, data, paths);` with no check that `WorldHash(base)` matches what the save was written against. Generation reads float maths that depends on code generation, e.g. npcs.cpp:114 `band.low + ((band.high - band.low) * unit)` (an FMA candidate on arm64, so powers can differ by an ulp and change `ProfileFor`/`DestinationWeight`). It also depends on `Travel::Seed`/`Visit`/`RouteDown` tie-breaking and `MakeTileLeg`, none of which moves `GENERATOR_VERSION` automatically.

**Impact:** Changing generation (routing tie-breaks, region derivation, StartingPower, `Travel::Seed`, the arena build) without remembering to bump `GENERATOR_VERSION` loads old saves against a different base. The same happens when a save moves between an x64 and a macOS-arm64 build, or between commits that share version 0.1.0. NPCs keep the new base's unchanged fields, 'gone' indices remove the wrong journeys, and changed-field masks patch other NPCs' values. `Adopt`'s consistency checks catch only part of this, so the player gets a silently different world instead of a refusal. Replay start states are saves too, so replays inherit the same problem.

**Recommendation:** When the base is made (host ctor, `ReadGameWorld`, `ScratchBase`), compute and cache a hash of it with `HashWorld`/`WorldHash` (already exists). Write that hash into the save next to `baseHash`, and refuse a load whose regenerated base hashes differently, logging both values. Do the same for the scratch base. This makes `GENERATOR_VERSION` a courtesy instead of the only guard. Also add a golden test of the shipped base's hash (see the test-coverage finding) so a generation change shows up in CI.

**Verification:** Checked host.cpp 164/280-293: the save stores seed, GENERATOR_VERSION and BaseHash, and loads compare only those. A hash of the regenerated base is never checked. That part holds. Design 0002 section 7.4 says results may differ across compilers and platforms, and section 7.6 refuses old saves rather than migrating them. So the cross-platform part is a documented limit, and GENERATOR_VERSION as a manual guard is the recorded design. The remaining gap is a forgotten version bump on a code change. A base-hash check would close it, so a medium to low hardening item.

### X-DETERMINISM-02: Replays identify their build by version string only (0.1.0 for every commit, preset and mode), so another binary is reported as a divergence instead of refused

- **Severity:** low (reviewer said medium) · **Verdict:** deliberate · **Category:** determinism/replays · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/replay.cpp:15`

The design promises replays deterministic only within one binary (0002 §7.4, 0002.2 §1). `Replay::StartPlaying` checks only `PN_GAME_VERSION` (xmake.lua `set_version("0.1.0")`, the same on every commit) and the game-data hash. Replays from another commit, from debug vs release vs releasedbg, from GCC vs Clang vs MSVC, or from x64 vs arm64 are all accepted and then fail with 'the replay diverged at wall tick N'.

**Evidence:** replay.cpp:15 `constexpr std::string_view VERSION = PN_GAME_VERSION;`. build_info.hpp.in defines only PN_GAME_VERSION, PN_SOURCE_DATA and PN_FUZZ_CORPUS. StartPlaying compares `version != VERSION` and `recordedDataHash != dataHash`, nothing else.

**Impact:** A recorded replay attached to a bug report and played after the fix, or on a teammate's different preset, looks like a new determinism bug. Real divergences become indistinguishable from expected build differences, which undermines the 'reproducible bug reports' goal. On macOS arm64, debug and release builds contract FMAs differently, so cross-mode playback is likely to diverge.

**Recommendation:** Stamp a build identity into build_info (git commit plus a dirty flag, toolchain, preset, mode, target arch) and record it in the replay header. Refuse on mismatch with a message saying which part differs, or warn and continue if cross-build playback is still wanted for debugging. Consider recording it in saves too for diagnostics.

**Verification:** Design 0002 section 7.4 says determinism holds 'within one executable', and across compilers and platforms results may differ. A replay checks the version string and the data hash. A divergence report on another build is the expected failure, so refusing earlier would be an improvement rather than a fix. I did not open replay.cpp to confirm the exact checks.

### X-DETERMINISM-03: Combat results depend on the atlas manifest (sprite footY), which is neither part of the replay's data hash nor game data

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** determinism/architecture · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/combat.cpp:45`

`HeightOf`/`ChestOf`/`HeadOf` read `manifest.Sprite(look.sprite).footY * look.scale` from assets/manifest.yaml, loaded at runtime. `Tag()` uses the chest height to decide which foxes a cast tags (the radius test against the chest), and the result also sets the volley origin, flight ticks and target points. These are all sim state that is saved and hashed. `GameData::Hash()` covers creatures, abilities, combat, the world map and names, not the manifest. Replays therefore accept a different atlas.

**Evidence:** combat.cpp:43-62 `static_cast<f32>(manifest.Sprite(look.sprite).footY) * look.scale`; combat.cpp:72-86 `Tag` filters by `SquaredDistance(point, chest) <= reach`; logic.cpp:401 `m_replay->StartRecording(Serialise(true), m_simulation->DataHash());`, where DataHash is `m_data.Hash()` (game_data.hpp:88-92) with no manifest.

**Impact:** Re-running `project_n atlas` after art changes, or running the same binary against another assets folder, changes which enemies are hit and when bolts arrive, under an identical binary and data hash. Old replays then 'diverge' for no code reason. It is also a layering problem: simulation outcomes depend on render art, so an art tweak is a gameplay change.

**Recommendation:** Move the sim-relevant body geometry (chest and head height per creature kind) into game data (creatures.yaml/CreatureData) so the sim never reads the atlas. At minimum, fold a hash of the sim-read manifest fields into the replay header and refuse on mismatch.

**Verification:** combat.cpp:45 does read manifest footY for sim geometry. Design 0002 line 469 says the manifest holds metadata the sim needs (hitboxes and similar) and that its asset IDs survive saves and replays, so the sim reading the manifest is the intended design. art/atlas.sha256 ties the manifest to its sources. The factual gap is real: GameData::Hash does not cover the manifest, so a replay accepts a changed atlas. This is a minor hardening item, not a bug.

### X-DETERMINISM-04: FP environment is set once per thread and checked only on the logic thread by a release-compiled-out assert; the main thread runs month-step chunks

- **Severity:** low (reviewer said medium) · **Verdict:** unverifiable · **Category:** determinism/threads · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/jobs.cpp:40`

`SetFloatingPointEnvironment` runs once on each worker (enkiTS threadStart, jobs.cpp:169), once on the main thread (app.cpp:265) and once on the logic thread. The only re-check is `PN_ASSERT(math::FloatingPointEnvironmentIsSet())` on the logic thread after each tick (logic.cpp:35), and `PN_ASSERT` compiles to nothing in release builds. Workers are never re-checked, despite the design's 'set and checked on the logic thread and every worker' (0002 §7.4, line 262). The main thread is enkiTS thread 0: `Pending::~Pending` (jobs.cpp:142) waits with the default priority and so runs HIGH-priority tasks. Its own `ParallelFor` joins (jobs.cpp:231) also run any HIGH task, including the logic thread's `MonthStep`/`Travel::SendOut` chunks. In between, the main thread calls GPU drivers (Vulkan/D3D12/Metal), SDL and FreeType. Workers run third-party decode, rasterise and Zstd code at low priority.

**Evidence:** jobs.cpp:40-47 `ChunkTask::ExecuteRange` calls the body with no environment check or set. app.cpp:262-265 comment: 'waiting on its low-priority work, it may run the logic's chunks, which need the simulation's environment'. logic.cpp:35 is the only runtime check.

**Impact:** If any driver or library sets FTZ/DAZ or changes rounding on the main thread or a worker, subnormal-sensitive arithmetic in chunks that happen to run there differs from other runs. A month step's result then depends on which thread picked up a chunk: nondeterministic, the same binary disagreeing with itself. A shipped build never notices; only a replay hash mismatch would show it, and it would be hard to reproduce. It may be related to the known flaky, unreproduced replay test.

**Recommendation:** In `ChunkTask::ExecuteRange` (and the low-priority task bodies), check `FloatingPointEnvironmentIsSet()` at entry and, if it is wrong, restore it with `SetFloatingPointEnvironment()` and log once. It costs one MXCSR/FPCR read per chunk of 1,024 NPCs. Keep the logic-thread check in release builds as a logged error instead of an assert. Add a test that sets FTZ on the forking thread before `ParallelFor` and checks every chunk still sees the simulation's environment. Correct 0002 §7.4 to match what is done.

**Verification:** Design 0002 line 262 does promise the FP environment is 'set and checked on the logic thread and every worker at start'. That means at start, so the claim that workers are never re-checked is consistent with the doc. Whether a driver changes MXCSR on the main thread cannot be decided by reading. I did not check the main-thread chunk execution path or the logic.cpp:35 assert. The risk is plausible but speculative.

### X-DETERMINISM-05: No golden-value regression test: generator, compiler-flag or code-generation changes that alter the sim go unnoticed

- **Severity:** low · **Verdict:** unverifiable · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/game/shipped_world_tests.cpp:414`

Determinism tests compare runs within one process: saved vs never-saved vs loaded, 1 vs 4 workers, record vs play. Only `core::Random` has a golden value (random_tests.cpp). There is no committed expected `WorldHash` for the shipped world from `SEED`, nor after a fixed month step, and no bit-exact golden for `core::math` (math_tests compare to the platform within ulps).

**Evidence:** `rg GENERATOR_VERSION tests` finds only refusal tests (save_tests.cpp:1568). Every hash comparison is between two in-process runs.

**Impact:** Changing generation without bumping `GENERATOR_VERSION` (see the delta-save finding) passes CI. So does a flag change such as `-march=x86-64-v3`, which with GCC's default `-ffp-contract=fast` would start emitting FMAs and silently change every sim result and save base. So does a refactor of `core::math` that shifts results by an ulp.

**Recommendation:** Add a test asserting the shipped base world's `WorldHash()` (and the `StateHash()` after one `AdvanceCalendar`) equals a committed constant, with a comment telling whoever updates it to bump `GENERATOR_VERSION`. Add a few bit-exact goldens for `math::Sin/Cos/Exp/Log/Atan2`. Run them on every preset; any per-platform difference is then visible, which is useful even without a cross-platform goal.

**Verification:** Not checked: I did not search the tests for golden hashes. Only the claim that GENERATOR_VERSION appears in refusal tests was left unverified. A golden WorldHash test is a plausible suggestion, but it would need updating on every legitimate generator change, which is a trade-off.

### X-DETERMINISM-06: `CellAt` converts unbounded floats to u32 (UB) for huge but finite positions accepted from saves

- **Severity:** low · **Verdict:** confirmed · **Category:** undefined-behaviour · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/npcs.cpp:132`

`static_cast<u32>(std::max(point.y / CELL_SIZE, 0.0F))` has no upper clamp, and `row * MAP_COLUMNS` can overflow. `Body::Valid()` accepts any finite value (e.g. 1e38), and `Deactivate` → `CellAt` runs in `ClearScene` when a loaded arena scene is left. The x clamp is fine for finite values but passes NaN through (`std::clamp(NaN, …)` returns NaN); only `Valid()` prevents that today. Similarly, `Spin::Valid` accepts any finite speed, so `std::remainder(current + speed, TURN)` can overflow to inf and produce a NaN that the game's own next save then refuses on load.

**Evidence:** npcs.cpp:131-135; components.hpp Body::Valid `core::IsFinite(previous) && core::IsFinite(current)`; host.cpp `spin.current = std::remainder(spin.current + spin.speed, TURN)`.

**Impact:** A crafted or corrupt save reaches float-to-integer UB (UBSan float-cast-overflow; x86 and ARM give different results). The fuzz target can hit it. A NaN spin can make a game unsaveable-then-unloadable.

**Recommendation:** Clamp row like column (to a bounded row count, or reject positions beyond a world bound in `Body::Valid`), and compute in i64 with a range check. Bound `Spin::speed` (e.g. |speed| <= TURN) in `Valid`.

**Verification:** The cited code is at lines 131-135 and matches the claim. The row is cast to u32 with only a lower clamp (std::max), so a huge finite y gives float-to-integer UB. The column is clamped, but the row is not. I did not verify that Body::Valid accepts huge values, and the Spin claim is unchecked. Real but low severity.

### X-DETERMINISM-07: `Paths::To` lazily writes its cache and is called from parallel chunks; safety rests on a warm-up convention checked only by assertions

- **Severity:** low · **Verdict:** unverifiable · **Category:** thread-safety · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/paths.cpp:230`

`Paths::To` fills `m_maps[profile][city]` on first use without synchronisation. `Travel::SendOut` calls it from `ParallelFor` chunks (`ChooseDestination`, `RouteDown`) and relies on `paths.Warm(WALKING)` plus `Warm(band.profile)` beforehand. If a future profile source (e.g. a mount or convoy profile, or a ProfileFor change) is not warmed, two chunks build the same slot concurrently: a data race on `std::optional<DistanceMap>`. In release nothing asserts.

**Evidence:** paths.cpp:230-238 `if (!slot) { slot = DistanceMap::Build(...); }`; travel.cpp SendOut warms only WALKING and `map.Travel().bands` profiles.

**Impact:** Latent UB and possible crash or garbage routes in month steps once travel gains new profiles; nondeterministic results at worst.

**Recommendation:** Split the API: a const `Paths::Warmed(city, profile)` for chunk code that asserts or returns an error if the slot is empty, and the lazy `To` only for the single-threaded caller. Or warm every profile at map load (Profiles().size() × cities maps; measure the memory).

**Verification:** I did not open paths.cpp or travel.cpp. The claim is a latent race that depends on a warm-up convention, and it can only be settled by reading those files. The finding itself says no current profile is unwarmed, which makes it a robustness suggestion.

### X-DETERMINISM-08: Hashing the whole state every 60 wall ticks while recording or playing a replay may hitch the logic thread on a 50k-NPC world

- **Severity:** low · **Verdict:** unverifiable · **Category:** performance · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/logic.cpp:289`

`CheckReplay` calls `Logic::Hash()` → `Host::StateHash()` every `REPLAY_HASH_TICKS` (60) wall ticks while recording or playing. It walks every world NPC, every relationship edge (up to 10-50 per NPC), every journey with its tile and arrive vectors, and the journals, mixing each scalar with two SplitMix rounds. That is plausibly millions of mixes per second, done inline in the tick loop.

**Evidence:** logic.cpp:296-307; host.cpp `HashWorld` hashes the world domain, relationships and travel whole.

**Impact:** Possible tick overruns or hitches once a second during recording and playback, growing linearly with world size (procedural maps may be far larger). Unmeasured.

**Recommendation:** Measure with `profile` while recording on the shipped world. If it is significant, hash the world part only when it can have changed (cache the world hash per month step and fold in the cheap active domain each check), or hash on a job.

**Verification:** CheckReplay (logic.cpp:289-307) hashes every 60 wall ticks, inline, as described. Whether it hitches depends on world size; needs a profile run. The reviewer itself says unmeasured. Hash cost is linear and once a second, so likely minor.

### X-DETERMINISM-09: f32 world positions quantise slow movement on large maps: walking at MIN_WALK_SPEED stalls beyond about 1,000 tiles

- **Severity:** low · **Verdict:** confirmed · **Category:** scaling/float · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/game_data.hpp:24`

Positions are f32 world units (tile 256). `Host::Walk` does `body.current + move * walkSpeed`. At |x| ≥ 2^18 (1,024 tiles) the ulp is 0.03125, so a step of `MIN_WALK_SPEED` 0.01 rounds to no movement. At 2^17 it rounds up to 0.0156, 56% too fast. Partly deflected sticks hit the same per-axis quantisation earlier on huge maps. The MIN_WALK_SPEED comment reasons only about arrive ticks ('100,000 tiles still fits'). It is deterministic, but the movement is wrong.

**Evidence:** game_data.hpp:21-24; host.cpp `body.current = body.current + (move * m_data.cultivator.walkSpeed);`; world_map.cpp TileCentre `(column + 0.5F) * tileSize` in f32.

**Impact:** On large procedural maps (an explicit goal), free movement becomes direction-dependent and stalls at low speeds, and interpolated positions jitter.

**Recommendation:** Before maps grow, decide on world-coordinate precision: tile-relative positions (tile index plus f32 offset), or f64 sim positions narrowed to f32 only for rendering relative to the camera. Record the limit in the spec.

**Verification:** MIN_WALK_SPEED is f32 0.01 (game_data.hpp:24), and its comment only reasons about arrive ticks. f32 ulp arithmetic is correct: above 2^18 the ulp is 0.03125, so a 0.01 step is lost. It is deterministic. Only matters on maps over about 1000 tiles, which are not shipped yet, so it is a scaling note.

### X-DETERMINISM-10: The claim that contraction can't make a binary disagree with itself is stronger than LTO, inlining and constant folding guarantee

- **Severity:** info (reviewer said low) · **Verdict:** deliberate · **Category:** determinism/float · **Status:** contradicts-recorded-decision · **Reviewer confidence:** low
- **Where:** `docs/design/0002-engine-architecture.md:267`

§7.4 keeps `-ffp-contract` unset because 'contraction can't make a binary disagree with itself'. With LTO (-flto=thin, /GL) the same source expression is inlined into several call sites, and each instance may be fused or not, or constant-folded unfused when an operand is known at compile time. Two computations of the same value can then differ by an ulp within one binary. Today the sim mostly tolerates this: bitwise comparisons (`Travel::SaveDelta`'s `same(Rebuild(plan), journey)`, `LegWalk::Fits`) fall back to saving whole. But any future check that recomputes a float result elsewhere and expects equality is exposed. On the shipped x86-64 baseline (no `-march`, so no FMA) contraction does nothing, so the decision buys nothing there and only makes macOS arm64 differ.

**Evidence:** xmake.lua has no `-ffp-contract`/`/fp:contract` and enables LTO (lines 36-55). Candidate fused expressions in sim code: npcs.cpp:114, journey.cpp `Between`, components.cpp `OrbitPoint`, volley.cpp `WaveStart` (whose monotonicity argument assumes separately rounded steps).

**Impact:** Low today. It makes arm64 results differ from x64 for generation (relevant to the delta-save finding) and leaves a latent within-binary inconsistency should code begin comparing recomputed floats.

**Recommendation:** Reopen the decision cheaply: compile core/ and game/ with `-ffp-contract=off` (MSVC is already off). There is no cost on x64, where no FMA is emitted, and arm64 then matches x64 bit for bit for basic operations, which makes saves portable in practice. Otherwise, reword the doc to 'per call site' and forbid recompute-and-compare patterns in the sim.

**Verification:** Section 7.4 records this decision explicitly, with its reasoning and a revisit trigger ('only if replays or saves must ever match across builds'). Per-call-site fusion under LTO is a fair nuance. The doc's claim is about one binary on the same inputs, and I did not find a current recompute-and-compare path that breaks it. Rewording the doc would be a reasonable change.
