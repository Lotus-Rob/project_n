# Review: gap1-third-party-library-usage (part 2)

### GAP1-THIRD-PARTY-LIBRARY-USAGE-11: The rotating log file never rotates by size, so one run's debug log grows without limit

- **Severity:** low · **Verdict:** confirmed · **Category:** resource-usage/logging · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/log.cpp:54`

`RotatingFileSinkConfig` sets append mode, rotation on creation and 9 backups, but no `set_rotation_max_file_size` or time rotation. The root logger runs at Debug in every build, including release, and the simulation logs at Debug on recurring events: every death, dash, cast, respawn, month step and walk-to.

**Evidence:** log.cpp:54-59; log.cpp:68 `logger->set_log_level(quill::LogLevel::Debug);`; e.g. combat.cpp:33, :480, :524, arena_layout.cpp:163.

**Impact:** Long sessions or fast replays (thousands of ticks of combat per second) can write very large `project_n.log` files into the per-user folder. There is no cap, unlike the 10-file limit across runs.

**Recommendation:** Set a maximum file size (e.g. 32 MiB) so rotation also happens within a run, and document it beside the 9-backup rule in log.hpp.

**Verification:** log.cpp:54-59 config sets append mode, rotation on creation and max backups only, with no max file size. The logger is set to Debug in all builds (line 68). Unbounded growth within a run is real, though it depends on log volume. I found no recorded decision.

### GAP1-THIRD-PARTY-LIBRARY-USAGE-12: Domain/State Create and Destroy carry on after a failed PN_ASSERT: undefined behaviour or orphaned entities in release

- **Severity:** low · **Verdict:** confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/ecs.cpp:16`

`Domain::Destroy` asserts that the ID is found and then calls `m_registry.destroy(found->second); m_entities.erase(found);` even when `found == end()`, which is undefined behaviour with assertions off (release). `State::Destroy` and `State::Move` do the same with `m_where`. `Domain::Create` with a duplicate ID leaves a second registry entity carrying the StableId while the map keeps pointing at the first, an orphan that still iterates, saves and hashes. Other code in the repo chooses a defined fallback 'with assertions off' (Travel::Start, Travel::Forget, Host::EnterScene).

**Evidence:** ecs.cpp:16-20; state.hpp `Destroy`: `PN_ASSERT(found != m_where.end(), ...); (*this)[found->second].Destroy(stableId);`; `Move` likewise.

**Impact:** A logic slip that debug builds would catch becomes memory corruption in shipped builds instead of a no-op.

**Recommendation:** Return early, or for Create return entt::null and refuse, when the precondition fails, as the rest of the code does under `// with assertions off`.

**Verification:** ecs.cpp:16-20: Domain::Destroy asserts then dereferences found->second without a guard, which is UB in release if the ID is missing. Create asserts on duplicate insertion but has already created the entity, leaving an orphan. I did not re-read State::Destroy/Move. These are invariant violations, so low.

### GAP1-THIRD-PARTY-LIBRARY-USAGE-13: CaptionOpacity, a few-flop pure function, carries a profiler zone

- **Severity:** low (reviewer said info) · **Verdict:** confirmed · **Category:** profiling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/gfx/labels.cpp:606`

`CaptionOpacity(f32 pixels, f32 scale)` computes a smoothstep in about five operations and opens a `PN_PROFILE_ZONE()`. It is called once a frame (renderer.cpp:782). The zone (with an enforced callstack) costs far more than the work, and in the capture it shows as a separate entry with nothing to learn from.

**Evidence:** labels.cpp:605-611.

**Impact:** Negligible time, but it clutters captures and contradicts the src/AGENTS.md guidance on zones for trivial functions.

**Recommendation:** Drop the zone; the caller's zone covers it.

**Verification:** labels.cpp:606 CaptionOpacity has PN_PROFILE_ZONE on a trivial pure function. src/AGENTS.md:46 says small functions called many times take no zone. It is called once per frame (per the finding), so the rule's wording ('thousands of times a frame') is not strictly breached, but the zone is still pointless clutter.

### GAP1-THIRD-PARTY-LIBRARY-USAGE-14: The quill backend thread polls every 100 µs for the whole run and is not counted against the job workers

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** performance/power · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/log.cpp:39`

The default `BackendOptions::sleep_duration = 100 µs` makes the backend wake about 10,000 times a second to poll every frontend queue, although the game logs a few lines a second. `Jobs::DefaultWorkers()` sizes enkiTS as `hardware - OTHER_THREADS` with `OTHER_THREADS = 2 // the main thread and the logic thread`, leaving out this always-alive thread.

**Evidence:** BackendOptions.h:76 `std::chrono::nanoseconds sleep_duration = std::chrono::microseconds{100};`; jobs.cpp:14 `constexpr u32 OTHER_THREADS = 2;`.

