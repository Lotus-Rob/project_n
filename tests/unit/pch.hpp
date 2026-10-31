#pragma once

// Precompiled header of the test and fuzz targets (set_pcxxheader in tests/xmake.lua): every
// standard header their sources use, src/core/log.cpp's included. `.cpp` files don't include standard headers themselves; they
// rely on this one (`lint` enforces it).
// IWYU pragma: begin_keep
#include "core/cxx23.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>
// IWYU pragma: end_keep
