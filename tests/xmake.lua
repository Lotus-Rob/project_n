-- One test executable per area (tests/unit/<area>/), registered with `xmake test`.

-- `xmake test` runs test binaries directly; only `xmake run` knows to use Wine. Windows tests
-- built on Linux (msvc-wine) run through it explicitly, so nothing depends on the host's binfmt.
local function test_under_wine(target, opt)
    import("lib.detect.find_tool")
    local wine = assert(find_tool("wine"), "wine not found")
    local args = table.join(path.absolute(target:targetfile()), table.wrap(opt.runargs or target:get("runargs")))
    return os.execv(wine.program, args, {try = true, curdir = target:rundir()}) == 0
end

target("core_tests")
    set_kind("binary")
    add_files("unit/core/*.cpp", "$(projectdir)/src/core/log.cpp")
    add_includedirs("$(projectdir)/src")
    set_pcxxheader("unit/pch.hpp")
    add_packages("doctest", "quill-wine")
    add_tests("default")
    if is_plat("windows") and not is_host("windows") then
        on_test(test_under_wine)
    end

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
            add_cxflags("-fsanitize=fuzzer", {force = true})
            -- conda-forge builds the libFuzzer runtime against libstdc++ (upstream bundles a private
            -- libc++); its std::__cxx11 symbols don't clash with our libc++'s std::__1. no-as-needed:
            -- clang adds the runtime after our flags, so --as-needed would drop libstdc++ first.
            add_ldflags("-fsanitize=fuzzer", "-Wl,--push-state,--no-as-needed,-lstdc++,--pop-state", {force = true})
            add_tests("corpus", {runargs = {"-runs=0", path.join(os.scriptdir(), "fuzz", "corpus", name)}})
    end
end
