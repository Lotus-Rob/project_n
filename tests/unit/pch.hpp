#pragma once

// Precompiled header of the test and fuzz targets (set_pcxxheader in tests/xmake.lua):
// core/types.hpp and every standard header the test and fuzz sources use (the src/ modules
// they link use src/pch.hpp). `.cpp` files don't include standard headers themselves; they rely
// on this one (`lint` enforces it).
// IWYU pragma: begin_keep
#include "core/cxx23.hpp"
#include "core/types.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <cstddef>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
// IWYU pragma: end_keep
