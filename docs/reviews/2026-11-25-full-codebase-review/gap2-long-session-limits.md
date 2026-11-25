# Review: gap2-long-session-limits

**Scope:** Make a table of every counter, clock, ID and time value in src/ and shaders/ that grows during play or with world age. Include simulation ticks (u64), world ticks (i64), the calendar day and month, journey arrive ticks (u32), journal days, stay drift, volley start ticks, use IDs and ability-use sequence numbers, StableId and EnTT entity versions (recycling), handover and snapshot epochs, wallTick, display_delay, tick_clock accumulators, the f32 render or shader time uniforms (glow age, cloud bob, river flow, seal spin, sprite dissolve, ground noise), glyph-cache and label-cache keys, the log, and the replay's per-tick action count and tick list. For each one give the type, the growth rate, the play time or world age (and the save count) at which it overflows, wraps or loses precision visibly, what happens then, and whether saves carry it, so the limit can be reached across many loads. Earlier units found single instances (MakeTileLeg's u32 arrive ticks, glow age at 2^23 ticks, DemoPulse counted from tick 0, near-u64-max tick counters accepted on load, EnTT's 2^20 live-entity limit, the u8 replay action count, f32 world positions). Do not repeat those; place them in the table and find the rest. The planned game runs for many in-game years and real play sessions of tens of hours, so judge each limit against that. (Why this unit exists: Precision and overflow limits were found one at a time by module reviewers (journey, shaders, combat, third-party), but nobody made a systematic inventory of every growing quantity and the session length or world age at which it breaks. A ToI-like game is played for tens of hours and simulates years, so a missed u32 or f32 time value is a likely late, hard-to-reproduce bug.)

**Findings:** 6 standing (0 critical, 0 high, 0 medium, 3 low, 3 info), 0 raised but refuted on verification.

## Assessment

## gap2-long-session-limits: inventory of growing quantities

**Units used below.** 60 ticks/s. 2^23 ticks = 38.8 h, 2^24 = 77.7 h, 2^31 = 414 days, 2^32 = 828 days, 2^53 = 4.75 million years. A world day is 180 ticks (3 s) and a month is 5,400 ticks (90 s). The world clock moves only on the world map while it is not paused. So 10 h on the map is about 400 months, which is 33 "years" if a year is 12 months (the calendar has no year yet). `TickCounters` (wall, simulation and state) are saved, so any limit keyed on the simulation tick covers the **whole campaign across every load**, not one session. The simulation tick also keeps running while the world is paused (GAP1-CLOCK-01), so idle time counts too.

