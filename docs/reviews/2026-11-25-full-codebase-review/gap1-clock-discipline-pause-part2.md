# Review: gap1-clock-discipline-pause (part 2)

### GAP1-CLOCK-DISCIPLINE-PAUSE-11: Replays ignore focus loss, and no clock is ready for playback at another speed

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/logic.hpp:174`

While a replay plays, live Pause and Resume are held back until it stops, by design. An unfocused window playing a replay therefore keeps animating everything, against the spirit of the pause rule, though the game is not 'paused' as such. No replay speed control exists today (one recorded tick per wall tick), so 'replay at a different speed' cannot happen yet. If fast-forward or ToI-style world-time acceleration is added, the frame-clock world animations (cloud bob, fog ease) will not scale with it, while the render-tick and world-clock ones will.

**Evidence:** logic.cpp:177-181 and 274-276 (only the replay's own Pause and Resume apply while it plays).

**Impact:** A gap to design for rather than a present bug: speed-scaled playback would make the clouds and fog move out of step with the world.

**Recommendation:** Fold this into the same presentation-clock design: one clock that holds on any pause and scales with playback or world speed, separate from the UI's frame clock.

**Verification:** logic.cpp:177-181 sets m_windowPaused always but applies m_paused only when !m_replay->Playing(), so focus loss does not pause a playing replay. Line 274-276 is the replay's own Pause/Resume. The finding itself calls this a gap to design for, and I found no replay speed control. The claim that cloud bob and fog ease read the frame clock and would not scale is unverified. It is a forward-looking design note, not a present bug. I found no replay entry in sweep-up-backlog.md that records it as deliberate.
