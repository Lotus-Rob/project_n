# Review: assets-data (part 2)

### ASSETS-DATA-11: world_look kinds carry a hard-coded 16-kind cap and per-kind sprite sets tied to the 11 terrain kinds

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** low
- **Where:** `assets/data/world_look.yaml:30`

`kinds:` must cover every terrain kind of world.yaml, at most 16, and uses fixed stamp_<kind>_0..3 sprites (four variants each). The painting and marker tables are keyed by the six danger sorts. A new terrain kind or danger sort needs new sprites, an atlas run and an edit here, and the sorts are named in two places (markers and places.dangers).

**Impact:** Procedural worlds with biome variety hit the 16 cap and the four-variant repetition quickly. Content growth is bounded by the sprite list.

**Recommendation:** Plan an extension path (more kinds, shared fallback stamps) before world generation lands.

**Verification:** world_look.yaml does key kinds with per-kind stamp sprite sets of 4 variants, and the danger sorts are named in markers and places. I did not find the 16 cap in the loader (my grep of src/data came up empty), so that detail is unverified. Because the map is procedural and its terrain kinds will change, the extension path is a future design matter. Speculative, not a defect.

### ASSETS-DATA-12: Presentation defaults and look values duplicated between glows

- **Severity:** info · **Verdict:** confirmed · **Category:** duplication · **Status:** new · **Reviewer confidence:** low
- **Where:** `assets/data/effects.yaml:41`

sword_aura and bolt_aura are byte-identical glow definitions, and fire_bolt and flying_swords share identical ability look and motion blocks (abilities.yaml motion for fire_bolt and flying_swords is also identical).

**Impact:** Retuning one requires editing two, which will drift. The identical blocks are probably intentional starting points.

**Recommendation:** Allow an `inherit` or default section, or remove one glow and point both roles at it.

**Verification:** effects.yaml lines 41-44: sword_aura and bolt_aura are byte-identical, and glow_roles at line 56 maps flying_swords and fire_bolt to them separately. This looks like deliberate separate roles that can be tuned apart. I did not check the abilities.yaml motion blocks. Cosmetic duplication.

### ASSETS-DATA-13: tablet_caption has the same Chinese text in English and Chinese

- **Severity:** info · **Verdict:** refuted · **Category:** i18n · **Status:** new · **Reviewer confidence:** low
- **Where:** `assets/data/strings.yaml:6`

tablet_caption en is 问道, identical to zh (commented 'the caption is the same in both'). en slot_empty is an em dash and the zh one is the same.

**Impact:** With language: en the caption needs CJK glyphs, so the English font must cover them. Fine if the font does; it hides a missing translation from a completeness check.

**Recommendation:** Add a marker, or an optional `same_in_all` form, so a translator and a completeness test can tell deliberate from forgotten.

**Verification:** strings.yaml:7 already carries the marker 'the caption is the same in both' as a comment, so it is deliberately marked. The en font needing CJK glyphs is a fact of a game whose names are Chinese. A completeness check is only a possible future feature, not a defect.