| Quantity | Where | Type | Growth | Breaks at | What happens | Saved? | Status |
|---|---|---|---|---|---|---|---|
| Wall / simulation / state ticks | core/tick_clock.hpp TickCounters | u64 | 60/s (wall always; simulation unless frozen) | 2^64 ticks, 9.7e9 years | none in practice | yes (Logic::Serialise) | near-max values are accepted on load (known CORE-PERSIST-17) |
| TickClock schedule | tick_clock.hpp TickTime, tick_clock.cpp Advance | u64 ns, index×1e9 | m_index +60/s, reset only by Restart/catch-up | index 1.8e10 = **9.75 years without a Restart**; (now-origin+1)×60 also at 9.75 years | u64 wrap, schedule garbage | no | info (new, theoretical) |
| platform::NowNs | SDL_GetTicksNS | u64 ns | 1e9/s since SDL init | 584 years | – | no | fine |
| FrameClock | app/view.cpp | u64 ns -> f64 s | wall time, 0.1 s step cap | f64 s exact enough for ~1e8 years | – | no | fine |
| Render clock RenderTick | tick_clock.hpp | f64 ticks | 60/s | 2^53 ticks | – | derived | fine |
| Display delay | core/display_delay.cpp | u64 ns, size_t m_observed | 60/s | never | – | no | fine |
| Handover LateAdoptions, taken lateness | core/handover.hpp | u64 | per frame | never | – | no | fine |
| Timeline epoch | core/logic.cpp m_epoch | u64 | +1 per load, replay or scene change | never | – | no | fine |
| Scene change number | frame.hpp SceneChange.change | u64 | per scene change | never | – | no | fine |
| Snapshot id (epoch, wallTick) | gfx/renderer.cpp:459 | u64 pair | per tick | never | reused by waiting bursts | – | known GFX-RENDERER-02 |
| World clock | game/world_state.hpp worldTicks | u64 (cast to i64 for journeys) | 60/s on the map only | TRAVEL_END = i64max/2 (2.4e9 years); CALENDAR_END | travel stops / clock stops | yes | fine |
| Months passed | WorldState::months, CalendarView.month | u64 | 1 per 90 s of map time | LAST_MONTH (3.4e15) | – | yes | the date line has no year, so it reads "month 400" after 10 h (known GD-WORLD-SIM-07) |
| Day of month | CalendarView.day | u32 | 0..29 | – | – | derived | fine |
| Leg start / stay ticks | core/journey.hpp | i64 | absolute world ticks | i64; StaySound/LegSound bound them | – | yes | fine |
| Leg arrive ticks | Leg::arrive | u32, relative to the leg | per leg | 2^32 ticks per leg | MakeTileLeg only asserts | yes | known Audit 13 / CORE-JOURNEY-VOLLEY-09 |
| Stay drift days | StayWhere/StayWalked | i64 day -> u64 Random key | per stay | – | O(days) per draw | yes (stay ticks) | known CORE-JOURNEY-VOLLEY-02 |
| Stay seed | Stay::seed | u32 | per journey | birthday collisions around 65k journeys | same drift | yes | known CORE-JOURNEY-VOLLEY-13 |
| Journal departureDay | game/travel.hpp | i32, clamped | – | 2^31 days = 204 years of map time | clamps | yes | known Audit 11 (clamp untested) |
| Journal daysAway | game/travel.hpp | **u8, clamped at 255** | grows with each NPC's reach, which grows with power, which grows with world age | 255 days = 46k ticks; reachable on maps wider than ~1,440 cost after ~60 cultivating months | journal says 255 for longer trips | yes | **new (F2)** |
| Relationship lastMet | npcs.hpp | u64 month | – | never | – | yes | fine |
| NPC power, opinion | npcs.hpp | f32, +1 per month or meeting | per month | 2^24 increments (never) | absorption | yes | growth without limit is known (GD-WORLD-SIM-03) |
| Volley startTick, Launch/Arrival ticks | core/volley.hpp | u64 simulation tick | per cast | never | – | yes | two tick types in core (known CORE-JOURNEY-VOLLEY-17) |
| VolleyTarget diedTick | volley.hpp | u64, 0 = alive | – | – | relies on simulation ticks starting at 1 | yes | fine |
| Ability use ID | core/frame.hpp AbilityUse | u64 = tick<<8 or source | per use | tick 2^56 (3.8e7 years) | top bits lost | yes, inside flights | collision on a zero cooldown is known (GAME-COMBAT-14) |
| Cooldown readyTick, affliction nextTick, Dying startTick, Flash startTick | game/combat.hpp | u64 | per tick | never | – | yes | fine |
| StableId (world) | core/state.hpp | u64 in [1, 2^62) | per NPC or group made | 2^62 | – | yes | fine |
| StableId (scene) | core/state.hpp | u64 in [2^62, 2^63), one counter per session | per spawn or respawn (the arena churns foxes) | 2^62 spawns | – | yes | fine |
| EnTT entity | core/ecs.hpp | entt::entity: 20-bit index, 12-bit version | version +1 per destroy at a slot | **live index 2^20** (known); version wraps after 4,096 recycles of a slot | a stale handle would alias a new entity | no | 2^20 known (GAP1-THIRD-PARTY-04); version wrap is safe only by convention (F6) |
| Fog version | Host::m_fogVersion | u64 | per exploration change | never | – | no | fine |
| JourneysStamp | game/travel.cpp NextStamp | atomic u64 | per change | never | – | no | fine |
| Label pass, caption fades frame | gfx/labels.hpp | u64 | per frame | never | – | no | fine |
| Text layout cache | gfx/text_layout.cpp | key: string; lastUsed: f64 frame clock | per new text | evicted after LAYOUT_SECONDS | – | no | fine |
| Glyph cache | text_layout.cpp, shelf_packer | glyph index u32, pages u32 | per new glyph, never evicted | font glyph count (CJK: ~30 pages of 4 MB) | memory | no | known GFX-TEXT-14 |
| Glow age (sprite shader) | gfx/glow_table.cpp, sprite.vert.hlsl:179 | low 32 bits of the tick; age float(int(diff)) | per tick | sub-tick fraction lost from 2^23 ticks of age; int wraps at 2^31 | pulse stutters | the simulation tick is saved | known SHADERS-05; DemoPulse ages from tick 0 (known GAP1-CLOCK-08) |
| Cloud bob | gfx/cloud_draw.cpp:408 | f64 frame clock / period, reduced, then f32 | wall time | never | – | no | fine (runs while paused: known GAP1-CLOCK-05) |
| Seal spin and light streams | gfx/seal_draw.cpp SealSpin, SealWrap | f64 fmod before f32; lap u32 wraps | render seconds since the seal's start; the portal starts at tick 0 | never | – | portal: from the saved tick | fine; portal clocked from tick 0 (F5) |
| Orbit angle | game/components.cpp OrbitPoint | f64 phase + speed×tick, fmod | absolute simulation tick | 2^53 | **a speed change jumps by Δspeed×tick** | tick saved | **new (F1)**; spawn at tick 0 is known (GAME-SCENES-01) |
| Spin | host.cpp:893 | f32, remainder each tick | per tick | never | – | yes | fine |
| Dissolve | game/combat.cpp DissolveAt | age, at most dyingTicks | – | – | – | yes | fine |
| Damage number age | app/damage_numbers.cpp:228 | f64 difference -> f32 | – | – | – | no | fine |
| HUD warning and cooldowns | app/hud.cpp | f64 render ticks; Clear() on a new epoch | – | – | – | no | fine |
| River flow, ground noise | shaders | – | **no time input exists** | – | – | – | n/a |
| Player walk distance | Host::m_playerWalk | f64 | +step per tick, never reset in a session | ~1e15 units | – | no | fine |
| Replay recording | core/replay.cpp | size_t start (8 B) + HeldInput (8 B) per tick, plus actions; hashes 16 B/s | about 3.5 MB/h in memory, ≥432 KB/h in the file | memory | grows until stopped | the file | known DESIGN-DOCS-30; u8 action count known |
| Log file | core/log.cpp | Quill rotating sink, no size limit, Debug level | per cast, hit, death, month | disk | one run's file grows without limit | – | known GAP1-THIRD-PARTY-11, GAP1-LOGGING-05 |
| Tracy client (releasedbg) | xmake.lua:266 | in-memory queue | every zone, sample and context switch | RAM | grows until a viewer connects | – | **new (F3)** |
| Save size | relationships 10/50, journals 10/50, explored bits | bounded per NPC and tile | grows with NPCs met, then stops | 1 GiB MAX_SAVE_BYTES | – | – | fine |

