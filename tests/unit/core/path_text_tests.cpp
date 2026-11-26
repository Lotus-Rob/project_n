#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/path_text.hpp"

#include <doctest/doctest.h>

namespace {

struct Sample {
    std::u8string_view path;
    std::string_view text;
};
// Each text is spelled in byte escapes, so a wrong conversion can't agree with itself.
// Each text is spelled in escapes so the expectation doesn't depend on the source file's encoding.
constexpr std::array<Sample, 10> SAMPLES{ {
    { .path = u8"", .text = "" },
    { .path = u8"a", .text = "a" },
    { .path = u8"saves/slot.pns", .text = "saves/slot.pns" },
    { .path = u8"é", .text = "\xC3\xA9" },
    { .path = u8"项目", .text = "\xE9\xA1\xB9\xE7\x9B\xAE" },
    { .path = u8"\U0001F3AE", .text = "\xF0\x9F\x8E\xAE" },
    { .path = u8"项目_é_\U0001F3AE/settings.yaml", .text = "\xE9\xA1\xB9\xE7\x9B\xAE_\xC3\xA9_\xF0\x9F\x8E\xAE/settings.yaml" },
    { .path = u8"my saves/a b.pns", .text = "my saves/a b.pns" },
    { .path = u8".hidden", .text = ".hidden" },
    { .path = u8"saves/", .text = "saves/" },
} };

TEST_CASE("a path's text is its UTF-8 bytes") {
    u32 misses = 0;
    for (const Sample& sample : SAMPLES) {
        if (core::PathText(std::filesystem::path{ std::u8string{ sample.path } }) != std::string{ sample.text }) {
            ++misses;
        }
    }
    CHECK(misses == 0);
}

TEST_CASE("a path read from its text is the same path") {
    u32 misses = 0;
    for (const Sample& sample : SAMPLES) {
        const std::filesystem::path path{ std::u8string{ sample.path } };
        if (core::PathOf(core::PathText(path)) != path) {
            ++misses;
        }
    }
    CHECK(misses == 0);
}

} // namespace
