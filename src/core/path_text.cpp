#include "core/path_text.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace core {

std::string PathText(const std::filesystem::path& path) {
    const std::u8string utf8 = path.u8string();
    return { utf8.begin(), utf8.end() };
}

std::filesystem::path PathOf(std::string_view utf8) {
    return std::filesystem::path{ std::u8string(utf8.begin(), utf8.end()) };
}

} // namespace core
