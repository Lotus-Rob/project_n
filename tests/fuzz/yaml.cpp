// The data module's YAML readers (data::ParseGameData, data::ParseAbilities, data::ParseSettings, data::ParseManifest,
// data::ParseStrings, data::ParseEffects, data::ParseSeals, data::ParseWorldMap, data::ParseWorldLook, data::ParseNames): a
// data or settings file is edited by hand and the manifest may be damaged, so any text is refused with an error or read, never
// a crash or an exception escaping. Seeded with the shipped creatures, abilities, effects, seals, strings and names tables, a
// settings file, the manifest, a seals table past a cap, a small world map and a small look for it.
#include "../support/manifest.hpp" // relative: lint has no compile flags for the fuzz targets, which only build with ASan
#include "core/cxx23.hpp"          // IWYU pragma: keep
#include "data/abilities.hpp"
#include "data/effects.hpp"
#include "data/game_data.hpp"
#include "data/manifest.hpp"
#include "data/names.hpp"
#include "data/seals.hpp"
#include "data/settings.hpp"
#include "data/strings.hpp"
#include "data/world_look.hpp"
#include "data/world_map.hpp"

namespace {

// The `world` seed: the map the look is read against.
constexpr std::string_view SMALL_MAP = R"yaml(road_cost: 4
ford_cost: 30
tile_size: 256
kinds:
  - { symbol: "~", name: deep_water, cost: impassable }
  - { symbol: ".", name: meadow, cost: 10 }
grid: |
  ~~~~~
  ..~..
  .....
roads: [ [[0, 2], [4, 2]] ]
rivers: [ [[3, 1], [3, 2]] ]
fords: [ [3, 1] ]
profiles:
  - { key: on_foot, ticks_per_cost: 16 }
places:
  - { key: capital, kind: city, tile: [0, 2], name: place_capital, people: 100, start: true }
  - { key: wolf_pass, kind: danger, sort: mountain_pass, tile: [4, 1], name: place_wolf_pass }
)yaml";

} // namespace

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
    const std::expected<core::GameData, core::Error> gameData = data::ParseGameData(text, text, &warnings);
    const std::expected<core::Abilities, core::Error> abilities = data::ParseAbilities(text, &warnings);
    const std::expected<core::Effects, core::Error> effects = data::ParseEffects(text, &warnings);
    const std::expected<core::Settings, core::Error> settings = data::ParseSettings(text, &warnings);
    const std::expected<core::Manifest, core::Error> manifest = data::ParseManifest(text);
    const std::expected<core::Strings, core::Error> strings = data::ParseStrings(text, &warnings);
    const std::expected<core::Names, core::Error> names = data::ParseNames(text);
    // The seals name sprites, so they are read against the shipped manifest, loaded once and kept to the process's end.
    [[clang::no_destroy]] static const std::shared_ptr<const core::Manifest> shipped = test::TestManifest();
    const std::expected<core::Seals, core::Error> seals = data::ParseSeals(text, *shipped, &warnings);
    // The world map names its places by strings, so it is read against a table holding the seed's names.
    [[clang::no_destroy]] static const core::Strings placeNames = [] {
        core::Strings table;
        table.Add("place_capital", { { "Capital", "Capital" } });
        table.Add("place_wolf_pass", { { "Wolf Pass", "Wolf Pass" } });
        return table;
    }();
    const std::expected<core::WorldMap, core::Error> world = data::ParseWorldMap(text, placeNames, &warnings);
    // The look names the map's kinds and sorts, so it is read against the `world` seed's map, built once.
    [[clang::no_destroy]] static const core::WorldMap small = [] {
        std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(SMALL_MAP, placeNames);
        if (!map) {
            std::abort(); // the seed map is valid; every input after would test nothing
        }
        return std::move(*map);
    }();
    const std::expected<core::WorldLook, core::Error> look = data::ParseWorldLook(text, *shipped, small, &warnings);
    if ((!gameData && gameData.error().message.empty()) || (!abilities && abilities.error().message.empty()) ||
        (!effects && effects.error().message.empty()) || (!settings && settings.error().message.empty()) ||
        (!manifest && manifest.error().message.empty()) || (!strings && strings.error().message.empty()) ||
        (!names && names.error().message.empty()) || (!seals && seals.error().message.empty()) ||
        (!world && world.error().message.empty()) || (!look && look.error().message.empty())) {
        std::abort(); // a refusal always says why
    }
    return 0;
}
