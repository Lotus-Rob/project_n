-- xmake-repo's libsdl3 omits two of SDL's Wayland build requirements (egl.pc, xkbcommon);
-- without them SDL silently drops Wayland. Both are build-time only: SDL dlopens the
-- system's libraries at runtime. Upstream candidate for xmake-repo.
package("libsdl3-wayland")
    set_base("libsdl3")
    on_load(function (package)
        package:base():script("load")(package)
        if package:is_plat("linux") and package:config("wayland") then
            package:add("deps", "libxkbcommon-fixed", {private = true, configs = {shared = true, x11 = false, wayland = false}})
            package:add("deps", "libglvnd", {private = true})
        end
    end)
