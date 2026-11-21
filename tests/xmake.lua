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

-- What a target that builds a host needs: the shipped manifest (support/manifest.hpp) and its reader.
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

-- The tests' precompiled header, shared by every doctest target (the shared_pch rules in xmake.lua).
target("test_pch")
    set_kind("object")
    add_rules("c++", "shared_pch_owner")
    set_policy("build.fence", true)
    set_pcxxheader("unit/pch.hpp")
    add_deps("game_pch") -- the flags every module passes on
    add_packages("doctest")

-- doctest's CHECK takes its operands through an expression template whose evaluation order MSVC can't promise
-- left to right (4866, 4868), which is harmless for a check's operands; 5246 asks for double braces in arrays of
-- aggregates, which test tables read worse with.
local function test_warnings()
    if is_plat("windows") then
        add_cxflags("/wd4866", "/wd4868", "/wd5246")
    end
end

-- `module` is the src/ module under test (default core); it brings the modules below it. `arena`: it builds hosts.
local function unit_tests(name, files, module, arena)
    target(name)
        set_kind("binary")
        add_deps(module or "core")
        add_includedirs(os.scriptdir()) -- support/<name>.hpp
        if arena then
            with_manifest()
        end
        add_files(files)
        add_deps("test_pch")
        set_pcxxheader("unit/pch.hpp")
        add_rules("shared_pch", {owner = "test_pch"})
        add_packages("doctest")
        test_warnings()
        add_file_tests(files)
        if is_plat("windows") and not is_host("windows") then
            on_test(test_under_wine)
        end
end

unit_tests("core_tests", "unit/core/*.cpp")
unit_tests("game_tests", "unit/game/*.cpp", "game", true)
unit_tests("platform_tests", "unit/platform/*.cpp", "platform")
unit_tests("gfx_tests", "unit/gfx/*.cpp", "gfx", true) -- the shipped manifest (terrain_draw_tests.cpp), strings (text_layout_tests.cpp, which the glyph cache pre-warms) and font (ttf_tests.cpp)
    add_packages("libsdl3_ttf-wayland") -- private to gfx; its test reaches SDL3_ttf directly
unit_tests("app_tests", "unit/app/*.cpp", "app", true)
unit_tests("data_tests", "unit/data/*.cpp", "data", true)
-- Logging start-up states: Quill's loggers live for the whole process, so each test needs its own.
unit_tests("log_early_tests", {"unit/log_startup/main.cpp", "unit/log_startup/early_tests.cpp"})
unit_tests("log_fallback_tests", {"unit/log_startup/main.cpp", "unit/log_startup/fallback_tests.cpp"})

-- The headless benchmark: ticks a large world without a window or a GPU. Built
-- with the tests and run on request: `project_n test --mode release --target headless_bench`.
target("headless_bench")
    set_kind("binary")
    add_deps("game")
    with_manifest()
    add_files("bench/headless.cpp")
    set_pcxxheader("$(projectdir)/src/pch.hpp") -- not a doctest suite: the game's, without doctest
    add_rules("shared_pch", {owner = "game_pch"})

-- Pictures of fixed scenes compared with tests/reference/. Built with the tests
-- but not registered as one, as it needs a GPU: `project_n reference` runs it on Mesa's software Vulkan.
target("reference_tests")
    set_kind("binary")
    add_deps("app") -- the damage numbers it draws; it brings gfx and game
    with_manifest()
    add_files("unit/gfx/main.cpp", "reference/*.cpp")
    add_deps("test_pch")
    set_pcxxheader("unit/pch.hpp")
    add_rules("shared_pch", {owner = "test_pch"})
    add_packages("doctest")
    test_warnings()

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
