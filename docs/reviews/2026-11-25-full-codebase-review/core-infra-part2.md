# Review: core-infra (part 2)

### CORE-INFRA-11: Random purposes are allocated in three modules with no central registry or uniqueness check

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/names.cpp:12`

core::Random purposes live in game/random_purposes.hpp (1-14), core/names.cpp (0x4e414d45 'NAME'), core/journey.cpp (0x53544159 'STAY') and gfx/tile_hash.hpp (9, 'retired from game ... nothing here may reuse it'). Nothing checks across modules that two uses don't share a purpose.

**Evidence:** names.cpp:12, journey.cpp:14, random_purposes.hpp:35-36 comment.

**Impact:** A future purpose that collides makes two systems draw correlated numbers, a subtle simulation bug. The 'retired 9' comment shows collisions are already managed by memory.

**Recommendation:** Move every purpose into one core header (core sits below game and gfx) as an enum or constants, with a static_assert or unit test that all values are distinct.

**Verification:** Purposes are indeed spread over game/random_purposes.hpp (1-14), names.cpp (NAME) and journey.cpp (STAY). I found no cross-module distinctness check. The collision risk is low: the purpose is only one of several Random inputs (seed, tick, id, purpose, draw), and the values are mnemonic or in a disjoint range. This is a design nicety.

### CORE-INFRA-12: Division by zero in RunsOn/InSlice and ChunkCount; Rate/RunsOn unused in production

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** undefined-behaviour · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/schedule.hpp:27`