## Assessment

**Strengths.** Time handling here is unusually careful, and done on purpose. Every simulation and world counter is u64, or i64 with explicit ends (TRAVEL_END, CALENDAR_END, LAST_MONTH, the StaySound and LegSound bounds). Narrowings are handled deliberately throughout:
- Orbit angles and seal spins are reduced in f64 before they narrow.
- Seal stream ages are wrapped in f64, including the >2^53-lap case.
- The glow clock is split into low 32 bits plus a fraction.
- The cloud bob is reduced in turns.
- The player's walk distance is f64.
- HUD and damage-number state is cleared on a new epoch.

I found no new overflow that a real session reaches within 100 h, or a world within centuries of map time.

**Weaknesses and risks.**
1. **Absolute-tick parametrisation.** Orbits, the DemoPulse glow and the portal seal count from simulation tick 0, not from their own start. The saved simulation tick carries this across loads, so behaviour changes with campaign length. One consequence is new: a speed change (hot reload today, slows or hastes later) jumps every orbiter by Δspeed×tick (F1). The precision side of the same pattern is already known (the glow at 2^23).
2. **Limits tied to world age.** These come from unbounded power growth rather than from counters. Reach grows about 20 cost per point of power, so on large procedural maps journeys outgrow the u8 daysAway (F2), and the u32 per-leg arrive ticks (known) become more plausible.
3. **Memory that grows with session length.** The replay recording, the log file, the glyph cache (all known) and the releasedbg Tracy queue (new, F3) grow without limit. Nothing else in the main or logic loop does: the caches I checked (labels, layouts, the leg-walk cache, damage numbers) evict.
4. **Rules kept only by convention.** EnTT's 12-bit version wraps after 4,096 recycles of a slot. That is harmless only because no handle outlives a tick, and the code does not enforce that (F6).

