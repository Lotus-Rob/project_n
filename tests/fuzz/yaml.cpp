// The data module's YAML readers (data::ParseGameData, data::ParseSettings, data::ParseManifest, data::ParseStrings): a data or
// settings file is edited by hand and the manifest may be damaged, so any text is refused with an error or read,
// never a crash or an exception escaping. Seeded with the shipped creatures and strings tables, a settings file and the
// manifest.
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "data/game_data.hpp"
#include "data/manifest.hpp"
#include "data/settings.hpp"
#include "data/strings.hpp"

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
    const std::expected<core::Manifest, core::Error> manifest = data::ParseManifest(text);
    const std::expected<core::Strings, core::Error> strings = data::ParseStrings(text, &warnings);
    if ((!gameData && gameData.error().message.empty()) || (!settings && settings.error().message.empty()) ||
        (!manifest && manifest.error().message.empty()) || (!strings && strings.error().message.empty())) {
        std::abort(); // a refusal always says why
    }
    return 0;
}
