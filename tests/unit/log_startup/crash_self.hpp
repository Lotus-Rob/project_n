#pragma once

#include <filesystem>

/// The path this test binary was started with, which its cases run again as a child that dies.
[[nodiscard]] std::filesystem::path SelfPath();
