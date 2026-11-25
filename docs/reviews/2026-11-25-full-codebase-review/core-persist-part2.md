# Review: core-persist (part 2)

### CORE-PERSIST-11: WORLD_VERSION is read after the clock fields it versions, and its mismatch isn't logged

- **Severity:** low · **Verdict:** confirmed · **Category:** versioning · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/game/host.cpp:251`

WriteGameWorld writes seed, generator, base hash, ID counter and then WriteClock (calendar, pause, map place, sight radius, explored tiles vector) before archive(WORLD_VERSION). Explored tiles and sight radius were added with a WORLD_VERSION bump (8e526d0e), so a save of another version has its clock parsed with the current layout before its version is seen. That is safe only because every read is bounds-checked. The version mismatch then fails silently, with no log line, unlike generator and base-hash refusals. WriteScratch has the same order.

**Evidence:** host.cpp:162-170 (write order), 252-280 (read order, version check without log). The sweep-up backlog item 'Saves refused without a reason in the log' covers the missing log; the field order is new.

**Impact:** A clean version refusal becomes a misaligned parse first. Players and developers get no reason for the refusal.

**Recommendation:** Move WORLD_VERSION to directly after the generator/base identity (before WriteClock), and log 'save refused: world section version X, this game's Y' as the other refusals do.

**Verification:** host.cpp:162-165 writes the clock before WORLD_VERSION, and the read at ~251-275 reads the clock first. The version mismatch (line ~298) is checked with no log line, unlike the generator and base-hash refusals. The misparse is harmless because reads are bounds-checked.

### CORE-PERSIST-12: Replay keeps stale state across mode changes: a dangling m_start span and retained buffers

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** lifetime · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/replay.cpp:120`

StartRecording replaces m_bytes (freeing a playback buffer) but leaves m_start, m_next, m_nextHash and m_matched untouched, so m_start dangles into freed memory. StopPlaying clears m_start but keeps m_bytes (the whole replay, up to 1 GiB), m_actions, m_tickStarts and m_held. FinishRecording keeps m_bytes (the start state) and every recorded action until the next StartRecording.

**Evidence:** replay.cpp:120-128 (StartRecording), 277-280 (StopPlaying), 156-189 (FinishRecording).

**Impact:** Nothing reads m_start after StartRecording today, but the class invariant is fragile. Memory from a finished replay or recording stays held for the session.

**Recommendation:** Reset all per-mode members in StartRecording, StopPlaying and FinishRecording (clear and shrink_to_fit, or swap with empty), keeping only m_hashes if HashCount() must report the last recording.

**Verification:** replay.cpp:120-128 does not reset m_start, and StopPlaying (277-280) keeps m_bytes. Both are true. I did not confirm that StartRecording can be called during playback, so the dangle is hypothetical. Retained memory is real but minor. The severity is low or info.

### CORE-PERSIST-13: Handover::ReturnState writes m_free[m_freeCount++] with no bound check

- **Severity:** low · **Verdict:** confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/handover.hpp:159`

PopFree asserts that a free buffer exists, but ReturnState (and Publish's `m_free[m_freeCount++] = PopWaiting()`) never asserts m_freeCount < BUFFERS or that the pointer belongs to m_states. A double return, or returning a pointer not taken, writes past std::array's end in release (operator[] isn't checked).

**Evidence:** handover.hpp:156-160, 75-77.

**Impact:** A future Display bug would become silent memory corruption in the cross-thread handover instead of a clear assert.

**Recommendation:** Add `PN_ASSERT(m_freeCount < BUFFERS && state >= m_states.data() && state < m_states.data() + BUFFERS, ...)` in ReturnState, plus the same count check in Publish.

**Verification:** handover.hpp:159 does `m_free[m_freeCount++] = state` with no bound or ownership assert, and Publish at ~75 has the same gap. The defensive-assert claim holds and the impact is hypothetical.

### CORE-PERSIST-14: The order section rewrites a storage's entire ID list after any membership churn

- **Severity:** low · **Verdict:** confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/delta.hpp:382`

