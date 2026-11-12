#include "data/manifest.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "data/internal/yaml.hpp"

namespace data {

namespace {

using core::atlas::SPRITE_NAMES;
using core::atlas::SpriteId;

constexpr u32 MIN_MIP_LEVELS = 2;  // the gutter is at least 2^(mips - 1) texels
constexpr u32 MAX_MIP_LEVELS = 15; // a 16384-texel page's
constexpr std::string_view STALE = "the atlas is stale: run `./dev atlas`";

std::unexpected<core::Error> Refuse(const YAML::Node& node, std::string_view why) {
    return std::unexpected{ core::Error{ std::format("manifest line {}: {}", node.Mark().line + 1, why) } };
}

// `map`'s field `key`, which must be there; yaml-cpp throws, with the line, when it isn't a T.
template<typename T>
std::expected<T, core::Error> Field(const YAML::Node& map, const char* key) {
    const YAML::Node node = map[key];
    if (!node) {
        return Refuse(map, std::format("no {}", key));
    }
    return node.as<T>();
}

// `map`'s list of `count` numbers at `key`.
std::expected<std::vector<u16>, core::Error> Numbers(const YAML::Node& map, const char* key, std::size_t count) {
    const YAML::Node node = map[key];
    if (!node || !node.IsSequence() || node.size() != count) {
        return Refuse(node ? node : map, std::format("{} must be {} numbers", key, count));
    }
    std::vector<u16> numbers;
    for (const YAML::Node& number : node) {
        numbers.push_back(number.as<u16>());
    }
    return numbers;
}

std::expected<core::SpriteInfo, core::Error> Sprite(const YAML::Node& node, std::size_t index, const core::Manifest& manifest) {
    if (!node.IsMap()) {
        return Refuse(node, "a sprite must be a map");
    }
    const std::expected<std::string, core::Error> name = Field<std::string>(node, "name");
    if (!name) {
        return std::unexpected{ name.error() };
    }
    if (index >= SPRITE_NAMES.size() || *name != SPRITE_NAMES.at(index)) {
        return Refuse(
            node,
            std::format(
                "sprite {} is {}, but the game expects {}: {}",
                index,
                *name,
                index < SPRITE_NAMES.size() ? SPRITE_NAMES.at(index) : "none",
                STALE
            )
        );
    }
    const std::expected<std::string, core::Error> file = Field<std::string>(node, "file");
    const std::expected<u16, core::Error> page = Field<u16>(node, "page");
    const std::expected<std::vector<u16>, core::Error> region = Numbers(node, "region", 4);
    const std::expected<std::vector<u16>, core::Error> foot = Numbers(node, "foot", 2);
    if (!file) {
        return std::unexpected{ file.error() };
    }
    if (!page) {
        return std::unexpected{ page.error() };
    }
    if (!region) {
        return std::unexpected{ region.error() };
    }
    if (!foot) {
        return std::unexpected{ foot.error() };
    }
    core::SpriteInfo sprite{
        .file = *file,
        .page = *page,
        .x = region->at(0),
        .y = region->at(1),
        .width = region->at(2),
        .height = region->at(3),
        .footX = foot->at(0),
        .footY = foot->at(1),
    };
    if (sprite.page >= manifest.pages) {
        return Refuse(node, std::format("{} is on page {}, past the last", *name, sprite.page));
    }
    // The block uploaded around the region: the sprite's half of the gutter (gfx::BlockOf).
    const u32 edge = manifest.Edge();
    if (sprite.x < edge || sprite.y < edge || u32{ sprite.x } + sprite.width + edge > manifest.pageSize ||
        u32{ sprite.y } + sprite.height + edge > manifest.pageSize) {
        return Refuse(
            node, std::format("{}'s region and its {}-texel edge are outside its {}-texel page", *name, edge, manifest.pageSize)
        );
    }
    if (sprite.footX > sprite.width || sprite.footY > sprite.height) {
        return Refuse(node, std::format("{}'s foot is outside its image", *name));
    }
    return sprite;
}

std::expected<core::SceneInfo, core::Error> Scene(const YAML::Node& node) {
    if (!node.IsMap()) {
        return Refuse(node, "a scene must be a map");
    }
    const std::expected<std::string, core::Error> name = Field<std::string>(node, "name");
    if (!name) {
        return std::unexpected{ name.error() };
    }
    const YAML::Node sprites = node["sprites"];
    if (!sprites || !sprites.IsSequence()) {
        return Refuse(sprites ? sprites : node, std::format("scene {} must list its sprites", *name));
    }
    core::SceneInfo scene{ .name = *name, .sprites = {} };
    for (const YAML::Node& sprite : sprites) {
        const auto spriteName = sprite.as<std::string>();
        const std::optional<SpriteId> found = core::SpriteNamed(spriteName);
        if (!found) {
            return Refuse(sprite, std::format("scene {} draws {}, which isn't a sprite", *name, spriteName));
        }
        if (std::ranges::contains(scene.sprites, *found)) {
            return Refuse(sprite, std::format("scene {} lists {} twice", *name, spriteName));
        }
        scene.sprites.push_back(*found);
    }
    return scene;
}

std::expected<core::Manifest, core::Error> Parse(const YAML::Node& root) {
    if (!root.IsMap()) {
        return Refuse(root, "not a map");
    }
    core::Manifest manifest;
    for (const auto& [key, value] : { std::pair{ "page_size", &manifest.pageSize },
                                      std::pair{ "pages", &manifest.pages },
                                      std::pair{ "mip_levels", &manifest.mipLevels } }) {
        const std::expected<u32, core::Error> number = Field<u32>(root, key);
        if (!number) {
            return std::unexpected{ number.error() };
        }
        *value = *number;
    }
    if (manifest.mipLevels < MIN_MIP_LEVELS || manifest.mipLevels > MAX_MIP_LEVELS) {
        return Refuse(
            root["mip_levels"], std::format("{} mip levels, not {} to {}", manifest.mipLevels, MIN_MIP_LEVELS, MAX_MIP_LEVELS)
        );
    }
    const std::expected<u32, core::Error> gutter = Field<u32>(root, "gutter");
    if (!gutter) {
        return std::unexpected{ gutter.error() };
    }
    manifest.gutter = *gutter;
    // Two neighbours' fields reach FIELD_REACH_TEXELS each into it, and the last mip's texels span 2^(mips - 1).
    const u32 leastGutter = std::max(2 * core::FIELD_REACH_TEXELS, u32{ 1 } << (manifest.mipLevels - 1));
    if (manifest.gutter % 2 != 0) {
        return Refuse(root["gutter"], std::format("a {}-texel gutter, which is odd", manifest.gutter));
    }
    if (manifest.gutter < leastGutter) {
        return Refuse(root["gutter"], std::format("a {}-texel gutter, under {}", manifest.gutter, leastGutter));
    }
    const YAML::Node sprites = root["sprites"];
    if (!sprites || !sprites.IsSequence()) {
        return Refuse(sprites ? sprites : root, "no list of sprites");
    }
    for (std::size_t i = 0; i < sprites.size(); ++i) {
        std::expected<core::SpriteInfo, core::Error> sprite = Sprite(sprites[i], i, manifest);
        if (!sprite) {
            return std::unexpected{ sprite.error() };
        }
        manifest.sprites.push_back(std::move(*sprite));
    }
    if (manifest.sprites.size() != SPRITE_NAMES.size()) {
        return Refuse(
            sprites, std::format("{} sprites, but the game has {}: {}", manifest.sprites.size(), SPRITE_NAMES.size(), STALE)
        );
    }
    const YAML::Node scenes = root["scenes"];
    if (!scenes || !scenes.IsSequence()) {
        return Refuse(scenes ? scenes : root, "no list of scenes");
    }
    for (const YAML::Node& node : scenes) {
        std::expected<core::SceneInfo, core::Error> scene = Scene(node);
        if (!scene) {
            return std::unexpected{ scene.error() };
        }
        manifest.scenes.push_back(std::move(*scene));
    }
    return manifest;
}

} // namespace

std::expected<core::Manifest, core::Error> ParseManifest(std::string_view text) {
    try {
        return Parse(YAML::Load(std::string{ text }));
    }
    catch (const YAML::Exception& error) {
        return std::unexpected{ internal::ErrorOf("manifest", error) };
    }
}

std::expected<core::Manifest, core::Error> LoadManifest(const std::filesystem::path& path) {
    return internal::ReadText(path).and_then([](const std::string& text) { return ParseManifest(text); });
}

} // namespace data
