# Review: data-yaml-core

**Scope:** src/data/yaml.cpp, internal/yaml.hpp, settings.*, manifest.*, strings.*, names.*, animations.*, game_data.*, tests/fuzz/yaml.cpp. Focus additionally on malformed input, error messages, limits. Also read the matching tests under tests/unit/ (same module/file names) and judge whether they would catch the defects you find.

**Findings:** 25 standing (0 critical, 0 high, 1 medium, 19 low, 5 info), 0 raised but refuted on verification.

**Parts:** data-yaml-core.md, data-yaml-core-part2.md, data-yaml-core-part3.md

## Assessment

## data-yaml-core: assessment

**Strengths**
- The readers are consistent in shape. Each has `Parse*(text)` and `Load*(path)`, returns `std::expected<..., core::Error>`, and wraps the yaml-cpp calls in `catch (YAML::Exception&)`.
- Most refusals name a line and the offending field.
- The numeric helpers (`WholeNumber`, `TunedNumber`, `TunedTicks`) check `isfinite`, ranges and integrality before casting, and keep integers below 2^53. They avoid UB from out-of-range float-to-int casts.
- yaml-cpp 0.9.0 has a 500-deep `DepthGuard`, so deep nesting throws a `YAML::Exception` and does not overflow the stack.
- `strings.cpp` deliberately walks the map. It spots duplicate keys, which yaml-cpp keeps and a lookup would hide. It also caps the table at 65535 so `StringId` (u16) can't overflow.
- The fuzz harness feeds every reader the same bytes. It asserts that a refusal always carries a message, and it seeds from the shipped tables. That is a good safety net for "never a crash".
- The tests are mostly small and exact. They pin messages and line numbers.

**Weaknesses**
- Three or four error-handling styles coexist:
  - `internal::TunedNumber` and friends name the field.
  - `game_data.cpp::Number`, `manifest.cpp::Field` and `Numbers`, and `settings.cpp::ReadIfPresent` call `node.as<T>()` directly. The user then sees `line N: bad conversion`, with no field name.
  - Several files re-implement `RefusalAt` as a private helper: `FailAt` in strings, `Fail` in game_data, `Refuse` in manifest, `RefuseNames` in names.
  - Duplicate-key handling exists only in strings and animations.
- `internal/yaml.hpp`'s helpers are tested only indirectly. `ReadText`, `WriteText`, `ReadNamed`, `DescribeRange` and `NoteUnknownKeys` have no direct test, and `ReadText`/`WriteText` have no failure test.
- `SaveSettings` omits `edge_pan`, and the round-trip test omits it too.
- The manifest's page geometry (`page_size`, `pages`) is never validated.
- Orbit parameters and `walk_speed` are unranged (known audit 6).

**Risks**
- Silent dropping of user-set `edge_pan` the day any code calls `SaveSettings`. Nothing outside `LoadSettings` calls it today.
- Windows UTF-8 and code-page path handling (`path.string()`). This matters for a game with a zh locale.
- Hand-edited data files with duplicate keys, hex/octal integers, or `null`-looking scalars behave surprisingly.

**Overall health:** good. There is no memory-safety or UB defect in this scope. The findings are consistency, robustness-at-the-edges and test-coverage gaps.

**Files read in full:** (not reported)

## Findings

### DATA-YAML-CORE-01: SaveSettings never writes edge_pan, so a save would reset it

