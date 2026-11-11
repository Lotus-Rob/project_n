#include "core/strings.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace core {

namespace {

constexpr std::string_view VALUE_PLACEHOLDER = "{value}";
constexpr std::string_view MOST_PLACEHOLDER = "{max}";

} // namespace

void FillValue(std::string& into, std::string_view text, i64 value, i64 most) {
    into.clear();
    while (!text.empty()) {
        const std::size_t brace = text.find('{');
        into.append(text.substr(0, brace));
        if (brace == std::string_view::npos) {
            break;
        }
        text.remove_prefix(brace);
        if (text.starts_with(VALUE_PLACEHOLDER)) {
            std::format_to(std::back_inserter(into), "{}", value);
            text.remove_prefix(VALUE_PLACEHOLDER.size());
        }
        else if (text.starts_with(MOST_PLACEHOLDER)) {
            std::format_to(std::back_inserter(into), "{}", most);
            text.remove_prefix(MOST_PLACEHOLDER.size());
        }
        else {
            into.push_back('{');
            text.remove_prefix(1);
        }
    }
}

} // namespace core
