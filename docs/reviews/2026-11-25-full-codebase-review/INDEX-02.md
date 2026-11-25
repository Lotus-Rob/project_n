# Finding index 2/11 (standing findings, most severe first)

The ID prefix is the unit file: e.g. CORE-WORLD-03 is in core-world.md (or its -partN files).

| ID | Severity | Verdict | Status | Where | Finding |
|---|---|---|---|---|---|
| AGENT-DOCS-04 | low | partially-confirmed | known-deferred | `src/AGENTS.md:4` | src/AGENTS.md says 'phases 1 to 5 of §16 implemented', which no longer matches the work |
| AGENT-DOCS-05 | low | confirmed | new | `.claude/skills/look-at-the-game/SKILL.md` | look-at-the-game fetches an unlocked tool at runtime, against the pinned-environment rule |
| AGENT-DOCS-06 | low | partially-confirmed | new | `.claude/agents/log-triage.md:12` | The log-triage agent's AGENTS.md Gotchas grep skips .github, and its read-only rule is not enforced |
| AGENT-DOCS-07 | low | partially-confirmed | new | `AGENTS.md:57` | Context-budget process rests on a user-local hook and on memory outside the repo |
| AGENT-DOCS-08 | low | partially-confirmed | new | `.claude/settings.json:22` | The committed settings.json hardcodes one user's plugin set and does not pin the plugin |
| AGENT-DOCS-09 | low | partially-confirmed | new | `.claude/marketplace/plugins/clangd/.lsp.json:5` | The clangd plugin covers only .cpp and .hpp and assumes a Pixi env in every checkout |
| APP-INPUT-UI-02 | low | partially-confirmed | new | `src/app/bindings.cpp:250` | Gamepad unplug leaves the stick (and held pad buttons) latched: character walks or pans forever |
| APP-INPUT-UI-03 | low | partially-confirmed | new | `src/app/bindings.hpp:127` | No reset path for held inputs on focus loss or pause |
| APP-INPUT-UI-04 | low | confirmed | new | `src/app/fog_fade.cpp:15` | FogFade recomputes and rescans the whole map on every player step |
| APP-INPUT-UI-05 | low | deliberate | new | `src/app/hud.cpp:15` | HUD prompts are hard-coded to mouse/keyboard and ignore DeviceInUse |
| APP-INPUT-UI-06 | low | confirmed | new | `src/app/target_cursor.cpp:69` | Target cursor steps on every stick threshold crossing: no hysteresis or debounce |
| APP-INPUT-UI-07 | low | partially-confirmed | new | `src/app/target_cursor.cpp:47` | TargetCursor divides by zero if shown on an empty map; Confirm does not check the map matches |
| APP-INPUT-UI-08 | low | confirmed | new | `src/app/damage_numbers.cpp:127` | DamageNumbers::Play(UseEnded) rebuilds the waiting index and rescans waiting numbers on every UseEnded |
| APP-INPUT-UI-09 | low | partially-confirmed | new | `src/app/cast.cpp:88` | CastLatency attributes the next volley to the oldest press even if that press was refused |
| APP-INPUT-UI-10 | low | deliberate | known-deferred | `src/app/camera_control.cpp:167` | Held stick pan ignores focus |
| APP-INPUT-UI-11 | low | deliberate | known-deferred | `src/app/cast.hpp:22` | Cast dropped during dash |
| APP-INPUT-UI-12 | low | partially-confirmed | new | `src/app/hud.cpp:155` | HUD layout assumptions: window coordinates equal target pixels, fixed separators, positional slot index |
| APP-INPUT-UI-13 | low | partially-confirmed | new | `tests/unit/app/bindings_tests.cpp:242` | Tests do not cover input lifecycle holes or scale |
| APP-MAIN-02 | low | confirmed | new | `src/app/app.cpp:291` | Data race: logic thread copies m_options.data while the main thread's hot reload may assign it |
| APP-MAIN-03 | low | partially-confirmed | new | `src/app/app.cpp:311` | No tests for main.cpp or the App orchestration |
| APP-MAIN-04 | low | unverifiable | new | `src/app/app.cpp:533` | Casts are press-only: held input never repeats and the `pressed` parameter is dead |
| APP-MAIN-05 | low | partially-confirmed | new | `src/app/app.cpp:125` | Command-line parsing duplicated five times and unknown flags are ignored |
| APP-MAIN-06 | low | unverifiable | new | `src/app/app.cpp:745` | Hover pick scans every instance every frame on the main thread |
| APP-MAIN-07 | low | partially-confirmed | new | `src/main.cpp:67` | Start-up copies GameData repeatedly and the window sits unresponsive during data load |
| APP-MAIN-08 | low | confirmed | new | `src/app/app.cpp:321` | --quit-after counts from App construction, including scene load time |
| APP-MAIN-09 | low | confirmed | new | `src/app/app.cpp:112` | DataWatch can throw from directory iteration and does not watch subfolders |
| ART-TOOLS-01 | low | partially-confirmed | new | `art/world_map.py:55` | Generator constants duplicated from C++ with no cross-check |
| ART-TOOLS-02 | low | deliberate | contradicts-recorded-decision | `art/world_map.py:79` | The map generator is one hand-tuned map, not a procedural generator |
| ART-TOOLS-03 | low | unverifiable | new | `art/world_map.py:620` | join() and related helpers crash on a single-point polyline |
| ART-TOOLS-04 | low | unverifiable | new | `art/world_map.py:1681` | fray_shore applies mutations that cut across keep and city overrides |
| ART-TOOLS-05 | low | unverifiable | new | `art/world_map.py:1077` | Road routing fit_line can silently produce a line that does not follow the path |
| ART-TOOLS-06 | low | partially-confirmed | new | `art/placeholders.py:167` | PNG output byte-reproducibility depends on the host zlib |
| ART-TOOLS-07 | low | confirmed | contradicts-recorded-decision | `art/sprites.toml:1` | Dead sprites kept 'for stable ID' contradict the no-compat-before-1.0 rule |
| ART-TOOLS-08 | low | partially-confirmed | known-deferred | `art/scenes.toml:5` | Scene lists hand-duplicated and unchecked against what the code draws |
| ART-TOOLS-09 | low | unverifiable | new | `art/world_map.py:1541` | read_world parser is a bespoke YAML subset that can mis-parse hand edits |
| ASSETS-DATA-01 | low | deliberate | known-deferred | `assets/data/creatures.yaml:6` | Creature table is hard-wired to exactly 'cultivator' and 'fox' |
| ASSETS-DATA-02 | low | deliberate | known-deferred | `assets/data/world.yaml:47` | No cultivation, realm, power, item, sect or event schema; 'power' is only a travel threshold |
| ASSETS-DATA-03 | low | confirmed | new | `assets/data/world.yaml:44` | Numeric results of simulation runs are baked into data comments |
| ASSETS-DATA-04 | low | partially-confirmed | new | `assets/data/names.yaml:111` | Names pool has no gender or age, and drawn names will collide heavily at 50,000 NPCs |
| ASSETS-DATA-05 | low | unverifiable | new | `assets/data/abilities.yaml:40` | Flying swords' outer ring lies outside its seal |
| ASSETS-DATA-06 | low | partially-confirmed | new | `assets/data/strings.yaml:161` | Ability display strings share the flat global namespace with UI strings |
| ASSETS-DATA-07 | low | confirmed | new | `assets/data/strings.yaml:152` | Calendar is absent from data: the date line is month and day only |
| BUILD-ENV-01 | low | partially-confirmed | new | `xmake.lua:150` | No hardened standard library or assertions in release builds |
| BUILD-ENV-02 | low | partially-confirmed | new | `xmake.lua:146` | Missing CET, stack-clash protection and explicit PIE |
| BUILD-ENV-03 | low | partially-confirmed | new | `xmake.lua:97` | Linux and macOS release builds embed absolute build paths (no -ffile-prefix-map) |
| BUILD-ENV-04 | low | confirmed | new | `xmake-requires.lock:45` | xmake-requires.lock records build-tool versions that differ from the Pixi pins |
| BUILD-ENV-05 | low | unverifiable | new | `xmake.lua:233` | shared_pch overrides XMake internals (target.pcoutputfile) |
| BUILD-ENV-06 | low | partially-confirmed | new | `THIRD_PARTY_NOTICES.md:7` | THIRD_PARTY_NOTICES omits components SDL bundles in the static link |
| BUILD-ENV-07 | low | confirmed | new | `THIRD_PARTY_NOTICES.md:372` | Notices file has no check tying it to the lock and add_requires |
| BUILD-ENV-08 | low | partially-confirmed | new | `containers/msvc.Containerfile:18` | Container images and the Pixi download rely on network trust beyond pins |
| BUILD-ENV-09 | low | partially-confirmed | new | `.gitattributes:2` | text=auto can rewrite binary fuzz corpus and replay seeds |
| BUILD-ENV-10 | low | partially-confirmed | new | `tests/xmake.lua:5` | Wine test runner uses the host's default prefix and is silent on setup failures |
| BUILD-ENV-11 | low | partially-confirmed | new | `tests/xmake.lua:33` | Per-test `add_tests` pattern matching is fragile (suffix patterns, substring stems) |
| BUILD-ENV-12 | low | confirmed | new | `pixi.toml:127` | profiler environment depends on an alpha libcapstone from a dev label |
| BUILD-ENV-13 | low | partially-confirmed | new | `xmake.lua:283` | Absolute CONDA_PREFIX baked into Linux ldflags at file scope when CONDA_PREFIX is unset |
| BUILD-ENV-14 | low | partially-confirmed | new | `.githooks/pre-commit:4` | Hooks require pixi on PATH and run through `pixi run` for every commit |
| CI-01 | low | partially-confirmed | new | `.github/workflows/ci.yml:433` | Release job does not verify the tag points at a main commit that passed the gate |
| CI-02 | low | partially-confirmed | new | `.github/workflows/ci.yml:414` | `caches` job uses `always()`, so it still runs after the workflow is cancelled, and cancel-in-progress is on |
| CI-03 | low | partially-confirmed | new | `.github/workflows/ci.yml:47` | Untrusted-ish context values interpolated directly into shell scripts |
| CI-04 | low | confirmed | new | `.github/actions/lfs/action.yml:27` | LFS composite action saves caches on tag refs and from every parallel job; the docs say tags write no cache |
| CI-05 | low | partially-confirmed | new | `.github/workflows/ci.yml:1` | CI lacks gates for things local `check --sanitizers`, docs and fuzzing cover; no scheduled job |
| CI-06 | low | partially-confirmed | new | `.github/workflows/ci.yml:12` | `cancel-in-progress` on main cancels the previous main run before its cold caches are saved |
| CORE-INFRA-02 | low | partially-confirmed | new | `src/core/game_data.hpp:24` | f32 world coordinates contradict MIN_WALK_SPEED's premise and cap procedural map size |
| CORE-INFRA-03 | low | partially-confirmed | new | `src/core/view_limits.hpp:25` | WIDEST_HALF_VIEW is not the widest view: narrow, portrait or >32:9 windows show more, and the terrain budget is tested against it |
| CORE-INFRA-04 | low | partially-confirmed | new | `src/core/jobs.cpp:40` | Simulation chunks run on the main thread and on workers, but only the logic thread's FP environment is ever re-checked |
| CORE-INFRA-05 | low | confirmed | new | `tests/unit/core/math_tests.cpp:35` | Maths accuracy tests measure absolute error below 1, so 'a few ulps' is untested for most results |
| CORE-INFRA-06 | low | partially-confirmed | new | `tests/unit/core/math_tests.cpp:26` | Unit tests in scope are sweeps, contrary to tests/AGENTS.md |
| CORE-INFRA-07 | low | partially-confirmed | new | `src/core/jobs.cpp:93` | Save writes, scene decodes and glyph batches share one low priority; a Pending wait can run a save on the main thread |
| CORE-INFRA-08 | low | partially-confirmed | new | `src/core/manifest.hpp:91` | Out-of-range bugs throw std::out_of_range through .at(), against 'our code throws nothing' |
| CORE-INFRA-09 | low | confirmed | new | `src/core/strings.hpp:80` | Enum counts are maintained by hand; fixed arrays silently accept too few initialisers |
| CORE-INFRA-10 | low | confirmed | new | `src/core/names.cpp:67` | NameOf sums every surname weight on every call, per drawn traveller per publish |
| CORE-INFRA-11 | low | partially-confirmed | new | `src/core/names.cpp:12` | Random purposes are allocated in three modules with no central registry or uniqueness check |
| CORE-INFRA-12 | low | partially-confirmed | new | `src/core/schedule.hpp:27` | Division by zero in RunsOn/InSlice and ChunkCount; Rate/RunsOn unused in production |
| CORE-INFRA-13 | low | confirmed | new | `src/core/instances.cpp:79` | NaN inputs to PackUnorm16 and ScaledColour reach lround(NaN), which is unspecified |
| CORE-INFRA-14 | low | confirmed | new | `src/core/draw_list.cpp:118` | DrawList copies every instance and ID twice per publish |
| CORE-INFRA-15 | low | confirmed | new | `src/core/instances.cpp:108` | MakeInstance calls f64 std::remainder twice per instance, even for unrotated sprites |
| CORE-INFRA-16 | low | unverifiable | new | `src/core/ui_colours.hpp:201` | UI colours stored as 8-bit linear premultiplied lose distinct dark shades in the planned colour editor |
| CORE-INFRA-17 | low | confirmed | new | `src/core/log.cpp:54` | No per-run size cap on the log file |
| CORE-INFRA-18 | low | confirmed | new | `tests/unit/core/draw_list_tests.cpp:150` | Draw-list tests never use more than one worker or cross a gather chunk |
| CORE-INFRA-19 | low | confirmed | new | `src/core/module.dox:1` | module.dox and 0002's module table describe core as value types and facades; it is now a 12.6k-line catch-all with game-domain logic |
| CORE-INFRA-20 | low | confirmed | new | `src/AGENTS.md:27` | src/AGENTS.md is 45 KB in 50 'lines', far past the ~100-lines, one-rule-per-line guide |
| CORE-JOURNEY-VOLLEY-01 | low | confirmed | new | `src/core/volley.cpp:158` | Wave boundaries computed as count*(radii/total) truncate below exact integers: uneven or empty waves |
| CORE-JOURNEY-VOLLEY-02 | low | partially-confirmed | new | `src/core/journey.cpp:166` | StayWalked/StayAhead walk every day of a stay on every call; a loaded stay may last ~23.8M days |
| CORE-JOURNEY-VOLLEY-03 | low | partially-confirmed | new | `src/core/volley.cpp:246` | WalkTargets rescans every target at each link: O(T) per move, O(T²) per chain, called several times per sword per frame |
| CORE-JOURNEY-VOLLEY-04 | low | confirmed | new | `src/core/volley.cpp:376` | Each sword's plan (wave search, random draws, cos/sin) is recomputed ~12 times per frame by the renderer |
| CORE-JOURNEY-VOLLEY-05 | low | confirmed | new | `src/core/journey.hpp:7` | journey.hpp claims legs are reached only through Where()/EndTick(); road_curve.cpp duplicates Where's step logic and host/travel read tiles/arrive directly |
| CORE-JOURNEY-VOLLEY-06 | low | confirmed | new | `src/core/journey.hpp:72` | JourneyPart holds both a Leg and a Stay behind a bool: stay parts break Leg's 'tiles never empty', and saves and hashes carry the unused fields |
| CORE-JOURNEY-VOLLEY-07 | low | confirmed | new | `src/core/volley.hpp:166` | VolleyShape::Valid says Volley::Valid checks flightTicks; it checks only commitTicks; startTick is unbounded on load |
| CORE-JOURNEY-VOLLEY-08 | low | confirmed | new | `src/core/journey.hpp:220` | LegWalks::Of promises a table only until the next call, but the KeptWalk overload stores the returned pointers indefinitely |
| CORE-JOURNEY-VOLLEY-09 | low | confirmed | known-deferred | `src/core/journey.cpp:302` | MakeTileLeg guards its u32 arrive ticks with PN_ASSERT only |
| CORE-JOURNEY-VOLLEY-10 | low | deliberate | known-deferred | `src/core/journey.cpp:501` | Each journey copies its route's tiles twice, and every LegWalk copies them a third time |
| CORE-JOURNEY-VOLLEY-11 | low | partially-confirmed | new | `tests/unit/core/volley_tests.cpp:518` | Tests miss the contracts the defects above live in |
| CORE-PERSIST-02 | low | partially-confirmed | new | `src/core/delta.hpp:458` | Save base identity has no fingerprint: a base regenerated differently loads silently |
| CORE-PERSIST-03 | low | partially-confirmed | new | `src/core/delta.hpp:5` | No mechanical guard ties a component's Fields() layout to WORLD_VERSION / ACTIVE_VERSION |
| CORE-PERSIST-04 | low | confirmed | new | `src/core/replay.cpp:95` | Replay load amplifies 1 decompressed byte into a 56-byte Action: a small compressible replay can OOM the game |
| CORE-PERSIST-05 | low | partially-confirmed | new | `src/core/saves.cpp:19` | path.string() in save logs and errors throws on MSVC for non-ANSI profile paths; one call runs inside the background save job |
| CORE-PERSIST-06 | low | confirmed | new | `tests/unit/game/save_exactness_tests.cpp:486` | Committed fuzz seeds are never checked to still load; the replay seed has no generator or check at all |
| CORE-PERSIST-07 | low | partially-confirmed | new | `tests/fuzz/save.cpp:73` | Fuzz targets skip the zstd container and Logic::Restore, check no round trip, and run under ASan only |
| CORE-PERSIST-08 | low | partially-confirmed | contradicts-recorded-decision | `tests/xmake.lua:135` | Fuzz targets and their seed corpora never run in CI or in `check` |
| CORE-PERSIST-09 | low | confirmed | new | `tests/unit/core/replay_tests.cpp:180` | Replay test of never-recorded kinds omits ToggleRecording and PlayReplay |
| CORE-PERSIST-10 | low | partially-confirmed | new | `src/core/save_file.cpp:78` | WriteSaveFile: fixed temp name, close errors ignored, write errors carry no reason |
| CORE-PERSIST-11 | low | confirmed | known-deferred | `src/game/host.cpp:251` | WORLD_VERSION is read after the clock fields it versions, and its mismatch isn't logged |
| CORE-PERSIST-12 | low | partially-confirmed | new | `src/core/replay.cpp:120` | Replay keeps stale state across mode changes: a dangling m_start span and retained buffers |
| CORE-PERSIST-13 | low | confirmed | new | `src/core/handover.hpp:159` | Handover::ReturnState writes m_free[m_freeCount++] with no bound check |
| CORE-PERSIST-14 | low | confirmed | new | `src/core/delta.hpp:382` | The order section rewrites a storage's entire ID list after any membership churn |
| CORE-PERSIST-15 | low | partially-confirmed | new | `src/core/delta.hpp:501` | No semantic caps on loaded entity counts: memory bounded only by the 1 GiB decompressed body |
| CORE-PERSIST-16 | low | confirmed | new | `docs/design/0002.2-replays.md:79` | Replay design doc's format block is stale |
| CORE-PERSIST-17 | low | partially-confirmed | new | `src/core/logic.cpp:333` | Logic::Restore accepts any tick counters from a save or replay start |
| CORE-SIM-01 | low | partially-confirmed | new | `src/core/logic.cpp:134` | A month falling due on the tick that asks for a scene change carries the calendar step and its transition across the change |
| CORE-SIM-02 | low | confirmed | new | `src/core/logic.hpp:139` | A replay being recorded is silently lost when the game quits |
