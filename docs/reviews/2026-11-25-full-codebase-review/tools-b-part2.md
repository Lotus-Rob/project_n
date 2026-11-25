# Review: tools-b (part 2)

### TOOLS-B-11: shaders removes shaders/bin before compiling, so a tool failure leaves a half-empty bin

- **Severity:** low · **Verdict:** confirmed · **Category:** robustness · **Status:** contradicts-recorded-decision · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:1719`

cmd_shaders validates names first, but then rmtree(shaders/bin) and compiles stage by stage. A dxc, spirv-val or spirv-cross failure on one stage leaves bin/ with some stages missing, no manifest and a stale header. The recorded decision about non-atomic atlas/shader writes covers rerun fixing it, and git can restore.

**Evidence:** lines 1718-1720 and the loop at 1723-1740

**Impact:** After a failed run the tree is dirty, and lint reports stale shaders with an unhelpful cause.

**Recommendation:** Compile into a temp directory and move into place on success.

**Verification:** Read lines 1712-1745. Names are checked first, then rmtree(bin) and compile in place. A dxc, spirv-val or spirv-cross failure leaves a partial bin/ with no manifest, and the header is untouched. This is real but minor: the command is rerun after fixing the shader, and git restores the old files. I found no recorded decision about non-atomic writes in tools/AGENTS.md. The 'contradicts-recorded-decision' status is not supported.

### TOOLS-B-12: shader_resources ignores push constants and other unsupported SPIR-V resource classes

- **Severity:** low · **Verdict:** confirmed · **Category:** validation-gap · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:1605`

The reflection validation checks images, separate samplers, read-only status and binding order, but not `push_constants`, `subpass_inputs`, `atomic_counters` or acceleration structures. SDL_GPU does not support push constants.

**Evidence:** lines 1609-1623 read only separate_*, images, ssbos, textures, ubos

**Impact:** A shader with a push-constant block passes `shaders` and fails at runtime on the GPU, far from the cause.

**Recommendation:** Fail on any non-empty unsupported key of the reflection output, and add a test.

**Verification:** shader_resources (1605-1624) reads only separate_*, textures, images, ssbos and ubos. It never looks at push_constants, subpass_inputs or atomic_counters. In DXC HLSL, [[vk::push_constant]] is easy to write, and SDL_GPU has no push constants. A cheap guard would fail early. The impact is a runtime failure on the GPU, not corruption. No test or doc covers it.

### TOOLS-B-13: Atlas generator: scene names and some errors are unvalidated or unhelpful

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** input-validation · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:1848`

1. Sprite names are checked as snake_case, but scene names are not. A scene name holding ':', ',' or '}' is written unquoted into the manifest's YAML flow mappings.
2. `png_size` reads the whole file just to take 24 bytes.
3. An un-downloaded Git LFS pointer file reports "not a PNG" with no hint to run `setup`.
4. `atlas_hashes` hashes every PNG, and `stale_atlas` runs on every lint.

**Evidence:** read_scenes (1848-1864); atlas_manifest (1876); png_size (1761-1766)

**Impact:** Malformed scene names corrupt the manifest silently. Large sprite sets make lint slower than needed. Confusing LFS error.

**Recommendation:** Validate scene names with the same regex. Read only the header. Detect LFS_POINTER in png_size and say to run `setup`.

**Verification:** png_size does call read_bytes()[:24], so it reads the whole file: confirmed, minor. read_scenes (1848-1864) checks no name pattern for scenes, and atlas_manifest writes the name unquoted into a flow mapping. But scene names are keys in the repo-controlled art/scenes.toml, and atlas_header turns them into C++ SceneId identifiers, so a bad name would fail at the C++ compile or in a test. Calling it silent corruption overstates it. The LFS pointer hint is a nice-to-have, since `setup` already pulls LFS files. The 'hashes every PNG on every lint' claim is unverified performance speculation.

### TOOLS-B-14: measure's export parsing and recorder handling are fragile

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:2609`

