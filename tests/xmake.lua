-- One test executable per area (tests/unit/<area>/), registered with `xmake test`.

-- `xmake test` runs test binaries directly; only `xmake run` knows to use Wine. Windows tests
-- built on Linux (msvc-wine) run through it explicitly, so nothing depends on the host's binfmt.
local function test_under_wine(target, opt)
    import("lib.detect.find_tool")
    local wine = assert(find_tool("wine"), "wine not found")
    local args = table.join(path.absolute(target:targetfile()), table.wrap(opt.runargs or target:get("runargs")))
    return os.execv(wine.program, args, {try = true, curdir = target:rundir()}) == 0
end

-- `module` is the src/ module under test (default core); it brings the modules below it.
local function unit_tests(name, files, module)
    target(name)
        set_kind("binary")
        add_deps(module or "core")
        -- Out of unity batches: doctest names test cases by __LINE__ under Clang 22 (__COUNTER__ is a
        -- C2y extension there), so two files' cases on one line would clash. src/ stays in them.
        add_files(files, {unity_ignored = true})
        set_pcxxheader("unit/pch.hpp")
        add_packages("doctest")
        add_tests("default")
        if is_plat("windows") and not is_host("windows") then
            on_test(test_under_wine)
        end
end

unit_tests("core_tests", "unit/core/*.cpp")
unit_tests("platform_tests", "unit/platform/*.cpp", "platform")
-- Logging start-up states: Quill's loggers live for the whole process, so each test needs its own.
unit_tests("log_early_tests", {"unit/log_startup/main.cpp", "unit/log_startup/early_tests.cpp"})
unit_tests("log_fallback_tests", {"unit/log_startup/main.cpp", "unit/log_startup/fallback_tests.cpp"})

-- The headless benchmark (design doc 0002 §16): ticks a large world without a window or a GPU. Built
-- with the tests and run on request: `project_n test --mode release --target headless_bench`.
target("headless_bench")
    set_kind("binary")
    add_deps("core")
    add_files("bench/headless.cpp")
    set_pcxxheader("$(projectdir)/src/pch.hpp") -- not a doctest suite: the game's, without doctest

-- libFuzzer targets, one per tests/fuzz/<name>.cpp: linux-clang with AddressSanitizer only
-- (`project_n fuzz <name>`). There each also replays its seed corpus (tests/fuzz/corpus/<name>/)
-- as a test, so `check --sanitizers` keeps them building and passing.
if is_plat("linux") and get_config("toolchain") == "pixi-clang" and get_config("sanitizer") == "address" then
    for _, file in ipairs(os.files(path.join(os.scriptdir(), "fuzz", "*.cpp"))) do
        local name = path.basename(file)
        target("fuzz_" .. name)
            set_kind("binary")
            add_files(file)
            add_includedirs("$(projectdir)/src")
            set_pcxxheader("unit/pch.hpp")
            add_packages("doctest") -- the precompiled header includes it
            add_cxflags("-fsanitize=fuzzer", {force = true})
            -- conda-forge builds the libFuzzer runtime against libstdc++ (upstream bundles a private
            -- libc++); its std::__cxx11 symbols don't clash with our libc++'s std::__1. no-as-needed:
            -- clang adds the runtime after our flags, so --as-needed would drop libstdc++ first.
            add_ldflags("-fsanitize=fuzzer", "-Wl,--push-state,--no-as-needed,-lstdc++,--pop-state", {force = true})
            add_tests("corpus", {runargs = {"-runs=0", path.join(os.scriptdir(), "fuzz", "corpus", name)}})
    end
end
