#pragma once

/// @file
/// @ingroup core
/// Stops the build when the compiler isn't in C++23 mode. It declares nothing; include it with
/// `// IWYU pragma: keep`.

// Fail loudly, not silently: MSVC 14.50 ignores an unknown /std switch with only a warning (D9002)
// and compiles as C++14.
#ifdef _MSVC_LANG
#if _MSVC_LANG <= 202002L
#error "project_n requires C++23 (MSVC: /std:c++23preview)"
#endif
#elif __cplusplus <= 202002L
#error "project_n requires C++23"
#endif
