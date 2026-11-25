# Review: core-sim (part 3)

### CORE-SIM-21: The logic state in a replay's start comes before the save-format tag

- **Severity:** info · **Verdict:** confirmed · **Category:** format · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/logic.cpp:313`

Serialise(true) writes `m_paused, m_calendar` and then `SAVE_FORMAT`, so a replay's start state is not a save with an extra header; its unversioned bytes come before the format check.

**Evidence:** logic.cpp:310-319 and 321-336.

**Impact:** If the logic state's layout changes, an old replay start would be misread before SAVE_FORMAT could refuse it. The replay header's own VERSION catches it today, so the risk is small.

**Recommendation:** Write SAVE_FORMAT first and the logic state after the counters, or bump REPLAY_FORMAT/VERSION whenever the logic state changes and note that next to Serialise.

**Verification:** Serialise(true) writes m_paused and m_calendar before SAVE_FORMAT (logic.cpp:310-319), and Restore reads them in the same order, with only a calendar range check before the format check. The finding concedes the replay header's VERSION guards against old data today. It is a hygiene point with no live defect.
