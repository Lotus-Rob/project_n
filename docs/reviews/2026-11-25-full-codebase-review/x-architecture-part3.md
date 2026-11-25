# Review: x-architecture (part 3)

### X-ARCHITECTURE-21: Command-line parsing is duplicated across main.cpp, app.cpp and core/command_line

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** duplication · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/app.cpp:125`

ZoomOf, WorkersOf, TravelShareOf and MonthStepAtOf in app.cpp, and QuitAfterNs in main.cpp, each repeat the same find-flag, take-next and from_chars pattern. core/command_line.hpp holds only SecondsToNs, and HasFlag lives in app.

**Evidence:** app.cpp 125-189, 234-236; main.cpp 39-46; core/command_line.hpp 17.

**Impact:** Each new flag copies the boilerplate. The parsing is split over three modules, and a core header exists only for main.

**Recommendation:** Add one `FlagValue<T>(args, name, range)` helper (in app, or a small cli unit), and move SecondsToNs beside it.

**Verification:** HasFlag, ZoomOf and WorkersOf are in app, and QuitAfterNs is in main.cpp (line 39), so the parsing is split as described. I did not count the repeated from_chars pattern. This is cosmetic duplication.

### X-ARCHITECTURE-22: The world map is an immutable object shared by both threads, so the world can't change at run time

- **Severity:** info · **Verdict:** unverifiable · **Category:** design-risk · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/game_data.hpp:69`

The simulation (Paths, journeys, exploration), the publisher (RoadCurveIndex) and the main thread (renderer ground and terrain, FogFade, TargetCursor) all read the same shared_ptr<const WorldMap>. Its safety relies on the map never changing.

**Evidence:** game_data.hpp 66-72; renderer.hpp SetWorld; app.cpp 683-743 (cursor reads m_options.data.world).

**Impact:** Game-like world changes (sect territories, a destroyed or founded town, new roads, seasonal rivers) can't be expressed without breaking this sharing, and the renderer's caches are keyed on 'SetWorld called'.

**Recommendation:** When designing procedural maps (finding 3), split the immutable terrain from the mutable world-map state (ownership, places' status). Keep the mutable part in WorldState, saved and hashed, and publish it as versioned views.

**Verification:** This is a forward-looking design risk about a shared immutable WorldMap. It cannot be decided by reading code. The memory note 'world map is procedural' covers the map side, and the mutable-state split is a sound idea to settle in that design.

### X-ARCHITECTURE-23: app.hpp and game.hpp include game/host.hpp, so app compiles against the whole game module's internals

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** build-coupling · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/app/app.hpp:34`

app.hpp includes game/host.hpp only for game::ARENA_SCENE and game::BuiltInData in Options' defaults. That pulls in combat.hpp, travel.hpp, npcs.hpp, world_state.hpp, leg_walk_cache.hpp and the rest into every app translation unit, and tests/unit/app does the same.

**Evidence:** app.hpp 34, 68-70; game.hpp 16.

**Impact:** Any change to Host's private members recompiles all of app. The main thread's headers also expose simulation internals that it must not touch (only Game on the logic thread may).

**Recommendation:** Have app.hpp include game/scenes.hpp (for the scene key) and take the default data from elsewhere. Keep host.hpp to game.cpp/game.hpp, or use a forward-declared factory.

**Verification:** app/app.hpp line 34 includes game/host.hpp, and lines 68-70 use game::ARENA_SCENE (in game/scenes.hpp) and game::BuiltInData (declared in host.hpp, not scenes.hpp). app/game.hpp lines 29-31 and 85 hold game::Host as a member, so app::Game legitimately needs host.hpp. The recommendation that app.hpp include scenes.hpp only is incomplete, because BuiltInData would also have to move. The build-coupling point holds.
