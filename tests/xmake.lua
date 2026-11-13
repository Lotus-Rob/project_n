-- One test executable per area (tests/unit/<area>/), registered with `xmake test`.

-- `xmake test` runs test binaries directly; only `xmake run` knows to use Wine. Windows tests
-- built on Linux (msvc-wine) run through it explicitly, so nothing depends on the host's binfmt.
-- Its output is kept as XMake keeps a native test's, so `--output-on-failure` shows only the failing
-- files' output, not every file's run printed at once, interleaved.
local function test_under_wine(target, opt)
    import("lib.detect.find_tool")
    local wine = assert(find_tool("wine"), "wine not found")
    local args = table.join(path.absolute(target:targetfile()), table.wrap(opt.runargs or target:get("runargs")))
    local log = path.absolute(path.join(target:autogendir(), "tests", (opt.name:gsub("[/\\>=<|%*]", "_"))))
    os.mkdir(path.directory(log))
    os.tryrm(log .. ".out") -- a run Wine fails to start must not show the previous run's output
    os.tryrm(log .. ".err")
    local code = os.execv(wine.program, args, {try = true, curdir = target:rundir(),
                                               stdout = log .. ".out", stderr = log .. ".err"})
    for _, kind in ipairs({"out", "err"}) do
        local output = io.readfile(log .. "." .. kind)
        if output and #output > 0 then -- XMake prints an empty one too
            opt["std" .. kind] = output
        end
    end
    if code ~= 0 then
        opt.errors = string.format("run %s failed, exit code: %s", opt.name, tostring(code))
    end
    return code == 0
end

-- What a target that builds an arena needs: the shipped manifest (support/manifest.hpp) and its reader.
local function with_manifest()
    add_deps("data", "platform")
    add_includedirs(os.scriptdir())
    add_rules("assets")
end

-- doctest's `--source-file` value for the cases of `<stem>_tests.cpp`: `test_file_pattern` in tools/project_n.py.
local function test_file_pattern(stem)
    return "*/" .. stem .. "_tests.cpp,*\\" .. stem .. "_tests.cpp"
end

-- One test per `*_tests.cpp` file, named after it, so `xmake test` runs a target's files in parallel, and `rest`
-- for the cases outside them (main.cpp, a header): no case drops out. doctest passes a run that selects nothing.
-- No test file may be called `rest_tests.cpp` (the selftest checks). Description scope: no assert or raise here.
local function add_file_tests(files)
    local patterns = {}
    for _, glob in ipairs(table.wrap(files)) do
        for _, file in ipairs(os.files(path.join(os.scriptdir(), glob))) do
            local stem = path.basename(file)
            if stem:endswith("_tests") then
                stem = stem:sub(1, -#"_tests" - 1)
                table.insert(patterns, test_file_pattern(stem))
                add_tests(stem, {runargs = {"--source-file=" .. test_file_pattern(stem)}})
            end
        end
    end
    add_tests("rest", {runargs = {"--source-file-exclude=" .. table.concat(patterns, ",")}})
end

-- `module` is the src/ module under test (default core); it brings the modules below it. `arena`: it builds arenas.
local function unit_tests(name, files, module, arena)
    target(name)
        set_kind("binary")
        add_deps(module or "core")
        if arena then
            with_manifest()
        end
        -- Out of unity batches: doctest names test cases by __LINE__ under Clang 22 (__COUNTER__ is a
        -- C2y extension there), so two files' cases on one line would clash. src/ stays in them.
        add_files(files, {unity_ignored = true})
        set_pcxxheader("unit/pch.hpp")
        add_packages("doctest")
        add_file_tests(files)
        if is_plat("windows") and not is_host("windows") then
            on_test(test_under_wine)
        end
end

unit_tests("core_tests", "unit/core/*.cpp")
unit_tests("game_tests", "unit/game/*.cpp", "game", true)
    -- Where the skipped "write the fight save seed" case writes (save_tests.cpp), whatever the working directory.
    add_defines('PN_FUZZ_CORPUS="' .. path.join(os.projectdir(), "tests", "fuzz", "corpus"):gsub("\\", "/") .. '"')
unit_tests("platform_tests", "unit/platform/*.cpp", "platform")
unit_tests("gfx_tests", "unit/gfx/*.cpp", "gfx")
    add_rules("assets") -- the font (ttf_tests.cpp)
    add_packages("libsdl3_ttf-wayland") -- private to gfx; its test reaches SDL3_ttf directly
    add_deps("data") -- the shipped strings, which the glyph cache pre-warms (text_layout_tests.cpp)
unit_tests("app_tests", "unit/app/*.cpp", "app", true)
unit_tests("data_tests", "unit/data/*.cpp", "data", true)
-- Logging start-up states: Quill's loggers live for the whole process, so each test needs its own.
unit_tests("log_early_tests", {"unit/log_startup/main.cpp", "unit/log_startup/early_tests.cpp"})
unit_tests("log_fallback_tests", {"unit/log_startup/main.cpp", "unit/log_startup/fallback_tests.cpp"})

-- The headless benchmark (design doc 0002 §16): ticks a large world without a window or a GPU. Built
-- with the tests and run on request: `project_n test --mode release --target headless_bench`.
target("headless_bench")
    set_kind("binary")
    add_deps("game")
    with_manifest()
    add_files("bench/headless.cpp")
    set_pcxxheader("$(projectdir)/src/pch.hpp") -- not a doctest suite: the game's, without doctest

-- Pictures of fixed scenes compared with tests/reference/ (design doc 0002 §8.9). Built with the tests
-- but not registered as one, as it needs a GPU: `project_n reference` runs it on Mesa's software Vulkan.
target("reference_tests")
    set_kind("binary")
    add_deps("app") -- the damage numbers it draws; it brings gfx and game
    with_manifest()
    add_files("unit/gfx/main.cpp", "reference/*.cpp", {unity_ignored = true})
    set_pcxxheader("unit/pch.hpp")
    add_packages("doctest")

-- libFuzzer targets, one per tests/fuzz/<name>.cpp: linux-clang with AddressSanitizer only
-- (`project_n fuzz <name>`). There each also replays its seed corpus (tests/fuzz/corpus/<name>/)
-- as a test, so `check --sanitizers` keeps them building and passing.
if is_plat("linux") and get_config("toolchain") == "pixi-clang" and get_config("sanitizer") == "address" then
    for _, file in ipairs(os.files(path.join(os.scriptdir(), "fuzz", "*.cpp"))) do
        local name = path.basename(file)
        target("fuzz_" .. name)
            set_kind("binary")
            add_deps("game", "data") -- the readers under test, and core below them
            with_manifest()
            add_files(file)
            set_pcxxheader("$(projectdir)/src/pch.hpp") -- not a doctest suite: the game's, without doctest
            add_cxflags("-fsanitize=fuzzer", {force = true})
            -- conda-forge builds the libFuzzer runtime against libstdc++ (upstream bundles a private
            -- libc++); its std::__cxx11 symbols don't clash with our libc++'s std::__1. no-as-needed:
            -- clang adds the runtime after our flags, so --as-needed would drop libstdc++ first.
            add_ldflags("-fsanitize=fuzzer", "-Wl,--push-state,--no-as-needed,-lstdc++,--pop-state", {force = true})
            add_tests("corpus", {runargs = {"-runs=0", path.join(os.scriptdir(), "fuzz", "corpus", name)}})
    end
end
