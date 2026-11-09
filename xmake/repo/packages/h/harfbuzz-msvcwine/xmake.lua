-- xmake-repo's harfbuzz builds with meson, and two XMake gaps break it for windows-msvc from a Linux
-- host (msvc-wine), each silently: XMake's cross file names no archiver for Windows, so meson takes
-- $AR, which Pixi's activation sets to conda's GNU ar, and MSVC's linker rejects its archives
-- (LNK4003); and XMake's rename of meson's static `libX.a` to the `X.lib` MSVC links
-- (package.tools.meson, _fix_libname_on_windows) matches only backslash paths, so nothing is
-- renamed and XMake finds no library. This one archives with msvc-wine's lib.exe and renames with
-- `path` functions. Upstream candidate for XMake (package.tools.meson).
package("harfbuzz-msvcwine")
    set_base("harfbuzz")
    on_install(function (package)
        local crossmsvc = package:is_plat("windows") and not is_host("windows")
        local ar = os.getenv("AR")
        if crossmsvc then
            os.setenv("AR", path.join(path.directory(package:build_getenv("cc")), "lib.exe"))
        end
        try {
            function () package:base():script("install")(package) end,
            -- XMake's try swallows the error without a catch: re-raise it, or the failure surfaces later as a missing library.
            catch {
                function (errors) raise(errors) end
            },
            finally {
                function ()
                    if crossmsvc then
                        os.setenv("AR", ar or "")
                    end
                end
            }
        }
        if package:is_plat("windows") and not package:config("shared") then
            for _, lib in ipairs(os.files(path.join(package:installdir("lib"), "lib*.a"))) do
                os.mv(lib, path.join(path.directory(lib), path.basename(lib):sub(4) .. ".lib"))
            end
        end
    end)
