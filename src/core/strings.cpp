#include "core/strings.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace core {

namespace {

constexpr std::string_view VALUE_PLACEHOLDER = "{value}";
constexpr std::string_view MOST_PLACEHOLDER = "{max}";
constexpr std::string_view MONTH_PLACEHOLDER = "{month}";
constexpr std::string_view DAY_PLACEHOLDER = "{day}";

// Writes `text` into `into` with `first` as plain decimal in place of each `firstName` and `second` in place of each
// `secondName`; a brace that starts neither stays.
template<typename First, typename Second>
void Fill(
    std::string& into,
    std::string_view text,
    std::string_view firstName,
    First first,
    std::string_view secondName,
    Second second
) {
    into.clear();
    while (!text.empty()) {
        const std::size_t brace = text.find('{');
        into.append(text.substr(0, brace));
        if (brace == std::string_view::npos) {
            break;
        }
        text.remove_prefix(brace);
        if (text.starts_with(firstName)) {
            std::format_to(std::back_inserter(into), "{}", first);
            text.remove_prefix(firstName.size());
        }
        else if (text.starts_with(secondName)) {
            std::format_to(std::back_inserter(into), "{}", second);
            text.remove_prefix(secondName.size());
        }
        else {
            into.push_back('{');
            text.remove_prefix(1);
        }
    }
}

} // namespace

void FillValue(std::string& into, std::string_view text, i64 value, i64 most) {
    Fill(into, text, VALUE_PLACEHOLDER, value, MOST_PLACEHOLDER, most);
}

void FillDate(std::string& into, std::string_view text, u64 month, u32 day) {
    Fill(into, text, MONTH_PLACEHOLDER, month, DAY_PLACEHOLDER, day);
}

} // namespace core
