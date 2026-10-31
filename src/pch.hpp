#pragma once

/// @file
/// The game target's precompiled header (set_pcxxheader in xmake.lua): headers that are slow to
/// parse. Standard library and our own headers only, like every header in src/: it reaches every
/// source file. Sources still include what they use, so they build without it.
// IWYU pragma: begin_keep
#include "core/cxx23.hpp"

#include <exception>
#include <print>
// IWYU pragma: end_keep
