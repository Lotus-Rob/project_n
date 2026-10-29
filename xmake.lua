set_project("project_n")
set_version("0.1.0")
set_xmakever("3.1.2")
set_policy("package.requires_lock", true)
add_rules("mode.debug", "mode.release", "mode.releasedbg")
add_repositories("project_n-repo xmake/repo")

option("werror", {default = false, description = "Treat warnings as errors (set by `project_n check`)"})
if has_config("werror") then
    set_warnings("all", "error")
else
    set_warnings("all")
end

if is_plat("windows") then
    -- MSVC 14.50 has no /std:c++23: it warns (D9002, not fatal even with /WX), ignores it and
    -- compiles as C++14. Its C++23 mode is /std:c++23preview; src/core/cxx23.hpp guards the rest.
    add_cxxflags("/std:c++23preview")
    -- Static C++ runtime, like Linux: players need no Visual C++ Redistributable.
    set_runtimes("MT")
else
    set_languages("c++23")
end

-- conda-forge compilers from the Pixi env; names are triplet-prefixed, so XMake's
-- built-in gcc/clang toolchains would silently pick the host compilers instead.
local function pixi_toolchain(name, cc, cxx, flags)
    toolchain(name)
        set_kind("standalone")
        set_toolset("cc", "x86_64-conda-linux-gnu-" .. cc)
        set_toolset("as", "x86_64-conda-linux-gnu-" .. cc)
        set_toolset("cxx", "x86_64-conda-linux-gnu-" .. cxx)
        set_toolset("ld", "x86_64-conda-linux-gnu-" .. cxx)
        set_toolset("sh", "x86_64-conda-linux-gnu-" .. cxx)
        set_toolset("ar", "x86_64-conda-linux-gnu-ar")
        set_toolset("strip", "x86_64-conda-linux-gnu-strip")
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
-- (baseline 2.28). Project-level flags, so packages built from source never inherit them.
if is_plat("linux") then
    add_ldflags("-static-libstdc++", "-static-libgcc")
    if get_config("toolchain") == "pixi-clang" then
        -- -static-libstdc++ makes clang append a static -lc++, but not libc++abi;
        -- whole-archive it so link order can't drop it.
        add_ldflags("-Wl,--whole-archive," .. (os.getenv("CONDA_PREFIX") or "") .. "/lib/libc++abi.a,--no-whole-archive", "-lpthread")
    end
end

-- Libraries never resolve from the host (pacman, apt, pkg-config): only xmake-repo builds.
-- Build tools are the exception; they come pinned from the Pixi env (pixi.toml).
add_requireconfs("**|python|cmake|ninja|meson|autoconf|automake|libtool|m4|bison|flex|gperf|nasm|pkg-config|pkgconf", {system = false})
add_requires("doctest 2.5.3")
add_requires("libsdl3-wayland 3.4.16") -- xmake/repo: libsdl3 plus its Wayland build deps

target("project_n")
    set_kind("binary")
    add_files("src/**.cpp")
    add_includedirs("src")
    add_packages("libsdl3-wayland")

includes("tests")
