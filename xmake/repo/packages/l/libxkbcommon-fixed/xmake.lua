-- xmake-repo's libxkbcommon only declares libxml2 (needed by xkbregistry) when x11 is on.
-- Upstream candidate for xmake-repo.
package("libxkbcommon-fixed")
    set_base("libxkbcommon")
    on_load(function (package)
        package:base():script("load")(package)
        package:add("deps", "libxml2")
    end)