**Overall.** Healthy for tens of hours and many in-game years. The remaining limits sit in presentation (glow precision, already known) or development tooling (Tracy, logs). The one design lesson worth taking forward: for anything whose rate can change, keep its phase in state (as Spin does) rather than computing it from the absolute tick.

**Files read in full:** src/core/types.hpp, src/core/stable_id.hpp, src/core/tick_clock.hpp, src/core/tick_clock.cpp, src/core/calendar.hpp, src/core/journey.hpp, src/core/display_delay.hpp, src/core/display_delay.cpp, src/core/replay.hpp, src/core/replay.cpp, src/core/ecs.hpp, src/core/ecs.cpp, src/core/log.cpp, src/platform/clock.cpp, src/platform/clock.hpp, src/app/view.cpp, src/app/game.cpp, src/gfx/shelf_packer.hpp, /tmp/claude-1000/-home-lotus-Development-project-n/5e986e63-9457-419a-a1f3-2b39adbcdc44/scratchpad/review-history.md

## Findings

### GAP2-LONG-SESSION-LIMITS-01: Orbit angle is computed from the absolute simulation tick, so any speed change jumps every orbiter by Δspeed × tick

- **Severity:** low · **Verdict:** confirmed · **Category:** design-weakness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:1741`

OrbitPoint (src/game/components.cpp:14-19) places an orbiter at `fmod(phase + speed * tick, 2π)`, where `tick` is the absolute simulation tick. That tick is saved and keeps counting across scenes and loads. `phase` is documented as 'the angle at tick 0'. ApplyData (host.cpp:1735-1741) overwrites `orbit->speed = kind.orbitSpeed` on a hot reload of creatures.yaml and on EnterScene, but leaves `phase` unchanged. The angle at the current tick T then moves by (newSpeed - oldSpeed) × T at once. Host::Tick then sets body.previous to the old point and body.current to the new one, so the renderer interpolates a streak across the circle. The jump grows with the campaign's tick count, not the orbit's age. Spin (host.cpp:893) has no such problem, because it keeps its angle in state and steps it.

**Evidence:** components.cpp:17-18: `std::fmod(static_cast<f64>(orbit.phase) + (static_cast<f64>(orbit.speed) * static_cast<f64>(tick)), 2 * std::numbers::pi)`; host.cpp:889: `body.current = OrbitPoint(orbit, counters.simulation);` host.cpp:1739-1741: `orbit->radius = kind.orbitRadius; orbit->wobble = kind.orbitWobble; orbit->speed = kind.orbitSpeed;`

**Impact:** With 10 h of saved play (T ≈ 2.16e6), a speed edit of 0.001 rad/tick swings every fox and every stress bullet by about 2,160 rad, a random new angle, in one tick. The visible streak and jump get worse the longer the campaign. Any future gameplay that changes an orbiter's speed (slows, hastes, phase-based bosses) inherits the same discontinuity. Positions stay deterministic: replays stop on ReloadData anyway.

**Recommendation:** Keep orbits continuous when the rate changes. Either rebase the phase in f64 when the speed changes: phase' = fmod(phase + (oldSpeed - newSpeed) × T, 2π), with T the current simulation tick. Or keep the angle in state and step it each tick with a remainder, as Spin does. The second option also fixes the known spawn-at-tick-0 jump (GAME-SCENES-01) without a separate fix.

**Verification:** Checked components.cpp:14-19 (OrbitPoint fmod(phase + speed*tick) in f64 from absolute tick) and host.cpp:1735-1741 (ApplyData overwrites speed, leaves phase). Mechanism holds; a speed change jumps the angle by Δspeed×T. Impact is dev hot-reload and scene entry only today, so low.

### GAP2-LONG-SESSION-LIMITS-02: Travel journal daysAway (u8, clamped at 255) saturates as NPC reach grows with world age on large maps

- **Severity:** low · **Verdict:** confirmed · **Category:** scaling-limit · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/travel.cpp:36`

EntryOf clamps a journey's days away to `std::numeric_limits<u8>::max()` (255 days, 45,900 ticks). How far an NPC travels is reach_base + reach_per_power × power (300 + 20 × power cost units in world.yaml). Power rises by 1 each month an NPC cultivates and never falls. A one-way leg costs up to reach × ticks_per_cost (16) ticks. Once reach exceeds about 1,440 cost (power ≈ 57, roughly 60 cultivating months, a few hours of map time), a round trip with its stay can last more than 255 days on any procedural map wider than that in cost. The shipped test map's farthest pair costs 562, so tests never reach the clamp.

