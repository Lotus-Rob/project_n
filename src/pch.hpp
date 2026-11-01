#pragma once

/// @file
/// The game target's precompiled header (set_pcxxheader in xmake.lua): core/types.hpp and every
/// standard header the game's sources use. `.cpp` files don't include standard headers or
/// core/types.hpp themselves; they rely on this one (`lint` enforces the standard headers).
/// Headers still include everything they use. Standard library and our own headers only, like
/// every header in src/: it reaches every source file.
// IWYU pragma: begin_keep
#include "core/cxx23.hpp"
#include "core/types.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cfenv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <numeric>
#include <optional>
#include <source_location>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
// IWYU pragma: end_keep
