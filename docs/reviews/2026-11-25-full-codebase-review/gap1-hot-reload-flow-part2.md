# Review: gap1-hot-reload-flow (part 2)

### GAP1-HOT-RELOAD-FLOW-11: WatchData's composition (order, partial failure, CheckScenes refusal, what ReloadData carries) has no test

- **Severity:** low · **Verdict:** confirmed · **Category:** test coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/app/data_watch_tests.cpp:1`

The tests cover DataWatch and the pure helpers (ReloadOf, KeepWorld, KeepLook, KeepSeals, KeepAnimations). App::WatchData itself needs a window and a renderer, so nothing tests the order it applies tables in, that a CheckScenes refusal keeps the old data, that the ReloadData action carries the running map and names, that a failed effects read still lets the game data reload, or what each file edit reloads.

**Evidence:** There is no test constructing App or calling WatchData. All the reload decisions are inline in app.cpp:433-502.

**Impact:** Regressions in the cross-module reload flow (the subject of this review) would go unnoticed. The findings above all live in untested glue.

**Recommendation:** Extract the decision part into a pure function: input the folder's parsed results and the current tables; output a plan (which tables to apply, which errors to log, the GameData to send). Unit-test it per data file. Keep WatchData as a thin applier.

**Verification:** WatchData in app.cpp needs a window and a renderer, and no test constructs App. Only DataWatch and the pure helpers are tested. The refactor to a pure planning function is reasonable.

### GAP1-HOT-RELOAD-FLOW-12: --cast-swords reads costs and cooldowns from the new data against a snapshot still made under the old data

- **Severity:** info · **Verdict:** confirmed · **Category:** consistency · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/app.cpp:363`

WatchData replaces m_options.data on the main thread at once, but the logic applies ReloadData a tick or more later (longer during a scene change or a month step). Until a snapshot made under the new data arrives, CastOnTimer combines the new abilities' costs and cooldownTicks with the drawn snapshot's Qi and ready ticks.

**Evidence:** app.cpp:493 `m_options.data = *gameData;` and app.cpp:363,375 `TimedCast(caster, m_options.data.abilities, ...)`, `m_options.data.abilities.Of(generator).cooldownTicks`.

**Impact:** A profile run may send one refused or mistimed cast just after a reload. Negligible.

**Recommendation:** Read costs and cooldowns from the snapshot (CasterView already carries qiMax and readyTick), or update m_options.data only once a snapshot shows the new data (for example a data hash in Snapshot).

**Verification:** app.cpp updates m_options.data on the main thread at once, while the logic applies ReloadData later, so the window exists. It affects only the --cast-swords profile helper, and the impact is negligible, as the finding says.

### GAP1-HOT-RELOAD-FLOW-13: Every Action carries a shared_ptr<const GameData> that only debug hot reload uses

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** performance / design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/frame.hpp:520`

core::Action holds `std::shared_ptr<const GameData> data` for ReloadData alone. Every action copied through the handover, the logic's m_input, replay recording (`Record({ .actions = m_input })`) and replay playback copies a shared_ptr. The struct is therefore not trivially copyable, and each copy of a ReloadData action costs an atomic refcount operation.

**Evidence:** frame.hpp:519-520 `/// The game data to swap in (ReloadData); empty otherwise. std::shared_ptr<const GameData> data = nullptr;`

**Impact:** A small constant cost and a heavier Action type on every input path, in release builds too, for a debug-only feature.

**Recommendation:** Send the data through its own latest-value slot in GameHandover (like SendView), with ReloadData only flagging it, keeping Action trivially copyable.

**Verification:** I did not re-read frame.hpp:520. I am relying on the finding's quote of a shared_ptr<const GameData> member in Action. The refcount cost is real but tiny, because actions are few per tick. Whether Action is trivially copyable matters little here. This is a design preference, not a defect.

### GAP1-HOT-RELOAD-FLOW-14: With --travel-share the start-time map is held twice for the whole run, and the look is checked against the copy the game doesn't run

- **Severity:** info · **Verdict:** confirmed · **Category:** memory / scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/main.cpp:135`

SetTravelShare replaces options.data.world with a full copy (WithTravel). main's local `world` still holds the original for the whole run, because Run() returns only after app.Run(). LoadWorldLook and the log line use `**world` (the original), while the renderer and the game use the copy. The two differ only in travel rules today, so the check is equivalent.

**Evidence:** main.cpp:112 `const std::expected<std::shared_ptr<const core::WorldMap>, core::Error> world = ...`, 119 SetTravelShare, 135 `LoadWorldLook(dataFolder, *options.manifest, **world)`, 155-156 App runs inside the same scope.

**Impact:** Doubles the map's memory during `measure`'s world run, which matters for very large procedural maps. A latent mismatch if WithTravel ever changes more.

**Recommendation:** Validate the look against options.data.world and release the local copy (scope it, or reset it) before constructing the App.

**Verification:** main.cpp:112 keeps the original map in a local const for the whole run. SetTravelShare (src/game/travel.cpp:187-199) replaces data.world with a WithTravel copy. LoadWorldLook is given **world, the original. The two differ only in travel rules today, so behaviour is the same. The double memory is real, but only with --travel-share.
