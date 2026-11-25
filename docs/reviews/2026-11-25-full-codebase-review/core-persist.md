# Review: core-persist

**Scope:** src/core/delta.{hpp,cpp}, archive.hpp, save_file.cpp, saves.hpp, replay.{hpp,cpp}, handover.hpp; plus tests/fuzz/save.cpp, tests/fuzz/replay.cpp and their corpora layout. Focus additionally on format versioning, corruption/truncation handling, endianness, atomic writes, hostile input. Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 24 standing (0 critical, 0 high, 1 medium, 16 low, 7 info), 0 raised but refuted on verification.

**Parts:** core-persist.md, core-persist-part2.md, core-persist-part3.md

## Assessment

## core-persist: assessment

**Overall health: good.** This is some of the most carefully built code in the repo. The archive reader treats every byte as untrusted. Each count is checked against the bytes left before anything is reserved, and a vector is replaced only once it has been read whole. The delta loader refuses every malformed layout I could construct: places that don't ascend or run past the base, a component gained and then lost or changed in the same section, an empty or out-of-range mask, created IDs that are already used or were never handed out, and values their type's `Valid()` rejects. The order section correctly keeps EnTT storage order apart from content, so a game that saves keeps running exactly as one that never did. The save container checks magic, version, both sizes, a ratio cap, zstd's own frame content size and a checksum, and it writes to a temp file and renames it. Replay parsing checks action kinds, slots, finite points, held-input range, that hashes ascend, and that no bytes are left over. Handover and Display are small, guarded by a lock, documented for thread-safety analysis and covered by a TSan stress test. The unit tests in this area are explicit, use hand-built byte layouts for refusals, and are well named.

**Weaknesses and risks:**
1. **Versioning and identity depend on discipline, not mechanism.**
   - A save is a delta against a world that the loading binary regenerates. The only guards on that base are a hand-bumped `GENERATOR_VERSION` (still 1 after many generation-affecting commits) and a data hash.
   - Nothing checks that the regenerated base matches the one the save was written against. Cross-binary floating-point differences are explicitly not a goal, and contraction is left on. Either one would make a save load silently against a different base. The spec lists that outcome as an anti-goal.
   - Likewise, changing a component's `Fields()` gets no automatic version bump, so an old save is misparsed instead of refused.
2. **Hostile-input amplification.**
   - The 1 GiB decompression cap is enforced, but a replay expands a 1-byte action into a 56-byte `Action`. A highly compressible replay file of tens of KB can therefore demand tens of GB.
   - The known `ReadTicks` reserve problem is a special case of this.
   - No fuzz target sees the zstd container, so this path is unfuzzed.
3. **Fuzzing has gaps.**
   - The targets skip `ReadSaveFile` and `Logic::Restore`, check no save→load round trip, and run under ASan only, without UBSan.
   - Corpus replays never run in CI or in `check`.
   - Committed seed files are never checked to still load. The test called "seeds load" regenerates the seeds in memory.
   - The replay seed is pinned to version string "0.1.0", so after a version bump it only exercises the refusal path.
4. **Test gaps.** No test reaches the checksum-mismatch path. The replay never-recorded test leaves out `ToggleRecording` and `PlayReplay`. The cap on writes over 1 GiB is untested (known).
5. **Platform risk.** `path.string()` is used in the save logs and in error messages. On MSVC it throws for user-profile paths outside the ANSI code page. One of those calls is the success log of every background save, so a throw there escapes the enkiTS task and terminates the game.
6. **Minor issues.** A fixed `.tmp` name, an unchecked close, no fsync (documented), stale members after a mode change in `Replay`, no bounds assert in `Handover::ReturnState`, and edge cases in `Hasher` for scalars wider than 8 bytes. The order section writes a storage's full ID list after any churn in it, which grows with map size.

**Scaling.** Saving is O(components × N) hash lookups, plus two sorts and a full ID list for each storage that changed membership. That is fine at 50k NPCs. At millions of NPCs the logic thread would stall and the order section would grow, so varint or permutation-diff encodings would be worth measuring.

