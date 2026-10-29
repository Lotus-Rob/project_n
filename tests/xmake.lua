-- One test executable per area (tests/unit/<area>/), registered with `xmake test`.
target("core_tests")
    set_kind("binary")
    add_files("unit/core/*.cpp")
    add_includedirs("$(projectdir)/src")
    add_packages("doctest")
    add_tests("default")
