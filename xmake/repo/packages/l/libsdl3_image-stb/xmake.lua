-- xmake-repo's libsdl3_image links its own libsdl3 (a second SDL beside libsdl3-wayland) and, by
-- default, looks for libpng, libtiff, libwebp and libavif wherever CMake finds them, dlopens them at
-- runtime, and silently drops a format whose library is missing. This one builds against
-- libsdl3-wayland with PNG only, decoded by the stb_image it bundles: no codec library from the
-- host or the player's system, and the same decoder on every platform (no ImageIO or WIC colour
-- management). Strict, so a dependency it can't find fails the build. Upstream candidate for
-- xmake-repo (the libsdl3 dependency follows the requiring project's choice of SDL package).
package("libsdl3_image-stb")
    set_base("libsdl3_image")
    on_load(function (package)
        package:add("deps", "libsdl3-wayland", {configs = {shared = package:config("shared")}})
    end)
    on_install(function (package)
        local configs = {"-DSDLIMAGE_SAMPLES=OFF", "-DSDLIMAGE_TESTS=OFF", "-DSDLIMAGE_VENDORED=OFF",
                         "-DSDLIMAGE_STRICT=ON", "-DSDLIMAGE_DEPS_SHARED=OFF",
                         "-DSDLIMAGE_BACKEND_STB=ON", "-DSDLIMAGE_BACKEND_IMAGEIO=OFF", "-DSDLIMAGE_BACKEND_WIC=OFF",
                         "-DSDLIMAGE_PNG=ON", "-DSDLIMAGE_PNG_LIBPNG=OFF", "-DSDLIMAGE_PNG_SAVE=OFF"}
        for _, format in ipairs({"ANI", "AVIF", "BMP", "GIF", "JPG", "JXL", "LBM", "PCX", "PNM", "QOI", "SVG",
                                 "TGA", "TIF", "WEBP", "XCF", "XPM", "XV"}) do
            table.insert(configs, "-DSDLIMAGE_" .. format .. "=OFF")
        end
        table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:debug() and "Debug" or "Release"))
        table.insert(configs, "-DBUILD_SHARED_LIBS=" .. (package:config("shared") and "ON" or "OFF"))
        import("package.tools.cmake").install(package, configs)
    end)
