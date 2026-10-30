#pragma once

// Precompiled header of the test targets (set_pcxxheader in tests/xmake.lua). Test files still
// include what they use, so they build without it.
// IWYU pragma: begin_keep
#include "core/cxx23.hpp"

#include <doctest/doctest.h>

#include <expected>
// IWYU pragma: end_keep