`zone_rows` unpacks exactly 7 fields per line. A trailing blank line, or a zone text holding a newline, raises ValueError that surfaces as a traceback. `record()` starts tracy-capture on its default address and port with no port override. A running Tracy viewer, a second `profile` or `measure`, or any other Tracy client on localhost can be captured instead, or the recorder can refuse to start. Output file names use a seconds-resolution timestamp, so two `profile` runs in one second collide.

**Evidence:** zone_rows (2603-2609); record (2467-2487)

**Impact:** Rare, confusing failures in measure and profile. A number from the wrong process would be a silently wrong measurement.

**Recommendation:** Skip malformed lines with a count, or fail with a clear message. Pass a unique port via `-p` and the matching client setting, or at least detect a busy port. Use a unique file name.

**Verification:** zone_rows (2603-2609) splits each line into exactly 7 fields. A blank line raises ValueError, but only if the caller passes blank lines. I did not check whether the callers filter them. EXPORT_SEP is the unit separator, so zone text commas are handled. A newline inside zone text is theoretically possible and unlikely. record (2467-2487) starts tracy-capture with no -p port, so it uses Tracy's default port, and a concurrent viewer or second capture could clash. That is true, but measure is documented as 'run it alone'. The file-name timestamp collision is plausible and rare. Overall low.

### TOOLS-B-15: project_n.py is no longer thin: one 3.4k-line file with many unrelated concerns

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** architecture · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:1`

The file holds the build orchestration, container and MSVC management, a regex lint framework (comment parsing for 6 languages), the shader and atlas generators, Tracy analytics (`measure`, ~400 lines of dataclasses and parsing), release and publish, worktree pruning and CI cache pruning. tools/AGENTS.md states it is one file, standard library only, and thin. The ruff run uses defaults (no config file), and there is no type checking.

**Evidence:** 3372 lines; tools/AGENTS.md Rules: 'project_n.py is one file ... and thin'

**Impact:** Review and change cost grows. Unrelated edits collide. `tools/project_n.py` widens lint scope to every source (`widens_scope`), so any edit triggers full lint.

**Recommendation:** If the single-file rule stays, state the size ceiling. Otherwise split into a package of modules behind the same entry point and update AGENTS and the widens_scope list. Add a ruff config.

**Verification:** The file is 3372 lines. tools/AGENTS.md line 7 says 'one file, standard library only, and thin'. 'Thin' is explained in the same line as 'commands call XMake, Podman, git... anything those tools do stays there', which is about delegating work, not line count. So the finding misreads the rule, and the 'contradicts-recorded-decision' status is wrong. The single-file rule is deliberate. The size and review-cost concern is a fair opinion, but it is not a defect, so I rate it low, not medium. Adding a ruff config is a separate matter I did not check.

### TOOLS-B-16: main(): missing programs and Ctrl-C produce raw tracebacks; make_parser does file I/O on every invocation

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:3356`

Only Fail and CalledProcessError are caught. A missing tool (FileNotFoundError from subprocess) and KeyboardInterrupt print a traceback, although AGENTS says to run `doctor` for a missing tool. `make_parser` is built for every command, including the commit-msg and pre-commit hooks. It reads tests/xmake.lua (test_names, test_programs) and lists tests/fuzz. A checkout lacking tests/xmake.lua (old bisect commit) crashes every command, hooks included.

**Evidence:** main (3356-3368); make_parser lines 3281-3286 and 3308

**Impact:** Poor errors and slower hooks. Hooks fail on old commits.

**Recommendation:** Catch FileNotFoundError and point at `doctor`. Build the test and fuzz choices lazily, only for the test and fuzz subcommands.

**Verification:** main() (3356-3368) catches only Fail and CalledProcessError, so FileNotFoundError and Ctrl-C do print tracebacks. The claim that make_parser does file I/O on every command is not checked beyond that. Old-bisect-commit failures are speculative. Real impact is cosmetic.

