# Review: x-cpp-safety (part 2)

### X-CPP-SAFETY-11: The archive Hasher would hit UB for scalars wider than 8 bytes; nothing stops one being archived

- **Severity:** info · **Verdict:** confirmed · **Category:** undefined-behaviour · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/archive.hpp:109`

`Hasher::Put` packs a scalar's bytes into a u64 with `u64{raw.at(i)} << (8 * i)`. `Scalar<T>` accepts any arithmetic type, including `long double` (16 bytes on x86-64 Linux) and `__int128`, where shifts of 64 or more are UB. The Writer and Reader would also silently write a format that differs by platform: long double is 8 bytes on MSVC and 16 on GCC.

**Evidence:** archive.hpp:34-35 `concept Scalar = std::is_arithmetic_v<T> || std::is_enum_v<T>;`, 105-112.

**Impact:** None today. A future field of that type would hash with UB and differ between platforms.

**Recommendation:** Add `static_assert(sizeof(T) <= sizeof(u64))` in the Scalar branches, or restrict Scalar to the sized aliases.

**Verification:** Put shifts u64{raw.at(i)} by 8*i for sizeof(T) bytes, with Scalar = is_arithmetic||is_enum. A 16-byte type would shift by 64 or more, which is UB. Nothing static_asserts the size. No such field exists today, so it is latent only.

### X-CPP-SAFETY-12: Replay header narrows the game version's length to u8 without a check

- **Severity:** info · **Verdict:** confirmed · **Category:** narrowing · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/replay.cpp:160`

`archive(REPLAY_FORMAT, static_cast<u8>(VERSION.size()))` followed by every character. A version string longer than 255 characters (a long pre-release or build-metadata suffix from set_version) writes a length that doesn't match the characters written. Every replay then fails to parse.

**Evidence:** replay.cpp:151-154; StartPlaying reads `versionLength` characters (198-201).

**Impact:** Low: replays break on unusually long version strings.

**Recommendation:** `static_assert(VERSION.size() <= 255)`.

**Verification:** Line is 160, not 151. static_cast<u8>(VERSION.size()) is unchecked and VERSION is a constexpr string_view from PN_GAME_VERSION, so a static_assert is trivially possible. Latent only.

### X-CPP-SAFETY-13: Handover indexes its fixed arrays unchecked in release

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** bounds · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/handover.hpp:159`

`ReturnState` does `m_free[m_freeCount++] = state;` with no check, and PopFree's only guard is PN_ASSERT. A main-thread bug that returns a snapshot twice, or holds three, writes past `m_free` in release, because std::array hardening is debug-only.

**Evidence:** handover.hpp:192-196, 241-244.

**Impact:** Memory corruption in a shared cross-thread structure, if a future change to Display breaks the hold-at-most-two rule.

**Recommendation:** Use `.at()`, or covered by the always-on verify suggested in the PN_ASSERT finding; these are not hot paths (a few calls per frame).

**Verification:** Cited line 192 is off; ReturnState is at 156-160. m_free[m_freeCount++] is unchecked there, and PopFree only has PN_ASSERT (186). BUFFERS=5 with 1 initially in use and a documented hold-at-most-two rule. Overflow needs a main-thread logic bug. Hardening is optional and cheap.

### X-CPP-SAFETY-14: NOLINT and pragma audit: every suppression in src/ is narrow and justified

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** lint-suppressions · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/manifest.hpp:38`

I reviewed every suppression in src/:
- NOLINTBEGIN(macro-usage) in assert.hpp, log.hpp, profile.hpp and thread_safety.hpp: stringising, source location, compiler-specific attributes; a macro is required.
- NOLINT(readability-identifier-naming) on `lock`/`unlock`: required by std::scoped_lock.
- NOLINT(reinterpret-cast) in save_file.cpp: char aliasing, the standard exception.
- NOLINT(performance-enum-size) on StringId and SceneId: a 16-bit range is needed and documented.
- NOLINT(modernize-use-std-numbers) on LN2_HI: a split constant, not ln 2.
- 21 NOLINT(readability-redundant-member-init) for designated-initialiser members: a documented Clang clash.
- clang `-Wunsafe-buffer-usage-in-container` pushes in main.cpp and gpu.hpp: confined to the two pointer→span edges.
- MSVC 4324 and 4868 pragmas around alignas and the EnTT/Quill includes.

No nested .clang-tidy in src/; tests/.clang-tidy only relaxes magic numbers, with a reason. One nit: manifest.hpp:38's NOLINT sits on the `};` line rather than the `enum class SceneId` line; it works today but is fragile if formatting moves the diagnostic.

**Evidence:** grep of NOLINT, #pragma and diagnostic over src/.

**Impact:** None. Suppression hygiene is a strength.

**Recommendation:** Optionally move manifest.hpp's NOLINT onto the declaration line to match strings.hpp.

**Verification:** Audit finding, not a defect. Spot-checked: manifest.hpp:38 NOLINT sits on the closing `};` of the empty-bodied enum, as claimed, and the redundant-member-init NOLINTNEXTLINEs carry reasons. I did not re-grep every suppression. The one nit is valid and trivial.