**Impact:** Constant wakeups cost power on laptops and handhelds (Steam Deck through Proton) and add scheduler noise next to fully loaded workers during the month step and the publish. It is small, but it lasts for the whole session.

**Recommendation:** Start the backend with `sleep_duration` of about 1–5 ms (`flush_log` and assertion flushes just wait up to that long). Optionally count it in OTHER_THREADS, or document why not.

**Verification:** log.cpp Start() calls Backend::start() with default options, so the 100us poll holds. The wakeup claim is factual but quill's backend also uses sleep/yield backoff and the power effect is unmeasured. No decision is recorded either way. OTHER_THREADS omission is minor. Treat as a measure-first spike, not a defect.

### GAP1-THIRD-PARTY-LIBRARY-USAGE-15: The EnTT facade is the raw registry: the 'no structural changes inside a job chunk' rule is unenforced

- **Severity:** info (reviewer said low) · **Verdict:** unverifiable · **Category:** architecture · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/ecs.hpp:29`

`core::Registry` is `entt::registry`, and `Domain::Entities()` hands out a mutable reference. Every game module calls EnTT directly (view, storage, emplace, remove). The chunk bodies of `MonthStep::Run` (npcs.cpp:351) and `Travel::SendOut`/`SetOut` (travel.cpp:124, :413) receive mutable `auto& npcs = world.storage<Npc>()` storages, which allow `emplace`/`erase` from worker threads. The src/AGENTS.md rule 'structural changes (create, destroy) never happen inside a job chunk' rests on review alone. State::Create says 'Never inside a job chunk' but nothing asserts it.

**Evidence:** npcs.cpp:336-372; travel.cpp:401-424; ecs.hpp:29-31.

**Impact:** A future chunk that emplaces a tag (e.g. marking an NPC 'met' or 'travelling' through a component) would race on EnTT's packed and sparse arrays: undefined behaviour and nondeterministic iteration order that only shows up as a rare desync.

**Recommendation:** Give chunks a narrower handle, such as a span or accessor over the component values of one storage plus const storages for lookups. Alternatively make State::Create/Destroy and Domain::Create/Destroy `PN_ASSERT` that the calling thread is not an enkiTS worker (Jobs can say whether the current thread is a worker).

**Verification:** Domain::Entities() returning a mutable registry and the lack of a runtime assertion are plausible, but this is an architectural opinion. I did not verify the chunk bodies in npcs.cpp or travel.cpp. The rule is stated in src/AGENTS.md as a review convention. Worth a cheap worker-thread assert, but no bug exists today.

### GAP1-THIRD-PARTY-LIBRARY-USAGE-16: Component reference stability is undocumented while code holds references across calls that could reorder the same pool

- **Severity:** info (reviewer said low) · **Verdict:** unverifiable · **Category:** api-docs/lifetime · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/ecs.hpp:30`

ecs.hpp documents only that an `Entity` is 'valid only inside its domain and within one tick'. It never says that a component reference or pointer is invalidated by any emplace (reallocation) or remove/destroy (swap-and-pop moves the last element into the hole) on the same storage. Code depends on this not happening: `TickAfflictions` holds `AfflictionState&` across `Combat::Damage` (combat.cpp:145-165), `Combat::Cast` holds `Caster*` across `Start` (:206-232), and `Combat::Damage` holds `Health*` across `emplace<Dying>` (:464-479). Each is safe today only because those calls touch other pools.

**Evidence:** combat.cpp:145 `AfflictionState& affliction = AfflictionOf(entities.get<Afflictions>(entity), element);` then :165 `Combat::Damage(state, target, ...)`.

**Impact:** A later change such as 'curing afflictions on death inside Damage' or 'emplacing Caster state on cast' would silently leave dangling references in the simulation. That is undefined behaviour that could also desync replays.

**Recommendation:** Document the pointer-stability rule in ecs.hpp ('a component reference lasts until the next emplace, remove or destroy on its type'). Optionally re-fetch after calls that may change pools in combat.cpp.

**Verification:** This is a documentation gap about reference stability, with no present bug by the finding's own admission. I did not verify the combat.cpp line citations. Adding a one-line comment to ecs.hpp is a reasonable improvement, nothing more.

### GAP1-THIRD-PARTY-LIBRARY-USAGE-17: Each entity costs two node-based unordered_map entries, multiplied by every world copy