- **Severity:** medium · **Verdict:** confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/data/settings.cpp:164`

ParseSettings reads `edge_pan` and accepts it as a known key. SaveSettings' format string writes window, language, combined_damage_numbers, damage_number_motion, text_outline and text_shadow, but not edge_pan. The first-run defaults file therefore doesn't show the player the option either.

**Impact:** Today only LoadSettings' first-run write calls SaveSettings, so the effect is that the generated file hides the option. When a settings UI or any code saves a loaded Settings, a user-set `edge_pan: false` is dropped from the file and reverts to true on the next start. settings.hpp says SaveSettings writes YAML that ParseSettings reads back, which is false for this field.

**Recommendation:** Write `edge_pan` with a comment. Add `.edgePan = false` to the round-trip test, and add a test that every Settings field survives a save and parse. Also update the header doc of ParseSettings, which doesn't mention edge_pan.

**Verification:** ParseSettings reads edge_pan at line 137 and lists it as a known key at line 119. The SaveSettings format at lines 164-185 has no edge_pan and no argument for it. A save of a loaded Settings would drop edge_pan:false. The first-run file also hides the option. Today only the first-run write calls SaveSettings, so the impact is latent.

### DATA-YAML-CORE-02: The settings round-trip test doesn't cover edgePan, so the omission above is invisible

- **Severity:** low · **Verdict:** confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/data/settings_tests.cpp:183`

The "saved settings read back the same" test builds a Settings without edgePan and compares eight fields. The "SaveSettings writes hold no unknown key" test only checks that the output parses without warnings. Neither detects a field that is never written.

**Impact:** A new Settings field can be added to ParseSettings but forgotten in SaveSettings with no test failing. That is exactly what happened with edge_pan.

**Recommendation:** Set every field to a non-default value in the round-trip test, and compare all fields. Consider a structural check such as a compile-time field count.

**Verification:** The round-trip SETTINGS initialiser at lines 186-193 omits edgePan. The test checks only the fields it sets, so the missing write goes unnoticed.

### DATA-YAML-CORE-03: Path::string() in messages and logs can throw on Windows for non-ASCII user folders

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** portability · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/settings.cpp:196`

LoadSettings, ReadText and WriteText format `path.string()` into messages and logs (settings.cpp lines 196, 198, 200 and 210-211; yaml.cpp lines 10, 14 and 24). platform::FromUtf8 deliberately builds the path from UTF-8 so it can hold any character. On MSVC `path::string()` converts through the ANSI code page and can throw std::system_error for characters not representable there.

**Impact:** A Windows player whose user folder has CJK characters (the game ships zh) on a non-CJK code page crashes or aborts startup, or logs garbage. This happens at the unconditional `PN_LOG_INFO("settings from {}", path.string())`, which main.cpp calls inside Run's try block. Startup fails before any message is shown.

**Recommendation:** Format paths with `path.u8string()` (converted to char) or a helper that never throws. Add a test with a non-ASCII temporary folder.

**Verification:** The path.string() calls exist at settings.cpp lines 196, 198, 200 and 210-211, and at yaml.cpp lines 10, 14 and 24. I did not run it on Windows. My recollection is that MSVC's path::string() converts with WideCharToMultiByte flags 0, so unrepresentable characters become '?'. That would make log and message text lossy, not a throw or a crash. The crash claim is doubtful. u8string would still be more correct.

### DATA-YAML-CORE-04: Several numbers are read with a bare as<T>(), giving a field-less 'bad conversion' message

- **Severity:** low (reviewer said medium) · **Verdict:** confirmed · **Category:** error-messages · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/data/game_data.cpp:24`

`Number()` calls `node.as<f32>()` without a try, as do `spriteNode.as<std::string>()` (line 50) and `around.as<std::string>()` (line 84). Manifest `Field<T>`/`Numbers` (manifest.cpp lines 28 and 38) and settings `ReadIfPresent` do the same. A wrong-typed value reaches the outer catch and becomes `creatures line N: bad conversion` through ErrorOf. WholeNumber and TunedNumber catch it and name the field.

**Impact:** A hand-edited file with `walk_speed: fast` or `width: abc` gets a refusal that names no field. The tests pin this poor text, for example `manifest line 6: bad conversion` and `creatures` only checks `line 8`. A sequence or map where text is expected also gets an opaque message.