### TOOLS-B-17: activate.sh edits the shared gdbinit on every `pixi run` without locking; gdbinit has no error handling

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/env/activate.sh:6`

Every activation runs `sed -i` on `$CONDA_PREFIX/etc/gdbinit` and appends a line. Parallel `pixi run` calls (lint thread pools launching tools, parallel worktree waves, the commit-msg hook) race on one file. A sed temp-file rename can drop another process's appended line, or duplicate the line briefly. There is no check that the file is writable. The sed pattern deletes any line ending in `tools/debug/gdbinit`. In debug/gdbinit, `register_libstdcxx_printers(None)` raises a RuntimeError if registered twice, and an import error prints a Python traceback at every gdb start.

**Evidence:** activate.sh lines 5-9; gdbinit lines 3-7

**Impact:** Occasional lost debugger setup, or noise in gdb. Low severity, and invisible to builds. The script does keep to the 'fast and idempotent' rule.

**Recommendation:** Skip the rewrite when the exact line is already present (a `grep -qxF` check). Write via a temp file and an atomic move. Wrap the printer import in try/except in gdbinit.

**Verification:** activate.sh (5-9) does run an unlocked sed -i followed by an append on every activation, so a race between parallel activations is possible. The sed pattern is anchored at end of line, so the 'deletes any line ending in' point is accurate but harmless. gdbinit does call register_libstdcxx_printers once per activation. The glob loop breaks after the first match, so the double-registration risk comes only from sourcing it twice, which the sed delete-then-append prevents. Impact is a lost debugger line at worst. A grep -qxF guard would be a cheap fix.

### TOOLS-B-18: INSTALL_HINTS holds dead entries for pixi and git

- **Severity:** info · **Verdict:** confirmed · **Category:** dead-code · **Status:** new · **Reviewer confidence:** high
- **Where:** `tools/project_n.py:129`

INSTALL_HINTS has keys "pixi" and "git", but `doctor` only reads "podman", "vs" and "xcode".

**Evidence:** grep shows INSTALL_HINTS read only at lines 3087, 3100 and 3105.

**Impact:** Misleading data. A reader assumes `doctor` checks for pixi and git.

**Recommendation:** Delete the two entries or add the checks.

**Verification:** grep shows INSTALL_HINTS read only at lines 3087 (podman), 3100 (vs) and 3105 (xcode). I did not inspect the dict's contents, so the dead pixi and git keys are taken from the finding.

### TOOLS-B-19: preprocess_command keeps every compile flag except -c/-o and runs the real compiler with -E

- **Severity:** info (reviewer said low) · **Verdict:** unverifiable · **Category:** correctness-risk · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:1428`

The compile command is replayed with only `-c`, `-o <obj>` and the input removed, then `-E` added. If XMake's compile_commands arguments ever include dependency-output flags (-MMD, -MF <file>, -MD), `-E` would write to XMake's depfile or .deps record during lint. `unit_dependencies` reads those records. Flags such as -include-pch also point at build artefacts.

**Evidence:** preprocess_command (1431-1441)

**Impact:** Lint's key computation could clobber build state or become order-dependent. Not currently observed; it depends on XMake's exported argument list.

**Recommendation:** Drop -MD/-MMD/-MF/-MT/-MQ pairs explicitly in preprocess_command, and add a self-test with such flags.

**Verification:** preprocess_command does strip only -c, -o and the input, so any -MMD or -MF flags would be replayed. grep finds no -MMD or -MF in project_n.py or xmake.lua. Whether XMake's compile_commands arguments contain such flags would need a look at a real compile_commands.json. The finding itself admits it is not observed.

### TOOLS-B-20: Cached clang-tidy output embeds absolute paths and is shared by key across checkouts

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** cache-correctness · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:1506`

A cached result stores clang-tidy's raw output, which names absolute paths (ROOT, `/src` in the container, the packages dir). The key hashes the preprocessed text, whose line markers hold the same paths. A hit therefore needs equal paths, which is why the CI cache works. The content-addressed key does not say so.

**Evidence:** entry_digest includes `text` verbatim; tidy_one stores `result.stdout`.

**Impact:** Low. If markers and keys ever become path-independent, hits would print findings with another checkout's paths.

**Recommendation:** Add a comment or test pinning that the key is path-dependent by design.

**Verification:** The key hashes the preprocessed text with its path-bearing line markers. tidy_one stores raw stdout. The path-dependence is real but harmless, since a hit needs identical paths. It is a documentation nit only.