RunsOn computes `% rate.every` and InSlice `% every`, with no check that every ≥ 1 (Rate's doc says 'at least 1'). ChunkCount (jobs.hpp:17) divides by chunkSize with no check, can overflow on items + chunkSize - 1, and truncates the quotient to u32. These are constexpr, so PN_ASSERT is not used. Rate and RunsOn have no production caller; only game/npcs.cpp uses InSlice.

**Evidence:** `return simulationTick % rate.every == rate.offset;`; `return static_cast<u32>((items + chunkSize - 1) / chunkSize);`

**Impact:** A zero rate or chunk size from data or a refactor is UB rather than an assert. The unused API adds surface area to maintain.

**Recommendation:** Add PN_ASSERT (usable in constexpr functions at run time) or `if consteval` guards for every/chunkSize > 0 and for the u32 range. Remove Rate/RunsOn until a system uses them, or note that they are kept on purpose.

**Verification:** RunsOn and InSlice do take % every with no guard, and ChunkCount divides by chunkSize. All production ChunkCount callers (npcs, travel, host, text_place, volley_draw, draw_list) pass nonzero constant chunk sizes, so no real UB path exists. RunsOn has no production caller (grep found only its definition). The Rate doc states 'at least 1'. The u32 truncation is theoretical. Hygiene only.

### CORE-INFRA-13: NaN inputs to PackUnorm16 and ScaledColour reach lround(NaN), which is unspecified

- **Severity:** low · **Verdict:** confirmed · **Category:** undefined-behaviour · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/instances.cpp:79`

std::clamp(NaN, lo, hi) returns NaN, and std::lround(NaN) raises FE_INVALID with an unspecified result (LONG_MIN on x86, 0 on aarch64), which is then narrowed to u16/u32. ScaledColour (ui_colours.cpp:68) does the same per channel. Its doc covers below 0 and above 1 but not NaN. Dissolve, glow values and opacities come from fades computed with divisions.

**Evidence:** `static_cast<u16>(std::lround(std::clamp(value, 0.0F, 1.0F) * ...))`

**Impact:** A NaN fade gives platform-dependent colours or dissolve. Results agree within one binary, but the contract has a hole and the FE_INVALID flag is raised.

**Recommendation:** Treat NaN explicitly (`if (!(value > 0)) return 0;` before the clamp), document it, and add a NaN case to the unorm16 and ScaledColour tests.

**Verification:** instances.cpp:79 PackUnorm16 is lround(clamp(value,0,1)*max). std::clamp passes NaN through, and lround(NaN) is unspecified. The code does what the finding says. Whether a NaN fade actually occurs in production is unverified, so the impact is speculative.

### CORE-INFRA-14: DrawList copies every instance and ID twice per publish

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/draw_list.cpp:118`

Publish first copies each part's instances, IDs and entries into m_instances, m_ids and m_entries, then sorts, then gathers m_instances[m_entries[i].index] into the snapshot. Instances are 56 bytes, so with the stress scene's ~100,000 instances that is an extra ~5.6 MB plus the IDs copied per publish, only so the gather can index one flat array.

**Evidence:** draw_list.cpp:118-128 copies the parts; :137-144 gathers from m_instances.

**Impact:** Avoidable memory traffic on the logic thread's publish path (memory: 'Avoid wasted work').

**Recommendation:** Copy only the entries (with the part offset added, as now). In the gather, find each source by part, either with the part index packed into SortEntry or by an upper_bound on m_offsets per chunk, and read straight from m_parts[p].instances. That drops m_instances and m_ids.

**Verification:** draw_list.cpp:118-128 copies the part instances and ids into m_instances and m_ids. The gather at 137-144 then reads from them. The extra copy is real and removable, but it is parallelised, and removing it would need part lookup in the gather. Gain is unmeasured. Under the project's 'unknowns are measured spikes' and 'avoid wasted work' notes it is a legitimate but low-priority item.

### CORE-INFRA-15: MakeInstance calls f64 std::remainder twice per instance, even for unrotated sprites

- **Severity:** low · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/instances.cpp:108`

Each MakeInstance computes `std::remainder(current, 2π)` and `std::remainder(current - previous, 2π)` in f64 through libm. Most instances (ground, props, crowds, travellers) have both rotations 0. MakeInstance runs for every published sprite every publish.

**Evidence:** `const f64 current = std::remainder(static_cast<f64>(draw.currentRotation), TURN);`

**Impact:** Tens of nanoseconds per instance of libm work on a hot path, for a result that is 0 in the common case.

**Recommendation:** Fast path: if both rotations are 0, pack 0. Otherwise, or in addition, reduce only when |rotation| > π.

**Verification:** instances.cpp:108-111 calls std::remainder twice in f64 per MakeInstance, unconditionally. A zero-rotation fast path is valid. The cost is small, tens of ns, and unmeasured.

### CORE-INFRA-16: UI colours stored as 8-bit linear premultiplied lose distinct dark shades in the planned colour editor

- **Severity:** low · **Verdict:** unverifiable · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/ui_colours.hpp:201`

UiColours keeps every colour as 8-bit linear premultiplied, and the header admits that several sRGB bytes share one linear byte in the dark and that some bright linear bytes are unreachable. Roughly sRGB 0-13 all map to linear 0. The accessibility menu planned to edit these (memory: recolour any UI element) would round-trip a player's dark or near-white choice to a different shade.

**Evidence:** ui_colours.hpp:201-205 ToSrgb doc; ui_colours_tests.cpp:108-125 allows ±1 step.

**Impact:** An accessibility colour picker can't keep exactly what the player picked. Dark text-ink and outline colours, which matter most for contrast, are hit hardest.

**Recommendation:** Store the player's choice as straight sRGB8 plus alpha (what the menu and settings file show) and convert to linear premultiplied when drawing (or cache the converted table). Alternatively keep 16-bit linear. Decide before the menu ships.

**Verification:** The 8-bit linear premultiplied storage and the lost dark shades are real and the header admits them. The colour editor is only planned, so whether this matters depends on its design. A design question to settle before the editor ships, not a current defect.

### CORE-INFRA-17: No per-run size cap on the log file

- **Severity:** low · **Verdict:** confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/log.cpp:54`

RotatingFileSinkConfig sets rotation on creation and 9 backups but no maximum file size, so a single run's project_n.log grows without bound. Debug level goes to the file, so a repeated warning or a long session can fill the disk.

**Evidence:** log.cpp:54-59 config without a size limit.

**Impact:** Unbounded disk use in long sessions or failure loops.

**Recommendation:** Set `config.set_rotation_max_file_size(...)` (for example 64 MiB) so a single run also rotates.

**Verification:** log.cpp:54-59 sets rotation on creation, 9 backups and append mode, with no max file size, so one run's log is unbounded. Rotation happens only at startup, so a long run or a warning loop could grow it without limit. Severity is low.

### CORE-INFRA-18: Draw-list tests never use more than one worker or cross a gather chunk

- **Severity:** low · **Verdict:** confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/core/draw_list_tests.cpp:150`

DrawList::Publish promises 'the bytes are the same whatever its worker count' and gathers in GATHER_CHUNK = 8,192 chunks, but every test uses `core::Jobs jobs{ 1 }` with fewer than 10 instances. The multi-part path is tested only with one worker, and the chunk boundary (total > 8,192) and groundBatches across appended publishes are not tested.

**Evidence:** Every TEST_CASE in draw_list_tests.cpp builds `core::Jobs jobs{ 1 };`.

**Impact:** A regression in the offset arithmetic at chunk boundaries or in the parallel copy would pass the unit tests.

**Recommendation:** Add one case with several parts and ~9,000 instances, published with Jobs{1} and Jobs{7}, comparing the snapshot bytes and IDs.

**Verification:** draw_list_tests.cpp uses `core::Jobs jobs{ 1 }` in every case I grepped (150, 169, 197, 209, 222). I did not check the instance counts or whether any test crosses GATHER_CHUNK, so the claim about fewer than 10 instances is unchecked. Multi-worker byte equality is untested.

### CORE-INFRA-19: module.dox and 0002's module table describe core as value types and facades; it is now a 12.6k-line catch-all with game-domain logic

- **Severity:** low (reviewer said info) · **Verdict:** confirmed · **Category:** architecture · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/module.dox:1`

module.dox says core holds 'Value types, pure functions and the facades ... the frame contract, timing, the handover, logging and profiling'. Core now has 83 files and ~12.6k lines, including world-map generation, pathfinding, journeys, volleys, combat rules, abilities, names, saves, replays and the logic loop. The cause is structural: anything both game and gfx need must sit in core, because game links only core.

**Evidence:** `ls src/core/*.cpp src/core/*.hpp | wc -l` = 83; `cat ... | wc -l` = 12,654.

**Impact:** Core has no internal layering: infrastructure (jobs, log, math) and game content share one target, rebuild unit and test binary, and the description misleads newcomers.

**Recommendation:** Update module.dox to describe what core really holds. Consider a module between core and game/gfx (for example `world` or `sim` for the map, paths, journeys, volleys and names) so core stays infrastructure; record the decision in a design doc.

**Verification:** module.dox says core holds 'value types, pure functions and the facades'. The task text lists world_map, paths, journey, volley, combat_rules, abilities, saves, replay and logic in core, so the description is out of date. I did not recount the 83 files or 12.6k lines. The architectural remedy is an opinion.

### CORE-INFRA-20: src/AGENTS.md is 45 KB in 50 'lines', far past the ~100-lines, one-rule-per-line guide

- **Severity:** low · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/AGENTS.md:27`

The root AGENTS.md rule: 'an AGENTS.md holds what any task in its directory needs (~100 lines, one line per rule)'. src/AGENTS.md's Scope line and the arena/host rule (line 27) are each many kilobytes of prose (line 27 is about 15 KB) describing the world map, travel, fog and saves in implementation detail. That is spec or Outcome material, not one rule per line.

**Evidence:** `wc -c src/AGENTS.md` = 45,264 bytes.

**Impact:** Every agent working anywhere in src/ loads ~45 KB of context, much of it irrelevant to its task, and it drifts from the code (it must be updated in the same commit as each change).

**Recommendation:** Move the subsystem descriptions to docs/design or per-directory AGENTS.md files (game/, gfx/), and keep src/AGENTS.md to short rules with pointers.

**Verification:** src/AGENTS.md is 45,264 bytes in 49 lines, and the longest line is 15,404 bytes. That is far outside the root rule of about 100 lines with one line per rule. I did not check whether the project records this as a deliberate choice.
