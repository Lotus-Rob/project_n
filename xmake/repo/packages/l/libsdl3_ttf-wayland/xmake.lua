-- xmake-repo's libsdl3_ttf depends on its own libsdl3, a second SDL beside our libsdl3-wayland (which
-- links it with Wayland). This one is the same package on our SDL: its load script with the dependency
-- swapped, as a base load script can't take a dependency back. xmake.lua pins the nested SDL to the
-- game's version. Upstream candidate for xmake-repo: let the SDL package be chosen.
package("libsdl3_ttf-wayland")
    set_base("libsdl3_ttf")
    on_load(function (package)
        package:add("deps", "libsdl3-wayland", {configs = {shared = package:config("shared")}})
        if package:config("harfbuzz") then
            package:add("deps", "harfbuzz-msvcwine") -- xmake/repo: harfbuzz, linkable by MSVC from a Linux host
        end
        if package:config("plutosvg") then
            package:add("deps", "plutosvg", "plutovg")
        end
    end)
