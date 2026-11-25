# Review: gap1-sim-boundary-completeness (part 2)

### GAP1-SIM-BOUNDARY-COMPLETENESS-11: names.yaml is part of GameData::Hash and BaseHash though the simulation never reads names

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** hash scope · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/game_data.hpp:97`

NameOf(names, seed, id) is called only in Host::PublishJourneys, which is presentation. GenerateWorld, ticks and month steps never read GameData::names. BaseHash ('what a world generated from a seed is made of and named by') and Hash both mix in names->Hash(), so editing names.yaml refuses every save and every replay. That looks deliberate, to keep NPC names stable across a load, and is consistent with the docs. It is worth noting because the data-driven sweep and localisation plans would make names a moddable or translated table.

**Evidence:** game_data.hpp:89-102 Hash() and BaseHash() mix `names ? names->Hash() : 0`; NameOf is used only at host.cpp:1338 inside PublishJourneys.

**Impact:** Saves and replays are refused for a change that affects no simulation result. Once names become per-locale or moddable, switching locale or adding a name mod would orphan every save.

**Recommendation:** Keep this if 'a loaded NPC keeps its name' is the intent, but record the reasoning. When localisation lands, consider a separate 'presentation identity' that a load can warn about rather than refuse.

**Verification:** game_data.hpp:90-99 does mix names->Hash() into Hash() and BaseHash(). The doc comment says BaseHash is what a world is 'made of and named by', and design 0002 line 293 names the names as part of the base world's identity. So it is deliberate. The claim that the simulation never reads names was not checked beyond the finding's own evidence. The localisation concern is speculative, not a defect.