**Recommendation:** Route every number through the internal helpers (TunedNumber/RequiredNumber with a Range), add a text-reading helper, and make the tests assert the field name.

**Verification:** Number() calls node.as<f32>() at line 24 with no try. spriteNode.as<std::string>() (line 50) and around.as<std::string>() (line 84) are also bare. The outer catch turns a failure into 'creatures line N: bad conversion' with no field name. The finding's own examples show tests pin this text. Impact is only poor error text for hand-edited files, so I rate it low, not medium. I did not re-check the manifest.cpp line numbers.

### DATA-YAML-CORE-05: Orbit radius, wobble and speed are unranged, and orbit shape isn't validated

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** input-validation · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `src/data/game_data.cpp:80`

`orbit` is read with `Number()`, which only checks finiteness. A negative radius or wobble, or a huge speed or radius up to the f32 maximum, is accepted. `orbit: 5` (a scalar) throws BadSubscript from `orbit["around"]`, giving an opaque error. A null `orbit:` is truthy and reports 'has no radius' at a mark that may be line 0. Related to known audit 6, which covers walk_speed only.

**Impact:** A bad creatures.yaml can place the fox at NaN-adjacent or enormous coordinates, or give a cryptic refusal. Orbit values feed the simulation, which is hashed.

**Recommendation:** Use RequiredNumber with explicit ranges for radius, wobble and speed. Check `orbit.IsMap()` and give a refusal that names the field. Add tests for the scalar and null orbit.

**Verification:** Orbit radius, wobble and speed go through Number(), which checks only finiteness (lines 22-30). There is no range check. A scalar orbit would make orbit["around"] throw BadSubscript, which the outer catch turns into an opaque error. I did not find 'known audit 6' in sweep-up-backlog.md, so the known-deferred status is unverified.

### DATA-YAML-CORE-06: yaml-cpp's integer reader accepts octal and hex, so `width: 0720` is read as 464

- **Severity:** low · **Verdict:** unverifiable · **Category:** input-validation · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/settings.cpp:17`

yaml-cpp's integer convert (convert.h:188) calls `stream.unsetf(std::ios::dec)`. The integer paths (`as<i32>` in settings and `as<u16>`/`as<u32>` in the manifest) therefore parse `0x500` as 1280 and `010` as 8. A zero-padded `0800` fails instead of being read as 800.

**Impact:** A player editing the window size gets a surprising value or an unrelated 'bad conversion'. The f64 paths (WholeNumber) behave differently, so the same text means different things in different keys.

**Recommendation:** Read integers through one helper that parses as decimal, such as std::from_chars on the Scalar, and refuse a leading 0 or 0x explicitly or consistently. Add tests for `0x10` and `010`.

**Verification:** The claim depends on yaml-cpp's convert.h behaviour (unsetf(dec)). I did not read the vendored source or run it. If true, hex or octal text in an integer key gives a surprising value. The impact is minor, since only the player edits these files.

### DATA-YAML-CORE-07: A bad window value discards the whole settings file, unlike the warn-and-default policy for other settings

- **Severity:** low · **Verdict:** confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/settings.cpp:138`

A text, motion, language or boolean setting of the wrong value keeps its default with a warning. A wrong-typed `window.width` (or `window: 5`, which makes `window["width"]` throw BadSubscript) or an out-of-range size returns an error, and LoadSettings then drops every setting and uses the defaults. The validation also runs only when the `window` key exists.

**Impact:** One typo in the window size silently resets language, text options and edge pan to defaults, and the game starts in English. For a game whose top priority is accessibility, losing the language or text options because of an unrelated typo is a real regression for the player. The tests pin this behaviour.

**Recommendation:** Treat window values like the other settings: warn and keep the default for that value. Keep refusal only for a non-map root or unparsable YAML.

