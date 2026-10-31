#pragma once

/// @file
/// The game target's precompiled header (set_pcxxheader in xmake.lua): every standard header the
/// game's sources use. `.cpp` files don't include standard headers themselves; they rely on this
/// one (`lint` enforces it). Headers still include everything they use. Standard library and our
/// own headers only, like every header in src/: it reaches every source file.
// IWYU pragma: begin_keep
#include "core/cxx23.hpp"

#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
// IWYU pragma: end_keep
