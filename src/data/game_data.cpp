#include "data/game_data.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "data/internal/yaml.hpp"

namespace data {

namespace {

using core::atlas::SpriteId;

std::unexpected<core::Error> Fail(const YAML::Node& node, std::string_view why) {
    return std::unexpected{ core::Error{ std::format("creatures line {}: {}", node.Mark().line + 1, why) } };
}

// shortcut: sprites by the atlas's file names; chunk 3's manifest names them (design doc 0002.3 §6).
std::optional<SpriteId> SpriteNamed(std::string_view name) {
    const std::string file = std::format("sprites/{}.png", name);
    for (std::size_t i = 0; i < core::atlas::SPRITES.size(); ++i) {
        if (file == core::atlas::SPRITES.at(i).file) {
            return static_cast<SpriteId>(i);
        }
    }
    return std::nullopt;
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

std::expected<core::CreatureData, core::Error> Creature(const YAML::Node& root, const char* name) {
    const YAML::Node node = root[name];
    if (!node || !node.IsMap()) {
        return Fail(node ? node : root, std::format("no creature {}", name));
    }
    const YAML::Node spriteNode = node["sprite"];
    if (!spriteNode) {
        return Fail(node, std::format("{} has no sprite", name));
    }
    const auto spriteName = spriteNode.as<std::string>();
    const std::optional<SpriteId> sprite = SpriteNamed(spriteName);
    if (!sprite) {
        return Fail(spriteNode, std::format("{}'s sprite {} isn't in the atlas", name, spriteName));
    }
    core::CreatureData creature{ .sprite = *sprite };
    const std::expected<f32, core::Error> walkSpeed = Number(node, name, "walk_speed");
    if (!walkSpeed) {
        return std::unexpected{ walkSpeed.error() };
    }
    creature.walkSpeed = *walkSpeed;
    if (const YAML::Node orbit = node["orbit"]) {
        const std::string orbitName = std::format("{}'s orbit", name);
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

std::expected<core::GameData, core::Error> ParseGameData(std::string_view creatures) {
    try {
        const YAML::Node root = YAML::Load(std::string{ creatures });
        if (!root.IsMap()) {
            return Fail(root, "not a map of creatures");
        }
        return Creature(root, "cultivator").and_then([&](const core::CreatureData& cultivator) {
            return Creature(root, "fox").transform([&](const core::CreatureData& fox) {
                return core::GameData{ .cultivator = cultivator, .fox = fox };
            });
        });
    }
    catch (const YAML::Exception& error) {
        return std::unexpected{ internal::ErrorOf("creatures", error) };
    }
}

std::expected<core::GameData, core::Error> LoadGameData(const std::filesystem::path& folder) {
    return internal::ReadText(folder / "creatures.yaml").and_then([](const std::string& text) { return ParseGameData(text); });
}

} // namespace data
