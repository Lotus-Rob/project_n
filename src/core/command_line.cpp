#include "core/command_line.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/tick_clock.hpp"

namespace core {

std::expected<u64, Error> SecondsToNs(std::string_view text) {
    u32 seconds = 0;
    const char* last = std::to_address(text.end());
    const auto [end, error] = std::from_chars(std::to_address(text.begin()), last, seconds);
    if (error != std::errc{} || end != last || seconds == 0) {
        return std::unexpected(Error{ std::format("'{}' isn't a whole number of seconds above 0", text) });
    }
    return u64{ seconds } * NS_PER_SECOND;
}

} // namespace core
