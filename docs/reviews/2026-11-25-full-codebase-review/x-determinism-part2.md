# Review: x-determinism (part 2)

### X-DETERMINISM-11: Fog and sight helpers write `m_world` while saving and hashing use `Live()`; `Explore()` and `SetSightRadius()` are test-only production API

- **Severity:** info · **Verdict:** confirmed · **Category:** consistency · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/host.cpp:1468`

`ExploreRound`, `Explore`, `SetSightRadius`, `FogFits` and `PublishJourneys` use `m_world` explicitly. `WriteClock`/`HashWorld` and `AdvanceCalendar` use `Live()`, which is the scratch world in dev scenes. Today only world-map scenes explore and they are never dev scenes, so the two agree, but the asymmetry is an invariant enforced by nothing. `Explore(Rect)` and `SetSightRadius` have no production caller (tests only) yet mutate hashed state outside the input path.

**Evidence:** host.cpp `ExploreRound`: `core::ExploreAround(m_world.explored, …)`; `StateHash` hashes `Live()`; `rg` finds only tests calling `Explore(`/`SetSightRadius(`.

**Impact:** A future dev scene with the world map, or a debug UI calling `Explore`, would change state that is either not hashed (a replay can't catch it) or not recorded as input (replays diverge).

**Recommendation:** Use `Live()` throughout, or assert `!m_scratch` where `m_world` is meant. Move test-only mutators behind a test seam, or route them through Actions so replays record them.

**Verification:** Host::Explore and SetSightRadius (host.cpp:1463-1468) have no non-test callers in src/. Explore uses m_world while hashing uses Live(). The asymmetry is real but harmless today. I did not check whether world-map scenes are ever dev scenes.

### X-DETERMINISM-12: `UnlikeOnBothWorkerCounts` never compares one worker count against another

- **Severity:** info · **Verdict:** confirmed · **Category:** test-quality · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/support/exactness.hpp:55`

The helper runs `Unlike(games, one, …)` and then `Unlike(games, four, …)`. Within each call the three games (saved, never-saved, loaded) all use the same `Jobs`, so a worker-count-dependent month step would diverge identically in all three and pass. Its name and doc ('Unlike() on 1 worker, then on 4') suggest it checks independence from the worker count; it checks save/load exactness under each count. True 1-vs-4 comparisons exist elsewhere (shipped_world_tests.cpp:414, travel_tests.cpp:1080, npcs_tests.cpp:379). The long 1-vs-4 soak (replay_tests.cpp:649) is skipped by default.

**Evidence:** exactness.hpp:55-60.

**Impact:** Readers may overestimate coverage; a regression that the dedicated tests miss (e.g. in scratch worlds) would not be caught here.

**Recommendation:** Rename it (e.g. `UnlikeUnderOneAndFourWorkers`), or make the second pass compare against hashes captured from the first.

**Verification:** exactness.hpp:55-60 matches the description. The first call runs Unlike with 1 worker. The second runs Unlike with 4 workers, 0 ticks, from a later tick. Neither call compares one worker count against the other, so the name overstates it. The finding cites other tests that do compare 1 against 4.

### X-DETERMINISM-13: RNG purposes and seeds are coordinated only by comments across modules; the gfx terrain seed is tied to the constant game seed

- **Severity:** info · **Verdict:** unverifiable · **Category:** rng-discipline · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/random_purposes.hpp:111`

Purpose values are spread out: game 1-14, core/volley 1-8 (under the volley's own seed), gfx/tile_hash 9 with `TERRAIN_SEED = 0x5eed`, names 'NAME', journey 'STAY' (under a 32-bit stay seed). Purpose 9 is 'retired' only because `TERRAIN_SEED` happens to equal Host's constant `SEED` 0x5eed. Every new game uses that same seed (host.cpp `SEED`), so when per-game seeds arrive the stamps won't follow the seed, and the reservation becomes meaningless or forgotten. Stay and journey seeds are truncated to u32, so collisions are likely across tens of thousands of journeys (birthday bound about 77k); the effect is cosmetic (identical wander patterns).

**Evidence:** random_purposes.hpp:111-112; tile_hash.hpp:14-16; host.cpp `constexpr u64 SEED = 0x5eed;`; travel.cpp:161 `static_cast<u32>(core::Random(...))`.

**Impact:** Low today. The risk is an accidental shared stream when purposes or seeds are added or seeds become per game.

**Recommendation:** Keep one table of purposes (or 32-bit tags like 'NAME'/'STAY' everywhere) in core with a static check for uniqueness. Derive the gfx terrain seed from the map or world seed explicitly. Widen stay seeds to u64 if save size allows.

**Verification:** I did not open random_purposes.hpp, so I verified nothing here. The claims about a coordinated purpose table, TERRAIN_SEED matching SEED, and u32 stay seeds are plausible. Host's SEED 0x5eed is confirmed at host.cpp:776. The impact described is cosmetic and speculative.

### X-DETERMINISM-14: A stop request mid-calendar-step leaves a half-applied month

- **Severity:** info · **Verdict:** confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/game/npcs.cpp:354`

`AdvanceCalendar` advances `months` and `worldTicks` first. On `stop`, Travel is skipped, `MonthStep` chunks not yet started are skipped by timing, and nothing is merged. The state is then neither the old month nor the new. This only happens at shutdown and nothing saves afterwards, but there is no guard against a future autosave on exit.

**Evidence:** host.cpp AdvanceCalendar increments `world.months` before `travel.Month`/`m_monthStep.Run`; npcs.cpp:354-377.

**Impact:** None today; a latent trap for autosave or exit-save features.

**Recommendation:** Document on `Simulation`/`Host` that a stopped `AdvanceCalendar` leaves the state unsavable, and assert in `Logic::Save` that no step was cut short (a flag set on early return).

**Verification:** npcs.cpp:354-377: if stop is requested, the step logs 'cut short: the game is closing' and returns without merging. The calendar counters are advanced earlier. The state is therefore a half-applied month. Only reachable at shutdown, so it is a latent trap rather than a bug.
