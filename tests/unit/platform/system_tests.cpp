#include "core/cxx23.hpp" // IWYU pragma: keep
#include "platform/system.hpp"

#include <doctest/doctest.h>

namespace {

bool IsPositiveNumber(std::string_view text) {
    return !text.empty() && text.front() != '0' &&
           std::ranges::all_of(text, [](char digit) { return digit >= '0' && digit <= '9'; });
}

TEST_CASE("the system summary names the OS, the CPU count and the memory") {
    const std::string summary = platform::SystemSummary();
    constexpr std::string_view CPUS = " logical CPUs, ";
    constexpr std::string_view RAM = " MiB RAM";
    REQUIRE(summary.ends_with(RAM));
    const std::size_t cpusAt = summary.rfind(CPUS);
    REQUIRE(cpusAt != std::string::npos);
    const std::size_t commaBeforeCpus = summary.rfind(", ", cpusAt - 1);
    REQUIRE(commaBeforeCpus != std::string::npos);
    CHECK(commaBeforeCpus > 0);
    const std::string_view view = summary;
    CHECK(IsPositiveNumber(view.substr(commaBeforeCpus + 2, cpusAt - commaBeforeCpus - 2)));
    const std::size_t ramStart = cpusAt + CPUS.size();
    CHECK(IsPositiveNumber(view.substr(ramStart, view.size() - RAM.size() - ramStart)));
}

} // namespace