**Evidence:** travel.cpp:18-19: `constexpr i64 MOST_DAYS_AWAY = std::numeric_limits<u8>::max();` travel.cpp:36: `.daysAway = static_cast<u8>(std::clamp<i64>(away, 0, MOST_DAYS_AWAY))`; world.yaml: `reach_base: 300, reach_per_power: 20`, `ticks_per_cost: 16`; npcs.cpp:99: `person.power += 1.0F;`

**Impact:** On large maps after a few hours of world time, the journals of the most powerful (most interesting) NPCs record '255 days' for every long trip. Journal data is saved, so the loss is permanent. Nothing warns.

**Recommendation:** Store daysAway as u16, or store the end day (i32) instead of a difference. Alternatively, cap reach in TravelRules so the round trip provably fits. Add a journal test with a journey longer than 255 days. Revisit this when power growth is bounded (GD-WORLD-SIM-03).

**Verification:** travel.cpp:18-19,36 clamps daysAway to u8 255. Clamp is silent. The reach and power-growth figures were not re-derived, so the time to reach it is unverified. Still a real saturation for long trips on large maps.

### GAP2-LONG-SESSION-LIMITS-03: Tracy in releasedbg is not on-demand: the client buffers every zone, sample and context switch in memory until a viewer connects

- **Severity:** low · **Verdict:** unverifiable · **Category:** resource-growth · **Status:** new · **Reviewer confidence:** medium
- **Where:** `xmake.lua:266`

The `tracy_on` requirement sets only `tracy_enable = true, only_localhost = true`. In the xmake-repo tracy package, `on_demand` defaults to false, and `sampling`, `callstack`, `context_switch` and `system_tracing` default to true. Without TRACY_ON_DEMAND, the Tracy client records from program start and keeps every event queued in memory until a server connects. A releasedbg run that no viewer or capture is attached to (`run --mode releasedbg`, or a profiler attached late) therefore grows its memory for as long as it runs. With PN_PROFILE_ZONE in per-tick and per-frame paths plus call-stack sampling, that can be hundreds of MB per hour or more.

**Evidence:** xmake.lua:266: `add_requires("tracy-libdl v0.14.1", {alias = "tracy_on", configs = {tracy_enable = true, only_localhost = true}})`; upstream package: `add_configs("on_demand", {type = "boolean", default = false, ...})`, `sampling` default true, `context_switch` default true.

**Impact:** Long releasedbg sessions (soak tests, a developer playing the profiling build for hours) can exhaust RAM. Memory measurements of such runs are then misleading. The shipping release build is unaffected.

**Recommendation:** Pass `on_demand = true` for `tracy_on`, so the client collects only while a viewer or capture is connected. Check that `profile` and `measure` still capture from the first frame they need. Alternatively, document in tools/AGENTS.md that a releasedbg build must not run unattended without a capture.

**Verification:** xmake.lua:266 sets only tracy_enable and only_localhost, as quoted. The upstream package's on_demand default could not be read here. It would need checking in xmake/repo or the xmake-repo tracy package. Only affects releasedbg builds.

### GAP2-LONG-SESSION-LIMITS-04: TickTime and TickClock::Advance overflow after about 9.75 years of continuous scheduling without a Restart

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** integer-overflow · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/tick_clock.hpp:37`

TickTime computes `originNs + index * NS_PER_SECOND / TICKS_PER_SECOND`, and Advance computes `(nowNs - m_originNs + 1) * TICKS_PER_SECOND`. Both overflow u64 once the index passes 1.8e10 ticks, or once nowNs - origin passes 3.07e17 ns. Either is about 9.75 years of logic-thread uptime without a Restart() (the month steps, saves and loads that call it) or a catch-up reset. The header promises 'no rounding error builds up however many ticks pass' but states no bound.

**Evidence:** tick_clock.hpp:37: `return originNs + (index * NS_PER_SECOND / TICKS_PER_SECOND);` tick_clock.cpp:12: `const u64 last = (((nowNs - m_originNs + 1) * TICKS_PER_SECOND) - 1) / NS_PER_SECOND;`

**Impact:** Not reachable in play: every month step restarts the clock. The only risk is a future dedicated or idle server that never steps a month, and even that needs years.

**Recommendation:** State the bound in the Doxygen, or compute in a way that cannot overflow: split the index into whole seconds and remaining ticks, or rebase the origin every 2^32 ticks.

**Verification:** The arithmetic in tick_clock.hpp and tick_clock.cpp was not re-opened, but the overflow bound is plausible (u64 ns × TICKS_PER_SECOND). The finding itself says it is unreachable because month steps call Restart. Documentation-only.

### GAP2-LONG-SESSION-LIMITS-05: Long-lived presentation clocks count from simulation tick 0 (portal seal, DemoPulse, orbits), so their state depends on campaign length, not their own age

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** design-weakness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1176`

