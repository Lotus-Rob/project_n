#include "data/game_data.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/manifest.hpp"
#include "data/abilities.hpp"
#include "data/internal/yaml.hpp"

namespace data {

namespace {

using core::atlas::SpriteId;

// The most health a creature has: below 2^53, which an f64 holds exactly.
constexpr i64 MAX_HEALTH = 9'007'199'254'740'992;

std::unexpected<core::Error> Fail(const YAML::Node& node, std::string_view why) {
    return std::unexpected{ core::Error{ std::format("creatures line {}: {}", node.Mark().line + 1, why) } };
}

// `map`'s finite number at `key`, which `name` names in errors; yaml-cpp throws on one that isn't a number.
std::expected<f32, core::Error> Number(const YAML::Node& map, std::string_view name, const char* key) {
    const YAML::Node node = map[key];
    if (!node) {
        return Fail(map, std::format("{} has no {}", name, key));
    }
    const auto value = node.as<f32>();
    if (!std::isfinite(value)) {
        return Fail(node, std::format("{}'s {} isn't a finite number", name, key));
    }
    return value;
}

std::expected<core::CreatureData, core::Error> Creature(
    const YAML::Node& root,
    const char* name,
    std::vector<std::string>* warnings
) {
    const YAML::Node node = root[name];
    if (!node || !node.IsMap()) {
        return Fail(node ? node : root, std::format("no creature {}", name));
    }
    internal::NoteUnknownKeys("creatures", node, name, "key", { "sprite", "health", "walk_speed", "orbit" }, warnings);
    const YAML::Node spriteNode = node["sprite"];
    if (!spriteNode) {
        return Fail(node, std::format("{} has no sprite", name));
    }
    const auto spriteName = spriteNode.as<std::string>();
    const std::optional<SpriteId> sprite = core::SpriteNamed(spriteName);
    if (!sprite) {
        return Fail(spriteNode, std::format("{}'s sprite {} isn't in the atlas", name, spriteName));
    }
    core::CreatureData creature{ .sprite = *sprite };
    const std::expected<f32, core::Error> walkSpeed = Number(node, name, "walk_speed");
    if (!walkSpeed) {
        return std::unexpected{ walkSpeed.error() };
    }
    creature.walkSpeed = *walkSpeed;
    const std::expected<i64, core::Error> health = internal::WholeNumber("creatures", node, name, "health", true, MAX_HEALTH);
    if (!health) {
        return std::unexpected{ health.error() };
    }
    if (*health < 1) {
        return Fail(node["health"], std::format("{}'s health is below 1", name)); // it would be born dying
    }
    creature.health = *health;
    if (const YAML::Node orbit = node["orbit"]) {
        const std::string orbitName = std::format("{}'s orbit", name);
        internal::NoteUnknownKeys("creatures", orbit, orbitName, "key", { "radius", "wobble", "speed", "around" }, warnings);
        if (const YAML::Node around = orbit["around"]) {
            const auto centre = around.as<std::string>();
            if (centre != "player" && centre != "start") {
                return Fail(around, std::format("{} goes around {}, not the player or the start", orbitName, centre));
            }
            creature.orbitsPlayer = centre == "player";
        }
        for (const auto& [key, value] : { std::pair{ "radius", &creature.orbitRadius },
                                          std::pair{ "wobble", &creature.orbitWobble },
                                          std::pair{ "speed", &creature.orbitSpeed } }) {
            const std::expected<f32, core::Error> number = Number(orbit, orbitName, key);
            if (!number) {
                return std::unexpected{ number.error() };
            }
            *value = *number;
        }
    }
    return creature;
}

} // namespace

std::expected<core::GameData, core::Error> ParseGameData(
    std::string_view creatures,
    std::string_view abilities,
    std::vector<std::string>* warnings
) {
    try {
        const YAML::Node root = YAML::Load(std::string{ creatures });
        if (!root.IsMap()) {
            return Fail(root, "not a map of creatures");
        }
        internal::NoteUnknownKeys("creatures", root, "", "creature", { "cultivator", "fox" }, warnings);
        const std::expected<core::CreatureData, core::Error> cultivator = Creature(root, "cultivator", warnings);
        if (!cultivator) {
            return std::unexpected{ cultivator.error() };
        }
        const std::expected<core::CreatureData, core::Error> fox = Creature(root, "fox", warnings);
        if (!fox) {
            return std::unexpected{ fox.error() };
        }
        return ParseAbilities(abilities, warnings).transform([&](const core::Abilities& table) {
            return core::GameData{ .cultivator = *cultivator, .fox = *fox, .abilities = table };
        });
    }
    catch (const YAML::Exception& error) {
        return std::unexpected{ internal::ErrorOf("creatures", error) };
    }
}

std::expected<core::GameData, core::Error> LoadGameData(const std::filesystem::path& folder) {
    std::vector<std::string> warnings;
    std::expected<core::GameData, core::Error> data =
        internal::ReadText(folder / "creatures.yaml").and_then([&](const std::string& creatures) {
            return internal::ReadText(folder / "abilities.yaml").and_then([&](const std::string& abilities) {
                return ParseGameData(creatures, abilities, &warnings);
            });
        });
    for (const std::string& warning : warnings) {
        PN_LOG_WARNING("{}", warning);
    }
    return data;
}

} // namespace data