SaveStorageOrder writes 1 byte only when now's storage equals base's exactly. Otherwise it writes a u64 count plus every entity's 8-byte StableId. One gained component (appended), or one entity materialised out of the world domain (swap-and-pop), makes that storage, often the 50k-entry Npc or StableId storage, cost 8 × N bytes raw, regardless of how little moved. Places and masks are likewise fixed u64s.

**Evidence:** delta.hpp:363-395; delta.hpp:15 'Every count and place is a u64'.

**Impact:** Raw save size and serialise time grow linearly with world size after the first churn. Zstd recovers part of it, but very large procedural maps (the stated goal) pay it on every save.

**Recommendation:** Measure on the largest planned map first. If it matters, encode the order as edits relative to base (the swap-and-pop history), or as a permutation of base places with the appended IDs. Use varints or delta-coded ascending places in the delta sections.

**Verification:** SaveStorageOrder (delta.hpp:~380) writes 1 byte only if SameOrder, else a u64 count plus 8-byte StableIds for the whole storage. Claim holds as a scaling note; no measurement recorded and zstd mitigates. Recommendation is already conditional on measuring.

### CORE-PERSIST-15: No semantic caps on loaded entity counts: memory bounded only by the 1 GiB decompressed body

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** hostile-input · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/delta.hpp:501`

LoadDomainDelta accepts up to Remaining()/16 created entities, each a Restore (EnTT entity, an unordered_map entry, components). LoadDomain does the same at Remaining()/8. ReadSaveFile allows a file of ~1 GiB, allocates it, then allocates another `stateBytes` (up to 1 GiB) and decompresses into it. The total peak can be several GiB from one corrupt or crafted save before Host's consistency checks reject it.

**Evidence:** delta.hpp:499-523; state.hpp:278-292; save_file.cpp:106-149.

**Impact:** A broken save file can push the game into swapping or an out-of-memory crash instead of a clean refusal. The threat is local only, hence low severity.

**Recommendation:** Cap world and scene entity counts at a design maximum (e.g. a constant tied to the largest map), refusing before Restore. Consider streaming decompression, or a lower MAX_SAVE_BYTES once real save sizes are known (the spec measured far below 100 MB).

**Verification:** MAX_SAVE_BYTES = 1<<30 (save_file.hpp:25). ReadSaveFile does cap the size, checks ZSTD_getFrameContentSize against the header, and bounds by ratio before allocating, so the peak is the file plus state (the file itself is bounded by compressBound, which a crafted file can nearly fill). A crafted save can reach several GiB before Host's checks. Local-only threat, so low is right. Entity-count caps are a reasonable but optional hardening.

### CORE-PERSIST-16: Replay design doc's format block is stale

- **Severity:** low · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/design/0002.2-replays.md:79`

The documented layout is 'u32 format · version string · start state · ticks: u8 action count, then each action's kind (u8) · hashes'. The code (format 5) also writes a u64 game-data hash after the version, a held-input change flag and Vec2 per tick, and per-kind fields (Select target, Cast slot + point, WalkTo point).

**Evidence:** 0002.2-replays.md:78-83 vs replay.cpp:156-189.

