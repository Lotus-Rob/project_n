// The data module's YAML readers (data::ParseGameData, data::ParseSettings): a data or settings file is
// edited by hand, so any text is refused with an error or read, never a crash or an exception escaping.
// Seeded with the shipped creatures table and a settings file.
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "data/game_data.hpp"
#include "data/settings.hpp"

// Declared first: libFuzzer ships no header for it, and -Wmissing-prototypes requires one.
extern "C" i32 LLVMFuzzerTestOneInput(const u8* bytes, std::size_t size);

extern "C" i32 LLVMFuzzerTestOneInput(const u8* bytes, std::size_t size) {
#pragma clang diagnostic push
// libFuzzer hands its input over as a pointer and a size: nothing can check them.
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
    const std::span<const u8> input{ bytes, size };
#pragma clang diagnostic pop
    std::string text(size, '\0');
    std::ranges::transform(input, text.begin(), [](u8 byte) { return static_cast<char>(byte); });
    std::vector<std::string> warnings; // collected, so the unknown-key path runs too
    const std::expected<core::GameData, core::Error> gameData = data::ParseGameData(text, &warnings);
    const std::expected<core::Settings, core::Error> settings = data::ParseSettings(text, &warnings);
    if ((!gameData && gameData.error().message.empty()) || (!settings && settings.error().message.empty())) {
        std::abort(); // a refusal always says why
    }
    return 0;
}