**Bottom line.** The code is correct for honest saves and robust against crashes on malformed bytes. The main exposure is **silent** mis-loads: a base that regenerates differently, or a layout change shipped without a version bump. A base fingerprint and an automatic layout fingerprint in the save would close both cheaply.

**Files read in full:** src/core/delta.hpp, src/core/delta.cpp, src/core/archive.hpp, src/core/save_file.cpp, src/core/save_file.hpp, src/core/saves.hpp, src/core/saves.cpp, src/core/replay.hpp, src/core/replay.cpp, src/core/handover.hpp, tests/fuzz/save.cpp, tests/fuzz/replay.cpp, tests/unit/core/delta_tests.cpp, tests/unit/core/archive_tests.cpp, tests/unit/core/replay_tests.cpp, tests/unit/core/save_file_tests.cpp, tests/unit/core/saves_tests.cpp, tests/unit/core/handover_tests.cpp, tests/fuzz/corpus/replay/seed (hexdump), tests/fuzz/corpus/save/* (header fields parsed)

## Findings

### CORE-PERSIST-01: The save checksum's mismatch path is untested

- **Severity:** medium · **Verdict:** confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/core/save_file_tests.cpp:119`

save_file.cpp:150 refuses a file whose decompressed bytes don't match the stored checksum. Its own comment says this exists because 'a flipped bit inside a literal would decompress all the same'. No test reaches that branch. 'a truncated or corrupted save fails to read' flips the last byte and a middle byte, which zstd itself most likely rejects (block structure, or content size), and it only checks that the read fails, not why. grep finds no test mentioning 'checksum'.

**Evidence:** save_file.cpp:36-49, 150-152. save_file_tests.cpp:119-139 only checks `CHECK_FALSE(readCorrupted.has_value())`.

**Impact:** A regression that drops or breaks the checksum comparison, or changes Hasher so the checksum ignores data, passes every test. Silent corruption of literal bytes would then load as a valid save.

**Recommendation:** Add `TEST_CASE("a save whose checksum doesn't match its contents is refused as its checksum")`: write a save, flip one bit of the header's checksum field (offset 8+4+8+8), and CHECK that the reason is 'its checksum doesn't match'. Optionally also save an incompressible pattern (a raw zstd block) and flip one payload byte.

**Verification:** grep -rn checksum tests/unit returned nothing, so no test reaches the checksum mismatch branch. The corrupted-save test only checks that the read fails.

### CORE-PERSIST-02: Save base identity has no fingerprint: a base regenerated differently loads silently

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** design/determinism · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/delta.hpp:458`

A world save is the delta from a base that the *loading* binary regenerates (GenerateWorld from the saved seed). The only identity checks (host.cpp ReadGameWorld ~269-290) are GENERATOR_VERSION, which is bumped by hand and has been 1 since 9be1c40a despite many generation-affecting commits, and GameData::BaseHash() (data only). SaveDomainDelta writes nothing that identifies the base itself: no entity count, no hash of the base domain or travel. The recorded decision is determinism 'within one binary', with -ffp-contract left on and no cross-compiler goal. Yet a save must be applied to a base regenerated by a later build or another compiler: a game update, linux-gcc vs linux-clang, or arm64 macOS where clang contracts to FMA. In that case unchanged fields take the new binary's values, places index a different sorted-ID list, and journeys are rebuilt differently, all without a word.

**Evidence:** delta.hpp:459-485 writes removed/created/sections only. host.cpp:263-300 checks only generator, baseHash and the version, then GenerateWorld(base, data, paths). world_state.hpp:30 `GENERATOR_VERSION = 1`. docs/design/0002 §7.4 says no determinism across compilers. The spec 2026-11-16 chunk5c1 anti-goals list 'A save that loads against a different base without a word'.

**Impact:** The spec's anti-goal 'a save that loads against a different base without a word' can happen. A forgotten GENERATOR_VERSION bump, or a compiler or flag change that alters a generated float (StartingPower, routes), yields a silently different world rather than a refusal. This is hard to debug because the hash checks after load compare against the loaded state.

**Recommendation:** Write a fingerprint of the base into the world section: Host::WorldHash of the freshly generated base (or SaveDomain hashed plus travel.Save hashed), alongside the base entity count. Compare it after GenerateWorld in ReadGameWorld/ReadScratch and refuse with a logged reason on mismatch. It costs one hash pass at save and load, and it also guards GENERATOR_VERSION discipline automatically.

**Verification:** The facts hold. host.cpp:164 and 280-284 check only seed, GENERATOR_VERSION and baseHash, and GENERATOR_VERSION is 1 (world_state.hpp:30). No fingerprint of the regenerated base is written. src/AGENTS.md says the base's identity is exactly seed, GENERATOR_VERSION and BaseHash, so this is a deliberate design. The spec lists 'loads against a different base without a word' as an anti-goal, and the hand-bumped version is its only guard. Pre-1.0 there is no compatibility promise, and the cross-compiler scenarios are speculative. The suggestion is a reasonable hardening, so low.

### CORE-PERSIST-03: No mechanical guard ties a component's Fields() layout to WORLD_VERSION / ACTIVE_VERSION

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** versioning · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/delta.hpp:5`

The delta and whole-domain formats are defined implicitly by each component's Fields() argument list and its scalar sizes. Adding, removing, reordering or resizing a field of Npc, Needs, Met or Group changes the wire layout. Unless a developer remembers to bump WORLD_VERSION (and ACTIVE_VERSION for active components), an old save passes every version check and is read misaligned. Valid() catches only some values (e.g. Npc::action range, finite power); u32 cell/home and counts can read as plausible garbage. The fuzz-seed test regenerates seeds in memory, so it does not catch a missed bump either.

**Evidence:** host.cpp:85-92 constants ARENA_FORMAT=10, WORLD_VERSION=8. delta.hpp:52-87 FieldBytes compares each value through Fields(). Nothing hashes the layout.

**Impact:** Before 1.0 saves may break freely (recorded decision), but they should be *refused*, not misread into a corrupted world that later trips invariants or simply plays wrong.

**Recommendation:** Derive a layout fingerprint at compile time or start-up from each listed component (e.g. a Hasher over FieldCount<C>() and the bytes a default C writes, plus the component list length) and write it beside WORLD_VERSION and ACTIVE_VERSION. Refuse on mismatch. Alternatively, add a golden-bytes test that pins a default-state save's bytes next to the version constants, so a layout change fails the test until the version and the golden bytes are updated together.

**Verification:** Fields() layout is not fingerprinted, and WORLD_VERSION and ACTIVE_VERSION are manual. I did not check Valid() in detail. The memory note says saves may break freely before 1.0, and the failure mode is a stale local save, which is not a shipped hazard. The golden-bytes suggestion is sound but not urgent.

### CORE-PERSIST-04: Replay load amplifies 1 decompressed byte into a 56-byte Action: a small compressible replay can OOM the game

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** hostile-input · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/replay.cpp:95`

ReadTicks bounds the tick count by the bytes left, but each action costs 1 byte on the wire (a kind with no fields) and becomes a full core::Action in memory: kind, StableId, shared_ptr<GameData>, SceneId, Slot, Vec2 and u64, about 56 bytes, up to double that during vector growth. Replay files pass through ReadSaveFile, which allows up to MAX_SAVE_BYTES (1 GiB) decompressed at a ratio of up to 32768:1. A replay of ~2^22 ticks, each holding 255 'Pause' kinds, is about 1 GiB decompressed but only tens to hundreds of KB on disk (highly repetitive). StartPlaying would then push about 1e9 Actions (56+ GB). On top of that come tickStarts and held (16 B per tick) and the retained 1 GiB m_bytes.

**Evidence:** replay.cpp:91-105 `actions.push_back(read)` per kind byte. frame.hpp:514-529 Action layout. save_file.hpp:25 MAX_SAVE_BYTES = 1<<30. save_file.cpp:18 MAX_RATIO = 32768.

**Impact:** A corrupted or crafted replay file in the saves folder crashes the game with an out-of-memory error on F10, instead of being refused. The fuzz target can't find this: it feeds raw bytes, bypasses zstd, and libFuzzer's default max_len is 4096.

**Recommendation:** Store actions compactly while parsing: reserve nothing, and cap total actions and ticks at a sane limit (e.g. the ticks of N hours at 60 Hz, and a per-replay action budget). Or store the parsed stream as a compact on-wire form and decode each tick lazily in Next(). Also cap the replay file's decompressed size well below 1 GiB. Add a test with a large but well-formed tick count of 255-action ticks to assert refusal.

**Verification:** replay.cpp:83-105 bounds ticks only by Remaining(). Each 1-byte kind becomes a full Action via push_back. MAX_SAVE_BYTES is 1 GiB and MAX_RATIO is 32768 (save_file.cpp:18, 135-136), so a small crafted file can decompress to a huge size. The amplification is real. The only source is a hostile or corrupted file the player puts in their own saves folder, so the impact is limited to a local OOM crash.

### CORE-PERSIST-05: path.string() in save logs and errors throws on MSVC for non-ANSI profile paths; one call runs inside the background save job

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** platform/error-handling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/saves.cpp:19`

MSVC's std::filesystem::path::string() converts through the ANSI code page and throws std::system_error ('No mapping for the Unicode character exists in the target multi-byte code page') when the path holds a character outside it. The save slot lives under the per-user data folder (SDL_GetPrefPath), whose path includes the Windows user name. `PN_LOG_INFO("saved the game to {}", slot.string())` runs inside the enkiTS background task after every successful save. FileError (save_file.cpp:33) and Logic::Load's logs (logic.cpp:383, 388) do the same on the logic thread.

**Evidence:** saves.cpp:19, save_file.cpp:33, logic.cpp:383/388 call path.string(). gfx/image.cpp:13 already uses u8string because 'SDL's paths are UTF-8'.

**Impact:** A Windows player whose profile path has characters outside the system code page (e.g. a CJK or Cyrillic user name on a Western-locale system) crashes with std::terminate on the first save, because the exception escapes the background task. Loading crashes the same way, on the logic thread. This is plausible for a wuxia game's audience. Elsewhere the repo already converts paths with u8string (gfx/image.cpp:13, platform/paths.cpp:13).

**Recommendation:** Add one core helper (e.g. `core::PathText(const std::filesystem::path&)`) that returns a UTF-8 std::string from u8string(). Use it in saves.cpp, save_file.cpp, logic.cpp, settings.cpp, yaml.cpp, app.cpp and main.cpp, and have lint flag `.string()` on paths. Add a unit test that formats a path holding non-ASCII characters.

**Verification:** saves.cpp:19 and save_file.cpp:33 do call path.string() on the save path. MSVC's string() throws for characters outside the ANSI code page. In saves.cpp:19 the call sits in the Background lambda, so an exception would escape it. It is a Windows-only, plausible scenario, and I could not verify it here. Whether the lambda catches exceptions is unchecked. Severity is lower because the user-name path must contain non-ANSI characters.

### CORE-PERSIST-06: Committed fuzz seeds are never checked to still load; the replay seed has no generator or check at all

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** test-coverage/fuzzing · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/game/save_exactness_tests.cpp:486`

'the save fuzz target's seeds load in its game' regenerates the four seeds in memory (Saved(fight), WorldSeed(), DeltaSeed(), ScratchSeed()) and checks those load. It never reads tests/fuzz/corpus/save/*. A layout, version or test-data change makes the committed seeds refusal-only, and no test fails. tests/AGENTS.md says 'regenerate all four whenever the save layout changes', but that rule is enforced by memory. The replay seed (tests/fuzz/corpus/replay/seed, 119 bytes) is hand-made: no test writes it or checks it is accepted. It embeds the version string "0.1.0" and a 2-byte opaque start state, so the next `set_version` bump turns every replay fuzz run into a fuzz of the version-refusal branch. The fuzz corpus replay (-runs=0) only asserts no crash.

**Evidence:** save_exactness_tests.cpp:486-498 uses in-memory bytes. The replay seed hexdump shows `05000000 05 '0.1.0'` and data hash 0. No test references tests/fuzz/corpus/replay.

**Impact:** Fuzzing quietly loses most of its coverage after format or version changes. The savings from seeding mid-fight and delta states disappear without anyone noticing.

**Recommendation:** Extend the seed test to read each committed seed file and CHECK both that it loads in the fuzz game and that it equals the freshly generated bytes. Add a skipped 'write the replay seed' case and a plain 'the replay fuzz seed plays' case (StartPlaying succeeds and every tick is fed). Build the replay seed's version from PN_GAME_VERSION so a release regenerates it.

**Verification:** save_exactness_tests.cpp:486-498 builds the seeds in memory (Saved, WorldSeed, DeltaSeed, ScratchSeed) and never reads the committed files in tests/fuzz/corpus. A stale committed seed would go unnoticed. I did not inspect the replay seed or its tests. The impact is reduced fuzz coverage only.

### CORE-PERSIST-07: Fuzz targets skip the zstd container and Logic::Restore, check no round trip, and run under ASan only

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** fuzzing · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/fuzz/save.cpp:73`

fuzz_save feeds bytes straight into Host::Load. ReadSaveFile (header, sizes, ratio, frame content size, checksum) and Logic::Restore's prefix (paused/calendar for replays, SAVE_FORMAT, tick counters) are never fuzzed. fuzz_replay calls StartPlaying on raw bytes, again skipping ReadSaveFile, and never restores the start state. After a successful load the target saves again but discards the bytes; it does not reload them and compare StateHash, so a Writer/Reader asymmetry for a field (read but not written, or written twice) goes unnoticed. The targets build only with -fsanitize=address (tests/xmake.lua:135); UBSan (shifts, signed overflow, invalid enum/bool loads, float-to-int casts on hostile values) is never enabled while fuzzing.

**Evidence:** tests/fuzz/save.cpp:77-91; tests/fuzz/replay.cpp:24-33; tests/xmake.lua:135-150.

**Impact:** Whole classes of hostile-input defects in this scope sit outside fuzzing: decompression bombs, counter wrap, UB in loaders and later ticks, save/load asymmetry.

**Recommendation:** (1) In fuzz_save, when Load succeeds, Save to bytes, Load those into a second host (or Restart, then Load), and abort if StateHash differs. (2) Add a `save_file` fuzz target that writes the input to a temp path and calls ReadSaveFile (or refactor ReadSaveFile into a span-taking core to avoid I/O). (3) Build the fuzz targets with `-fsanitize=address,undefined -fno-sanitize-recover=undefined`. (4) Have fuzz_replay call Logic::Restore on the start state through a fake Simulation.

**Verification:** tests/xmake.lua:135 enables the fuzz targets only under sanitizer == address, so UBSan is not applied. The lack of a save-and-reload StateHash comparison and the skipped ReadSaveFile layer are plausible from the structure. I did not read tests/fuzz/save.cpp line by line. This is a fuzzing-depth suggestion, not a defect.

### CORE-PERSIST-08: Fuzz targets and their seed corpora never run in CI or in `check`

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** process/fuzzing · **Status:** contradicts-recorded-decision · **Reviewer confidence:** high
- **Where:** `tests/xmake.lua:135`

fuzz_* targets exist only for linux-clang with `--sanitizer address`. That combination runs only under `check --sanitizers`, on request (tests/AGENTS.md). CI's address jobs are windows-msvc and macos-clang, and the Linux container builds are plain. So a change that breaks a fuzz target's build, or makes a seed crash under ASan, merges unnoticed.

**Evidence:** tests/xmake.lua:132-150; .github/workflows/ci.yml lines 248-253 and 323-328 (address only on windows and macos); tests/AGENTS.md 'Sanitizers run on request, never in plain check'.

**Impact:** The 'each replays its seeds as a test' safety net is effectively manual. The save and replay readers, the main untrusted-input surface, go unexercised between explicit sanitizer runs.

**Recommendation:** Add a short linux-clang ASan job (or a step in the existing linux-clang container job) that builds the fuzz_* targets and runs `-runs=0` over the corpora, or a time-boxed `-max_total_time=60`. Cost: one sanitizer build of core, data and game. The recorded decision covers running *all* tests under sanitizers on request; this asks only for the fuzz corpora.

**Verification:** Facts check out: tests/xmake.lua:135 gates fuzz targets on linux-clang+address, and ci.yml has address only on windows/macos. But tests/AGENTS.md says fuzz targets are covered by `check --sanitizers`, on request. That is a recorded decision (sanitizers on request), so it is a deliberate trade-off. The proposal to run only the corpora in CI is a reasonable suggestion, not a defect.

### CORE-PERSIST-09: Replay test of never-recorded kinds omits ToggleRecording and PlayReplay

- **Severity:** low · **Verdict:** confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/core/replay_tests.cpp:180`

NeverRecorded (replay.cpp:20-24) refuses seven kinds. The test loops over Load, ReloadData, SceneResident, SceneLeft and SceneEntered only. If ToggleRecording or PlayReplay were dropped from NeverRecorded, a replay holding ToggleRecording would make Logic call ToggleRecording during playback. That runs StartRecording, which flips m_mode to Recording mid-playback and leaves m_start pointing into the freed buffer.

**Evidence:** replay_tests.cpp:180-190 vs replay.cpp:20-24.

**Impact:** A regression in the refusal list would go undetected, and the consequence (recording starting during playback) is a confusing state-machine break.

**Recommendation:** Add ActionKind::ToggleRecording and ActionKind::PlayReplay to the loop, and name the test after all seven.

**Verification:** replay.cpp:20-24 lists 7 kinds. replay_tests.cpp:180-190 loops over only 5, so ToggleRecording and PlayReplay are untested. The consequence described is plausible but unproven (I did not trace Logic's handling).

### CORE-PERSIST-10: WriteSaveFile: fixed temp name, close errors ignored, write errors carry no reason

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** error-handling/atomic-writes · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/save_file.cpp:78`

(a) The temp file is always `<slot>.tmp`. Two game instances saving the same slot both open it with trunc; one can rename the other's half-written file over the slot. The checksum then refuses it on load, but the old save is gone. (b) After a successful flush, the ofstream destructor closes the file without checking; on network or quota filesystems the error can surface only at close, and the truncated temp file is then renamed over a good save. (c) Open, write and flush failures all report 'the write or flush failed' with no errno, so 'permission denied' and 'disk full' look the same. (d) No fsync of the file or directory before or after the rename (documented in save_file.hpp as deferred). With delayed allocation on btrfs or XFS, a power loss can leave a zero-length slot.

**Evidence:** save_file.cpp:78-96; save_file.hpp:27-31 'Safe against the game crashing, not against the machine losing power (no fsync)'.

**Impact:** Rare loss of the previous good save, and logs that don't say why a save failed.

**Recommendation:** Use a unique temp name (slot + '.' + pid or a random suffix + '.tmp'). Call output.close() explicitly and check it before renaming. Capture errno (std::error_code from std::system_category) in the message. When saves start to matter, add a platform flush helper (fsync / FlushFileBuffers on the file, then fsync on the directory on POSIX).

**Verification:** save_file.cpp:78-96 matches: fixed `.tmp` name, no explicit close check (the flush is checked, so (b) is weak), a generic error message with no errno, and no fsync. The fsync gap is documented as deferred in save_file.hpp:27-31. The shared temp name matters only with two instances on one slot, which is rare.