**Impact:** Anyone writing tooling (replay inspectors, the fuzzer's seed generator) from the design doc gets the wrong layout.

**Recommendation:** Update the block, or point to replay.cpp's ReadTicks/FinishRecording as the reference, and mark the revision as the repo does for other design-doc changes.

**Verification:** docs/design/0002.2-replays.md:78-83 shows the format block lacking the u64 data hash, held-input data, and per-kind action fields. The doc header says §3/§4 were revised, but the layout block is out of date against the code (format 5). Docs-only impact.

### CORE-PERSIST-17: Logic::Restore accepts any tick counters from a save or replay start

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** hostile-input · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/logic.cpp:333`

The wall, simulation and state counters are read and adopted without range checks. A save with wall = 2^64-1 wraps on the next Count(). Events, the render clock (f64 conversions), REPLAY_HASH_TICKS checks and the flash/seal 'starts by' checks all run on the wrapped values. The save fuzz target deliberately uses SAVED_AT = max-1, but never through Logic::Restore.

**Evidence:** logic.cpp:330-336; tests/fuzz/save.cpp:20.

**Impact:** A corrupted save can lead to odd timing behaviour (events dropped as stale, interpolation jumps) rather than a refusal. A crash is unlikely.

**Recommendation:** Refuse counters above a sane bound (e.g. < 2^62) and require simulation <= wall, as a cheap invariant.

**Verification:** logic.cpp:~330-336 reads format and counters with only the SAVE_FORMAT check and no range validation on counters; calendar is range-checked. The claim of wrapping on the next Count is plausible but I did not trace the downstream effects. Saves are checksummed, so only a deliberately crafted or hand-edited file reaches this. Low confidence in the impact; low severity.

### CORE-PERSIST-18: ReadTicks reserves up to Remaining() ticks × 16 bytes (Audit 2)

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** hostile-input · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/core/replay.cpp:85`

`if (ticks > archive.Remaining()) Fail();` then reserve(ticks) for tickStarts (8 B) and held (8 B). Each tick takes at least 2 bytes (count + held flag), so the bound is twice as loose as it needs to be, and the reservation is 16× the bytes left.

**Evidence:** replay.cpp:83-89. Recorded as Audit 2 in the review history.

**Impact:** A malformed replay can reserve 16× its decompressed size up front (up to ~16 GiB for a 1 GiB body).

**Recommendation:** Bound by `Remaining() / 2` (the minimum tick size) and reserve no more than that, or don't reserve from a claimed count at all.

**Verification:** replay.cpp:83-89 checks ticks > Remaining(), then reserves two vectors of 8 B each. A tick needs at least 2 bytes, so the bound is loose and the reservation reaches 16x the remaining bytes. The reserve is real, though it is virtual memory that is mostly untouched. This is the same defect as finding 2, only smaller. I did not verify the 'Audit 2' known-deferred claim.

### CORE-PERSIST-19: Hasher: UB shift for scalars wider than 8 bytes, and value/size mixing that can collide

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/archive.hpp:106`

Hasher::Put for a Scalar builds `bits |= u64{raw.at(i)} << (8*i)` for every byte. A long double (16 bytes on x86-64 Linux) or __int128 (both satisfy std::is_arithmetic in GNU mode) shifts by 64 or more, which is undefined behaviour. The size is mixed in by addition (`Mix(bits + sizeof(T))`), so u64{4} and u32{8} contribute identically. Also Mix(0) == 0, so a first value whose bits + size wrap to 0 (u64 0xFFFF...F8) leaves the hash at 0, like hashing nothing. The schema fixes the types in each position, so this does not bite today, but the Hasher is part of both the replay format and the save checksum.

**Evidence:** archive.hpp:35-36 (Scalar concept), 106-114.

**Impact:** A latent UB trap if anyone hashes a wider scalar, plus weaker-than-documented guarantees ('two states hash alike when every value, in order, has the same bits').

**Recommendation:** `static_assert(sizeof(T) <= sizeof(u64))` in Hasher::Put and Writer/Reader, or restrict Scalar to exclude long double and 128-bit types. Mix the size separately (`Mix(m_hash ^ Mix(bits) ^ size * GOLDEN)`) at the next format bump, and start m_hash from a non-zero constant.

**Verification:** archive.hpp:35-36 (Scalar uses is_arithmetic) and 106-114 (the shift loop) match, so long double or __int128 would shift by 64 or more, which is UB. The Mix(bits+sizeof) collision is also real. No such type is archived today, and a static_assert would be a cheap guard. It is latent, so the severity is info.

### CORE-PERSIST-20: Reader::GetSequence assumes every non-scalar element takes at least one byte

- **Severity:** info · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/archive.hpp:216`

LEAST_BYTES = 1 for non-scalar elements, and a size above Remaining()/1 is refused. An element type whose Fields() archives nothing (an empty tag struct, or a struct whose only fields are empty) writes 0 bytes. A legitimately saved vector of more such elements than bytes left would then be refused on load, so a save/load round trip fails.

**Evidence:** archive.hpp:208-233.

**Impact:** Latent: no saved vector holds such a type today.

**Recommendation:** Statically require that archived elements write at least one byte (e.g. static_assert(!std::is_empty_v<Element>)), or document the rule in the file comment.

**Verification:** archive.hpp:~211-216 sets LEAST_BYTES to 1 for non-scalars, so a zero-byte element type would be refused on load. This is latent and a theoretical edge case.