- **Severity:** info (reviewer said low) · **Verdict:** unverifiable · **Category:** scaling/memory · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/ecs.hpp:71`

Every stable ID lives in `Domain::m_entities` (`std::unordered_map<u64, Entity>`) and in `State::m_where` (`std::unordered_map<u64, DomainIndex>`). A node-based map costs roughly 40–64 bytes per entry plus a heap allocation. The Host keeps several worlds (m_world, m_base, m_scratch, m_scratchBase, and during a load the loaded world and its base), each with both maps. The delta code does `Find` hash lookups per entity per component (ComparePresence: two per base place per component; SaveComponentChanges and EntitiesAt again), and `CopyOf` rebuilds both maps entity by entity.

**Evidence:** ecs.hpp:71; state.hpp `m_where`; delta.hpp:185-201; host.cpp:237-256.

**Impact:** At 100k world NPCs this is tens of MB and cache-missing hash lookups on save and load. It grows linearly as procedural worlds grow, and save and load run on the logic thread (a hitch).

**Recommendation:** Use a flat open-addressing map, or exploit that world IDs are dense from 1 (a vector indexed by ID with a scene-range side table). For the deltas, walk the StableId storage once to build place↔entity arrays instead of calling Find per component.

**Verification:** Two unordered_maps per entity (ecs.cpp uses m_entities, State has m_where) is plausible. The 100k NPC figure and the save/load hitch were not measured. Per the repo's rule that unknowns are measured spikes, this needs measuring, not a guess.

### GAP1-THIRD-PARTY-LIBRARY-USAGE-18: MonthStep and SetOut look each entity up by sparse index to reach a value already at packed index i

- **Severity:** info · **Verdict:** confirmed · **Category:** performance · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/npcs.cpp:360`

The chunk loops iterate the Npc storage by packed index (`const core::Entity entity = npcs[i];`), then call `npcs.get(entity)` (sparse page → packed index i again) twice, and do random-access `needs.get(entity)` and `ids.get(entity)`. After GenerateWorld's SortCanonical the Npc, Needs and StableId storages are in the same order, but nothing relies on or keeps that.

**Evidence:** npcs.cpp:360-370; travel.cpp:132-134.

**Impact:** A small constant factor per NPC per month step (a few redundant sparse loads and cache misses). It matters only at very large NPC counts, but the month step is the known main-thread hitch.

**Recommendation:** Fetch the Npc reference once per entity (`Npc& person = npcs.get(entity)`), or iterate `npcs.each()` ranges per chunk. Consider keeping Needs sorted as the Npc storage (`registry.sort<Needs, Npc>()`) after structural changes, so the lookups walk memory in order.

**Verification:** npcs.cpp:358-370 matches: npcs.get(entity) called twice plus needs/ids lookups. Only a small constant factor; a valid micro-optimisation, not a defect.

### GAP1-THIRD-PARTY-LIBRARY-USAGE-19: Quill timestamps use the TSC clock by default; System is simpler for a game's diagnostic log

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** logging · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/log.cpp:67`

`create_or_get_logger(name, sinks)` takes the default `ClockSourceType::Tsc`. Timestamps come from rdtsc and are calibrated by the backend, which resyncs every 500 ms (`rdtsc_resync_interval`). Under Wine/Proton, in VMs, or on CPUs without an invariant TSC, the mapping can drift between resyncs.

**Evidence:** Frontend.h:294/323 `ClockSourceType clock_source = ClockSourceType::Tsc`; BackendOptions.h:288.

**Impact:** Log timestamps may be off by up to a resync interval relative to wall-clock events (SDL, Tracy, crash reports), which makes correlation harder. At this log rate the TSC brings no performance benefit.

**Recommendation:** Pass `quill::ClockSourceType::System` to both create_or_get_logger calls.

**Verification:** log.cpp:67 uses create_or_get_logger with no clock source, so the default TSC applies. The drift claim under Wine/VMs is unverified and speculative; Quill recalibrates against the system clock. Nice-to-have at most.

### GAP1-THIRD-PARTY-LIBRARY-USAGE-20: An unbounded blocking queue lets a runaway log loop take up to 2 GiB per thread before it blocks

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** resource-usage/logging · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/log.cpp:67`

The default `FrontendOptions` (`UnboundedBlocking`, 128 KiB initial, `unbounded_queue_max_capacity` 2 GiB) apply to every logging thread: main, logic, each enkiTS worker that logs, and SDL's threads through LogFromSdl. A log statement in a hot loop (or an SDL category flooding at Debug, since RouteSdlLogs enables Debug for all categories) grows the queue toward 2 GiB per thread before the thread blocks.

**Evidence:** FrontendOptions.h:53/63/76; sdl_log.cpp:35 `SDL_SetLogPriorities(SDL_LOG_PRIORITY_DEBUG)`.

**Impact:** A logging bug turns into large memory growth and then stalls the simulation thread, instead of losing some lines. This is unlikely given the 'never every frame' rule, but there is no backstop.

**Recommendation:** Use a FrontendOptions subtype with a smaller `unbounded_queue_max_capacity` (e.g. 64 MiB) and accept blocking at that point, or rate-limit (`QUILL_LOG_*_LIMIT`) the paths that can repeat. Consider setting SDL categories individually rather than enabling Debug for all of them.

**Verification:** log.cpp has no FrontendOptions override, so the Quill defaults apply (unbounded blocking, large cap). A runaway-log scenario is hypothetical. The Debug-for-all-SDL-categories point is fair but speculative.
