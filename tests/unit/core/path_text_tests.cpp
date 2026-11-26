#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/path_text.hpp"

#include <doctest/doctest.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

struct Sample {
    std::string_view why;
    std::u8string_view path;
    std::string_view text;
};

// The expected texts are byte escapes, so a wrong conversion can't agree with itself.
constexpr std::array<Sample, 11> SAMPLES{ {
    { .why = "empty", .path = u8"", .text = "" },
    { .why = "plain ASCII", .path = u8"a", .text = "a" },
    { .why = "a folder and a file", .path = u8"saves/slot.pns", .text = "saves/slot.pns" },
    { .why = "backslash kept as the path holds it", .path = u8"saves\\slot.pns", .text = "saves\\slot.pns" },
    { .why = "é: inside the Western code page", .path = u8"é", .text = "\xC3\xA9" },
    { .why = "项目: outside every Western code page", .path = u8"项目", .text = "\xE9\xA1\xB9\xE7\x9B\xAE" },
    { .why = "🎮: outside the BMP", .path = u8"🎮", .text = "\xF0\x9F\x8E\xAE" },
    { .why = "all three inside a folder",
      .path = u8"项目_é_🎮/settings.yaml",
      .text = "\xE9\xA1\xB9\xE7\x9B\xAE_\xC3\xA9_\xF0\x9F\x8E\xAE/settings.yaml" },
    { .why = "a space", .path = u8"my saves/a b.pns", .text = "my saves/a b.pns" },
    { .why = "a dotfile", .path = u8".hidden", .text = ".hidden" },
    { .why = "trailing separator", .path = u8"saves/", .text = "saves/" },
} };

std::filesystem::path PathOfSample(const Sample& sample) {
    return std::filesystem::path{ std::u8string{ sample.path } };
}

TEST_CASE("a path's text is its UTF-8 bytes") {
    u32 misses = 0;
    std::string missed;
    for (const Sample& sample : SAMPLES) {
        if (core::PathText(PathOfSample(sample)) != sample.text) {
            ++misses;
            missed += std::format("[{}] ", sample.why);
        }
    }
    CHECK_MESSAGE(misses == 0, missed);
}

TEST_CASE("a path read from its text is the same path") {
    u32 misses = 0;
    std::string missed;
    for (const Sample& sample : SAMPLES) {
        const std::filesystem::path path = PathOfSample(sample);
        // native(): path's == ignores the separator, which is exactly what a round trip must keep.
        if (core::PathOf(core::PathText(path)).native() != path.native()) {
            ++misses;
            missed += std::format("[{}] ", sample.why);
        }
    }
    CHECK_MESSAGE(misses == 0, missed);
}

#ifdef _WIN32
// The narrow Windows APIs (and Quill's file name) read text in this code page; the manifest makes it UTF-8.
TEST_CASE("the process's ANSI code page is UTF-8") {
    CHECK(GetACP() == CP_UTF8);
}
#endif

} // namespace
