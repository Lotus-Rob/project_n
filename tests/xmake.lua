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
    add_files("unit/core/*.cpp")
    add_includedirs("$(projectdir)/src")
    set_pcxxheader("unit/pch.hpp")
    add_packages("doctest")
    add_tests("default")
    if is_plat("windows") and not is_host("windows") then
        on_test(test_under_wine)
    end
