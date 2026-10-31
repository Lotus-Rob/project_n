-- xmake-repo's tracy links only pthread on Linux, but its client calls dladdr, which glibc keeps
-- in libdl before 2.34; with the 2.28 sysroot the link fails. Upstream candidate for xmake-repo.
package("tracy-libdl")
    set_base("tracy")
    on_load(function (package)
        package:base():script("load")(package)
        if package:is_plat("linux") then
            package:add("syslinks", "pthread", "dl") -- the base's own pthread doesn't carry over set_base
        end
    end)
