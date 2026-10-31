set_project("project_n")
set_version("0.1.0")
set_xmakever("3.1.2")
set_policy("package.requires_lock", true)
-- releasedbg is the profiling build (Tracy, below); coverage is `project_n coverage` (gcov flags).
add_rules("mode.debug", "mode.release", "mode.releasedbg", "mode.coverage")
add_repositories("project_n-repo xmake/repo")

-- Sanitizers only on request (`project_n test --sanitizer <name>`), never in the default gate.
-- project_n.py holds which ones each preset supports. Set per target, not project-wide: XMake
-- passes a project-level sanitizer to every package, and some (wayland) can't link instrumented.
option("sanitizer", {default = "none", values = {"none", "address", "undefined", "thread", "leak"},
                     description = "Build with a sanitizer"})
rule("sanitizer")
    on_load(function (target)
        local sanitizer = get_config("sanitizer")
        if sanitizer and sanitizer ~= "none" then
            target:set("policy", "build.sanitizer." .. sanitizer, true)
        end
    end)
rule_end()
add_rules("sanitizer")

-- LTO in optimised builds, per target for the same reason: XMake hands a project-level LTO policy
-- to every package. Sanitizer builds (never shipped) skip it.
rule("lto")
    on_load(function (target)
        local sanitizer = get_config("sanitizer")
        if is_mode("release", "releasedbg") and (not sanitizer or sanitizer == "none") then
            target:set("policy", "build.optimization.lto", true)
            if target:is_plat("linux") and get_config("toolchain") == "pixi-clang" then
                -- The env's ld has no LLVMgold plugin, so link with lld. Forced: XMake probes
                -- -flto=thin alone, with the default ld, and silently drops it.
                target:add("ldflags", "-fuse-ld=lld", "-flto=thin", {force = true})
            end
        end
    end)
rule_end()
add_rules("lto")

-- Release builds keep debug info, split off by XMake into a .sym, .dSYM or .pdb next to the
-- stripped binary, so player crash reports can be symbolised. Only the binary ships.
if is_mode("release") then
    set_symbols("debug")
end

-- The highest warning level on every compiler, always as errors. Packages are unaffected:
-- they're built separately, and their headers are system/external includes for our code.
set_warnings("everything", "pedantic", "error")

local sanitized = get_config("sanitizer") and get_config("sanitizer") ~= "none"

if is_plat("windows") then
    -- MSVC 14.50 has no /std:c++23: it warns (D9002, not fatal even with /WX), ignores it and
    -- compiles as C++14. Its C++23 mode is /std:c++23preview; src/core/cxx23.hpp guards the rest.
    add_cxxflags("/std:c++23preview")
    -- Static C++ runtime, like Linux: players need no Visual C++ Redistributable.
    set_runtimes("MT")
    -- Reproducible binaries (S2), except in releasedbg: profiling builds never ship, and enabled
    -- Tracy embeds full source paths, which /experimental:deterministic rejects (C5049).
    if not is_mode("releasedbg") then
        -- The linker otherwise stamps the link time into the PE header and debug directory, the
        -- only bytes that differed between two machines' builds.
        add_cxflags("/Brepro", {force = true})
        add_ldflags("/Brepro", {force = true})
        -- LTCG (/GL, /LTCG) reorders .rdata constants from run to run without this. The .pdb's
        -- bytes still vary; it matches its binary by the content-derived GUID /Brepro writes.
        add_cxflags("/experimental:deterministic", {force = true})
        add_ldflags("/experimental:deterministic", {force = true})
    end
    -- Library headers (<...>) are external: no warnings from code we don't own. XMake's own
    -- /external:I paths don't match under msvc-wine, so classify by include style instead.
    -- Forced: XMake probes each flag alone, and cl rejects /external:anglebrackets without
    -- /external:W, so the probe silently dropped it on native Windows.
    add_cxflags("/external:anglebrackets", "/external:W0", {force = true})
    -- /Wall's codegen notes rather than code issues: padding (4820), inlining decisions (4710,
    -- 4711), removed unused inlines (4514), Spectre mitigation points (5045).
    add_cxflags("/wd4820", "/wd4710", "/wd4711", "/wd4514", "/wd5045")
    if not sanitized then
        add_cxflags("/guard:cf") -- Control Flow Guard for shipped builds
        add_ldflags("/guard:cf")
    end
    if is_mode("debug") then
        add_defines("_MSVC_STL_HARDENING=1") -- bounds-checked STL in development builds; no ABI change
    end
else
    set_languages("c++23")
    add_cxflags("-fstack-protector-strong")
    if is_mode("release", "releasedbg") then
        add_defines("_FORTIFY_SOURCE=2") -- 3 needs glibc 2.33 headers; the baseline is 2.28
    end
    if is_mode("debug") then
        -- Bounds-checked standard library in development builds; neither changes the ABI.
        add_defines(get_config("toolchain") == "pixi-gcc" and "_GLIBCXX_ASSERTIONS"
                    or "_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE")
    end
    if get_config("toolchain") == "pixi-clang" then
        -- -Weverything includes compatibility warnings for standards older than C++23.
        add_cxflags("-Wno-c++98-compat", "-Wno-c++98-compat-pedantic", "-Wno-pre-c++20-compat-pedantic")
    end
end

