#pragma once

// Fail loudly if a compiler silently falls back below C++23: MSVC 14.50 ignores an unknown
// /std switch with only a warning (D9002) and compiles as C++14.
#if defined(_MSVC_LANG)
#if _MSVC_LANG <= 202002L
#error "project_n requires C++23 (MSVC: /std:c++23preview)"
#endif
#elif __cplusplus <= 202002L
#error "project_n requires C++23"
#endif