The world scene publishes its portal seal with `.startTick = 0, .endTick = core::NEVER_ENDS`. DemoPulse publishes `{ .role = role }`, which starts at tick 0. Orbits evaluate from tick 0. Their phases and ages are therefore functions of the saved simulation tick, which accumulates over the whole campaign and also while the world is paused. Seal math is robust: it is wrapped in f64 by SealSpin and SealWrap, and SEAL_LAP_WRAP handles laps. But the pattern makes every such clock jump on a load (the tick changes) and on scene entry. Any appear curve of the portal's layers is evaluated at an age of hours, so it never plays. The glow shader's precision limit is reached because of this pattern (the DemoPulse case is known).

**Evidence:** host.cpp:1176: `{ .key = PORTAL_SEAL, .point = *portal, .size = PORTAL_SEAL_SIZE, .startTick = 0, .endTick = core::NEVER_ENDS }`; host.cpp:1746-1748: DemoPulse returns `{ .role = role }` (startTick 0); components.hpp:127: `/// The angle at tick 0, in radians.`

**Impact:** No bug at present, apart from those already known (DemoPulse precision) and F1 (orbit jump). The risk is the next effect that is narrowed to f32 or a u32 without the care seal_draw takes.

**Recommendation:** Give each long-lived presentation clock its own start: the portal seal could start at the scene's entry tick (kept by the host). Add a rule to src/AGENTS.md: a presentation time derived from the simulation tick is taken relative to the item's own start, never from 0.

**Verification:** host.cpp:1176 and DemoGlow (host.cpp:~1746) return startTick 0, as quoted. The pattern is true, but it is mostly a restatement of known items (DemoPulse, orbit jump). The portal-seal math is f64-wrapped, so there is no present bug. The proposed AGENTS.md rule is a judgement call.

### GAP2-LONG-SESSION-LIMITS-06: EnTT's 12-bit entity version wraps after 4,096 recycles of a slot; only an unenforced convention keeps handles from aliasing

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** lifetime · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/ecs.hpp:31`

core::Entity is entt::entity: 20 bits of index and 12 bits of version. Each destroy and create at the same slot bumps the version, which wraps after 4,095 recycles. The arena (fox deaths and respawns), the stress scene and each scene's Deactivate/Build cycle recycle slots continuously. A session of tens of hours will wrap versions on hot slots. That is safe today, because a handle never crosses a tick or a domain, as the comment and src/AGENTS.md say. But nothing enforces it: no debug check on a handle's tick or generation. A future cache of handles that lives across ticks (a spatial hash, an AI target, a label anchor) would see `valid()` succeed on a different entity once the version wraps. The 2^20 live-index limit is known separately.

**Evidence:** ecs.hpp:30-31: `/// An EnTT entity handle: valid only inside its domain and within one tick.` `using Entity = entt::entity;` No core::Entity data members exist today: grep over src/**/*.hpp finds only parameters and return types.

**Impact:** None today. It is a latent aliasing bug for future code, and long sessions are exactly where it would show.

**Recommendation:** Keep the rule, and back it with a debug-only guard. For example, Domain::Find could return a wrapper type that cannot be stored in a struct field, or a lint or grep could flag `core::Entity` members. Alternatively, use 64-bit entity identifiers (entt::entt_traits with a u64 entity), which also lifts the 2^20 live-entity limit.

**Verification:** EnTT's default entity has 20 index bits and 12 version bits. The comment in ecs.hpp says handles are valid within one tick. This is a latent risk with no current violation, and the finding says as much. Low confidence and no concrete failure.
