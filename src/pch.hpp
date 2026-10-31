#pragma once

/// @file
/// The game target's precompiled header (set_pcxxheader in xmake.lua): headers that are slow to
/// parse. Sources still include what they use, so they build without it.
// IWYU pragma: begin_keep
#include "core/cxx23.hpp"

#include <SDL3/SDL.h>

#include <exception>
#include <print>
// IWYU pragma: end_keep
