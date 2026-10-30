set_project("project_n")
set_version("0.1.0")
set_xmakever("3.1.2")
set_policy("package.requires_lock", true)
add_rules("mode.debug", "mode.release", "mode.releasedbg")
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

-- The highest warning level on every compiler, always as errors. Packages are unaffected:
-- they're built separately, and their headers are system/external includes for our code.
set_warnings("everything", "pedantic", "error")

if is_plat("windows") then
    -- MSVC 14.50 has no /std:c++23: it warns (D9002, not fatal even with /WX), ignores it and
    -- compiles as C++14. Its C++23 mode is /std:c++23preview; src/core/cxx23.hpp guards the rest.
    add_cxxflags("/std:c++23preview")
    -- Static C++ runtime, like Linux: players need no Visual C++ Redistributable.
    set_runtimes("MT")
    -- Library headers (<...>) are external: no warnings from code we don't own. XMake's own
    -- /external:I paths don't match under msvc-wine, so classify by include style instead.
    -- Forced: XMake probes each flag alone, and cl rejects /external:anglebrackets without
    -- /external:W, so the probe silently dropped it on native Windows.
    add_cxflags("/external:anglebrackets", "/external:W0", {force = true})
    -- /Wall's codegen notes rather than code issues: padding (4820), inlining decisions (4710,
    -- 4711), removed unused inlines (4514), Spectre mitigation points (5045).
    add_cxflags("/wd4820", "/wd4710", "/wd4711", "/wd4514", "/wd5045")
else
    set_languages("c++23")
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
    tc:add("ldflags", "-stdlib=libc++", "-L" .. prefix .. "/lib")
    tc:add("shflags", "-stdlib=libc++", "-L" .. prefix .. "/lib")
end)

-- Our executables link the C++ runtime and libgcc statically so artefacts need only glibc
-- (baseline 2.28; on macOS, only the system libraries). Project-level flags, so packages built from source never inherit them.
-- Sanitizer builds (never shipped) link it dynamically instead: sanitizer runtimes replace parts
-- of it (operator new, __cxa_guard_*), and a whole-archived static libc++abi collides with TSan.
local sanitized = get_config("sanitizer") and get_config("sanitizer") ~= "none"
if is_plat("linux") then
    local prefix = os.getenv("CONDA_PREFIX") or ""
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
    -- libc++'s availability markers describe the system's libc++ (std::to_chars needs macOS 13.3);
    -- we always run conda's, statically or (sanitized) from the env, so they don't apply.
    add_defines("_LIBCPP_DISABLE_AVAILABILITY")
    if not sanitized then
        -- conda's libc++ (newer than the system's), statically; libc++abi stays macOS's own.
        add_ldflags("-nostdlib++", prefix .. "/lib/libc++.a", "-lc++abi")
    else
        add_ldflags("-Wl,-rpath," .. prefix .. "/lib")
    end
end

-- Libraries never resolve from the host (pacman, apt, pkg-config): only xmake-repo builds.
-- Build tools are the exception; they come pinned from the Pixi env (pixi.toml).
add_requireconfs("**|python|cmake|ninja|meson|autoconf|automake|libtool|m4|bison|flex|gperf|nasm|pkg-config|pkgconf", {system = false})
add_requires("doctest 2.5.3") -- MIT; tests only
add_requires("libsdl3-wayland 3.4.16") -- zlib; xmake/repo: libsdl3 plus its Wayland build deps

target("project_n")
    set_kind("binary")
    add_files("src/**.cpp")
    add_includedirs("src")
    add_packages("libsdl3-wayland")

includes("tests")