**Verification:** An out-of-range window size returns std::unexpected (lines 143-153). LoadSettings then logs a warning and returns default-constructed settings, so language and text options reset too. Other settings only warn and keep their defaults. A wrong-typed window value throws through ReadIfPresent, which the catch at line 157 turns into the same refusal. The log says the file is left as it is, which is some mitigation. The accessibility argument is reasonable. I found no record of this as a deliberate decision.

### DATA-YAML-CORE-08: WriteText overwrites in place, so a crash mid-save leaves a truncated settings file

- **Severity:** low · **Verdict:** confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/data/yaml.cpp:19`

WriteText opens with `trunc` and writes directly. It never writes to a temporary file and renames, and it doesn't check that `path` is a regular file.

**Impact:** A crash, power loss or full disk during a save leaves a partial or empty settings.yaml. A partial file with a bad window value then falls back to all defaults (see above), losing the player's accessibility settings.

**Recommendation:** Write `path.tmp` and `std::filesystem::rename` over it (the save file code probably already has such a helper). Test the failure path (unwritable folder).

**Verification:** WriteText opens with trunc and writes in place (yaml.cpp 19-27). src/core/save_file.cpp:90 already does temp file plus std::filesystem::rename, so the helper exists and the settings write (settings.cpp:165) skips it. The backlog's rejected item (10) covers only the atlas and shader outputs, not settings. The impact is real but needs a crash during a tiny file write.

### DATA-YAML-CORE-09: NoteUnknownKeys and KeyLine throw on non-scalar keys, and NoteUnknownKeys is exposed to alias amplification

- **Severity:** low · **Verdict:** deliberate · **Category:** robustness · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `src/data/yaml.cpp:244`

`entry.first.as<std::string>()` throws TypedBadConversion for a key that is a sequence or map (`? [a, b] : c`). That escapes NoteUnknownKeys, so an otherwise valid or merely warning-worthy file is refused with 'bad conversion'. settings.cpp KeyLine (line 26) and strings.cpp (line 37) do the same. NoteUnknownKeys also warns on every aliased copy of a map, so a small file with aliases can produce a huge number of warnings (known item).

**Impact:** A confusing refusal on a harmless oddity, and with aliases a very large warnings vector and log spam. The refusal comes with no key or line context.

**Recommendation:** Use `IsScalar() ? Scalar() : "<non-text key>"` as animations.cpp's OnlyKeys does, and cap the number of warnings per file (for example 50 plus 'and N more').

**Verification:** The alias amplification part is recorded in sweep-up-backlog.md line 34 as a known deferral (dedupe or cap the warnings). The throw on a non-scalar key is real: yaml.cpp:244 and settings.cpp:26 use entry.first.as<std::string>() unguarded, and strings.cpp:37 does the same. The outcome is a refusal of a malformed file without line context, which is acceptable. Real but cosmetic.

### DATA-YAML-CORE-10: RefusalAt / Mark().line + 1 reports 'line 0' for null or programmatic nodes, and an alias reports its anchor's line

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** error-messages · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/data/yaml.cpp:177`

RefusalAt, Refuse (manifest), FailAt (strings), Fail (game_data) and RefuseNames all compute `node.Mark().line + 1`. For a Null node from an empty document, `Mark()` is the null mark (line -1), so the text is 'line 0'. For an alias the mark is where the anchor is defined, not where it is used.

**Impact:** Minor: misleading line numbers such as 'strings line 0: not a map of strings' for an empty strings.yaml, or an error pointing at the anchor.

**Recommendation:** Print only 'line N' when the mark is not null; for an empty file say 'the file is empty'. Add a test.

**Verification:** RefusalAt computes node.Mark().line + 1 with no null-mark guard (yaml.cpp 177-179), whereas ErrorOf at yaml.cpp:27-32 does guard with mark.is_null(). So a null or undefined node does give a wrong line number, such as 'line 0'. I did not run it to see what an empty document produces. The alias-anchor claim is plausible yaml-cpp behaviour but unchecked. Cosmetic.