-- conda-forge compilers from the Pixi env; names are triplet-prefixed, so XMake's
-- built-in gcc/clang toolchains would silently pick the host compilers instead.
local triplet = is_host("macosx") and "arm64-apple-darwin20.0.0-" or "x86_64-conda-linux-gnu-"
local function pixi_toolchain(name, cc, cxx, flags)
    toolchain(name)
        set_kind("standalone")
        set_toolset("cc", triplet .. cc)
        set_toolset("as", triplet .. cc)
        set_toolset("cxx", triplet .. cxx)
        set_toolset("ld", triplet .. cxx)
        set_toolset("sh", triplet .. cxx)
        set_toolset("ar", triplet .. "ar")
        set_toolset("strip", triplet .. "strip")
        set_toolset("objcopy", triplet .. "objcopy") -- splits release debug info into a .sym
        on_load(function (tc)
            local prefix = os.getenv("CONDA_PREFIX")
            assert(prefix, "run through `pixi run project_n`: CONDA_PREFIX is not set")
            flags(tc, prefix)
        end)
    toolchain_end()
end

pixi_toolchain("pixi-gcc", "gcc", "g++", function (tc, prefix) end)

-- libc++ (and its static archives) live in the env's lib/, outside the sysroot search path.
pixi_toolchain("pixi-clang", "clang", "clang++", function (tc, prefix)
    tc:add("cxxflags", "-stdlib=libc++")
    if is_host("macosx") then
        -- Exactly the env's libc++, by path: a -L into the env let its other libraries shadow
        -- the SDK's (artefacts linked @rpath/libiconv). libc++abi is the system's, as for conda.
        local libcxx = {"-nostdlib++", prefix .. "/lib/libc++.1.dylib", "-lc++abi"}
        tc:add("ldflags", libcxx)
        tc:add("shflags", libcxx)
    else
        tc:add("ldflags", "-stdlib=libc++", "-L" .. prefix .. "/lib")
        tc:add("shflags", "-stdlib=libc++", "-L" .. prefix .. "/lib")
    end
end)

-- Our executables link the C++ runtime and libgcc statically so artefacts need only glibc
-- (baseline 2.28). macOS is the exception, below. Project-level flags, so packages built from source never inherit them.
-- Sanitizer builds (never shipped) link it dynamically instead: sanitizer runtimes replace parts
-- of it (operator new, __cxa_guard_*), and a whole-archived static libc++abi collides with TSan.
if is_plat("linux") then
    local prefix = os.getenv("CONDA_PREFIX") or ""
    add_ldflags("-Wl,-z,relro,-z,now") -- full RELRO: the GOT is read-only after startup
    if not sanitized then
        add_ldflags("-static-libstdc++", "-static-libgcc")
        if get_config("toolchain") == "pixi-clang" then
            -- -static-libstdc++ makes clang append a static -lc++, but not libc++abi;
            -- whole-archive it so link order can't drop it.
            add_ldflags("-Wl,--whole-archive," .. prefix .. "/lib/libc++abi.a,--no-whole-archive", "-lpthread")
        end
    elseif get_config("toolchain") == "pixi-clang" then
        add_ldflags("-Wl,-rpath," .. prefix .. "/lib") -- conda's GCC adds this rpath itself
    end
elseif is_plat("macosx") then
    local prefix = os.getenv("CONDA_PREFIX") or ""
    -- conda's libc++ (newer than the system's) as a dylib. Linked statically it clashed with the
    -- system libc++ that libSystem loads anyway (a bad free at exit, S7). `artifact` ships
    -- libc++.1.dylib (Apache-2.0 WITH LLVM-exception) in the app bundle's Frameworks and drops the env rpath.
    add_ldflags("-Wl,-rpath,@executable_path/../Frameworks", "-Wl,-rpath," .. prefix .. "/lib")
    -- libc++'s availability markers describe the system's libc++ (std::to_chars needs macOS 13.3);
    -- we always run conda's, so they don't apply.
    add_defines("_LIBCPP_DISABLE_AVAILABILITY")
end

-- Libraries never resolve from the host (pacman, apt, pkg-config): only xmake-repo builds.
-- Build tools are the exception; they come pinned from the Pixi env (pixi.toml).
add_requireconfs("**|python|cmake|ninja|meson|autoconf|automake|libtool|m4|bison|flex|gperf|nasm|pkg-config|pkgconf", {system = false})
add_requires("doctest 2.5.3") -- MIT; tests only
add_requires("libsdl3-wayland 3.4.16") -- zlib; xmake/repo: libsdl3 plus its Wayland build deps
-- xmake/repo: tracy plus libdl for glibc 2.28. It collects
-- in releasedbg; everywhere else its macros are empty and the library compiles to nothing.
-- Localhost only: no profile data on the network. Keep the version equal to the viewer's in
-- pixi.toml (feature.profiler): the protocol must match. Both variants are required in every
-- mode, so xmake-requires.lock always holds both instead of whichever mode was configured last.
-- XMake keys requirements by their string, so the two spell the one exact version differently.
add_requires("tracy-libdl v0.14.1", {alias = "tracy_on", configs = {tracy_enable = true, only_localhost = true}}) -- BSD-3-Clause; profiling builds only
add_requires("tracy-libdl 0.14.1", {alias = "tracy_off", configs = {tracy_enable = false, only_localhost = true}}) -- BSD-3-Clause; profiling builds only

target("project_n")
    set_kind("binary")
    add_files("src/**.cpp")
    add_includedirs("src")
    set_pcxxheader("src/pch.hpp")
    add_packages("libsdl3-wayland", is_mode("releasedbg") and "tracy_on" or "tracy_off")

includes("tests")
