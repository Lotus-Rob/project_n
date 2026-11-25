# Review: gap1-third-party-library-usage (part 3)

### GAP1-THIRD-PARTY-LIBRARY-USAGE-21: Lock waits and allocations are invisible in Tracy captures

- **Severity:** info · **Verdict:** confirmed · **Category:** observability · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/thread_safety.hpp:36`

`core::Mutex`/`core::SharedMutex` wrap std mutexes. They guard the handover between the logic and main threads, the LegWalkCache under the parallel publish, and text layout. None of them use Tracy's `LockableBase`/`SharedLockableBase` in releasedbg, and there is no `TracyAlloc`/`TracyFree` or plot (entity counts, queue sizes, journeys). Contention shows only as unexplained gaps in zones.

**Evidence:** thread_safety.hpp:36-80; rg finds no TracyLockable/TracyPlot/TracyAlloc in src.

**Impact:** `measure`'s hitch analysis cannot tell lock waits (e.g. the logic thread waiting on the handover mutex, or the LegWalkCache exclusive lock during the publish) from work.

**Recommendation:** In releasedbg, have core::Mutex/SharedMutex wrap `tracy::Lockable<std::mutex>` / `SharedLockable` through profile.hpp. Add a `PN_PROFILE_PLOT(name, value)` macro for a handful of counters (entities per domain, journeys, draw-list size).

**Verification:** grep of src finds no TracyLockable, TracyPlot, TracyAlloc or PN_PROFILE_PLOT, so lock waits, plots and allocations are not instrumented. This is an enhancement suggestion, not a contract misuse.
