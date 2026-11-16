#include "data/world_look.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "data/internal/yaml.hpp"

namespace data {

namespace {

constexpr std::string_view LOOK_READER = "world_look";

using Failure = std::unexpected<core::Error>;

Failure RefuseLook(const YAML::Node& node, std::string_view why) {
    return Failure{ internal::RefusalAt(LOOK_READER, node, why) };
}

/// The sprites a look may name: those of the manifest, and among them those the scene showing the map loads.
struct Atlas {
    const core::Manifest* manifest; // never null
    std::span<const core::atlas::SpriteId> resident;
};

/// The sprite `node` names, which must be in the atlas, in the manifest and among the world scene's; `owner` names it in
/// refusals. The renderer would draw a sprite outside the world scene's from a page that isn't loaded.
std::expected<core::atlas::SpriteId, core::Error> LookSpriteAt(
    const YAML::Node& node,
    std::string_view owner,
    const Atlas& atlas
) {
    const std::expected<core::atlas::SpriteId, core::Error> sprite =
        internal::ReadSprite(LOOK_READER, node, owner, *atlas.manifest);
    if (!sprite) {
        return std::unexpected{ sprite.error() };
    }
    if (!std::ranges::contains(atlas.resident, *sprite)) {
        return RefuseLook(
            node,
            std::format("{} names the sprite \"{}\", which the {} scene doesn't load", owner, node.Scalar(), WORLD_LOOK_SCENE)
        );
    }
    return *sprite;
}

/// The sprites of the list `node` holds, which `owner` names in refusals: `count` of them, or any number from 1 when
/// `count` is 0.
std::expected<std::vector<core::atlas::SpriteId>, core::Error> SpriteList(
    const YAML::Node& node,
    std::string_view owner,
    const Atlas& atlas,
    std::size_t count
) {
    if (!node || !node.IsSequence()) {
        return RefuseLook(node, std::format("{} isn't a list of sprites", owner));
    }
    if (count != 0 && node.size() != count) {
        return RefuseLook(node, std::format("{} has {} sprites, not {}", owner, node.size(), count));
    }
    if (node.size() == 0) {
        return RefuseLook(node, std::format("{} has no sprite", owner));
    }
    std::vector<core::atlas::SpriteId> sprites;
    for (const YAML::Node& entry : node) {
        const std::expected<core::atlas::SpriteId, core::Error> sprite = LookSpriteAt(entry, owner, atlas);
        if (!sprite) {
            return std::unexpected{ sprite.error() };
        }
        sprites.push_back(*sprite);
    }
    return sprites;
}

/// The map at `key` in `root`, or a refusal when it is missing or isn't one.
std::expected<YAML::Node, core::Error> MapAt(const YAML::Node& root, const char* key) {
    const YAML::Node node = root[key];
    if (!node) {
        return RefuseLook(root, std::format("the look has no {}", key));
    }
    if (!node.IsMap()) {
        return RefuseLook(node, std::format("{} isn't a map", key));
    }
    return std::expected<YAML::Node, core::Error>{ node };
}

/// The sprite at `key` in `map`, which `owner` names in refusals.
std::expected<core::atlas::SpriteId, core::Error> SpriteIn(
    const YAML::Node& map,
    const char* key,
    std::string_view owner,
    const Atlas& atlas
) {
    const YAML::Node node = map[key];
    if (!node) {
        return RefuseLook(map, std::format("{} has no {}", owner, key));
    }
    return LookSpriteAt(node, std::format("{} {}", owner, key), atlas);
}

std::expected<void, core::Error> ReadLookKinds(
    const YAML::Node& root,
    const Atlas& atlas,
    const core::WorldMap& map,
    core::WorldLook& look
) {
    const std::expected<YAML::Node, core::Error> kinds = MapAt(root, "kinds");
    if (!kinds) {
        return std::unexpected{ kinds.error() };
    }
    for (const core::TerrainKind& kind : map.Kinds()) {
        const YAML::Node variants = (*kinds)[kind.name];
        if (!variants) {
            return RefuseLook(*kinds, std::format("the map's kind {} has no look in kinds", kind.name));
        }
        std::expected<std::vector<core::atlas::SpriteId>, core::Error> sprites =
            SpriteList(variants, std::format("kind {}", kind.name), atlas, 0);
        if (!sprites) {
            return std::unexpected{ sprites.error() };
        }
        look.kinds.push_back(std::move(*sprites));
    }
    // A kind the map doesn't use is accepted, but its sprites must exist: a typo would hide until a map used it.
    for (const auto& entry : *kinds) {
        const std::string name = entry.first.Scalar();
        if (std::ranges::any_of(map.Kinds(), [&](const core::TerrainKind& kind) { return kind.name == name; })) {
            continue;
        }
        if (const auto sprites = SpriteList(entry.second, std::format("kind {}", name), atlas, 0); !sprites) {
            return std::unexpected{ sprites.error() };
        }
    }
    return {};
}

std::expected<void, core::Error> ReadMasks(
    const YAML::Node& root,
    const char* key,
    const Atlas& atlas,
    std::array<core::atlas::SpriteId, core::LINE_MASKS>& masks
) {
    const YAML::Node node = root[key];
    if (!node) {
        return RefuseLook(root, std::format("the look has no {}", key));
    }
    const std::expected<std::vector<core::atlas::SpriteId>, core::Error> sprites =
        SpriteList(node, key, atlas, core::LINE_MASKS);
    if (!sprites) {
        return std::unexpected{ sprites.error() };
    }
    std::ranges::copy(*sprites, masks.begin());
    return {};
}

std::expected<void, core::Error> ReadMarkers(
    const YAML::Node& root,
    const Atlas& atlas,
    const core::WorldMap& map,
    core::WorldLook& look
) {
    const std::expected<YAML::Node, core::Error> markers = MapAt(root, "markers");
    if (!markers) {
        return std::unexpected{ markers.error() };
    }
    const std::expected<core::atlas::SpriteId, core::Error> city = SpriteIn(*markers, "city", "marker", atlas);
    if (!city) {
        return std::unexpected{ city.error() };
    }
    look.cityMarker = *city;
    for (const auto& entry : *markers) {
        const std::string sort = entry.first.Scalar();
        if (sort == "city") {
            continue;
        }
        const std::expected<core::atlas::SpriteId, core::Error> sprite =
            LookSpriteAt(entry.second, std::format("marker {}", sort), atlas);
        if (!sprite) {
            return std::unexpected{ sprite.error() };
        }
        look.dangerMarkers.emplace_back(sort, *sprite);
    }
    for (const core::Place& place : map.Places()) {
        if (place.kind != core::PlaceKind::Danger) {
            continue;
        }
        const auto has = [&](const auto& marker) { return marker.first == place.sort; };
        if (!std::ranges::any_of(look.dangerMarkers, has)) {
            return RefuseLook(*markers, std::format("the map's danger sort {} has no marker in markers", place.sort));
        }
    }
    return {};
}

std::expected<void, core::Error> ReadRoute(
    const YAML::Node& root,
    const Atlas& atlas,
    std::vector<std::string>* warnings,
    core::RouteLook& route
) {
    const std::expected<YAML::Node, core::Error> node = MapAt(root, "route");
    if (!node) {
        return std::unexpected{ node.error() };
    }
    internal::NoteUnknownKeys(LOOK_READER, *node, "route", "key", { "dot", "spacing", "marker", "cursor" }, warnings);
    const std::expected<core::atlas::SpriteId, core::Error> dot = SpriteIn(*node, "dot", "route", atlas);
    if (!dot) {
        return std::unexpected{ dot.error() };
    }
    const std::expected<core::atlas::SpriteId, core::Error> marker = SpriteIn(*node, "marker", "route", atlas);
    if (!marker) {
        return std::unexpected{ marker.error() };
    }
    const std::expected<core::atlas::SpriteId, core::Error> cursor = SpriteIn(*node, "cursor", "route", atlas);
    if (!cursor) {
        return std::unexpected{ cursor.error() };
    }
    const std::expected<f64, core::Error> spacing =
        internal::RequiredNumber(LOOK_READER, *node, "route", "spacing", { .low = 0.0, .lowOpen = true });
    if (!spacing) {
        return std::unexpected{ spacing.error() };
    }
    route = { .dot = *dot, .spacing = static_cast<f32>(*spacing), .marker = *marker, .cursor = *cursor };
    return {};
}

} // namespace

std::expected<core::WorldLook, core::Error> ParseWorldLook(
    std::string_view yaml,
    const core::Manifest& manifest,
    const core::WorldMap& map,
    std::vector<std::string>* warnings
) {
    try {
        const YAML::Node root = YAML::Load(std::string{ yaml });
        if (!root.IsMap()) {
            return RefuseLook(root, "not a map");
        }
        const std::optional<core::SceneId> scene = manifest.SceneNamed(WORLD_LOOK_SCENE);
        if (!scene) {
            return Failure{ core::Error{ std::format("{}: the manifest has no {} scene", LOOK_READER, WORLD_LOOK_SCENE) } };
        }
        const Atlas atlas{ .manifest = &manifest, .resident = manifest.Scene(*scene).sprites };
        internal::NoteUnknownKeys(
            LOOK_READER, root, "", "key", { "kinds", "roads", "rivers", "bridge", "ford", "markers", "route" }, warnings
        );
        core::WorldLook look;
        if (auto read = ReadLookKinds(root, atlas, map, look); !read) {
            return std::unexpected{ std::move(read.error()) };
        }
        if (auto read = ReadMasks(root, "roads", atlas, look.roads); !read) {
            return std::unexpected{ std::move(read.error()) };
        }
        if (auto read = ReadMasks(root, "rivers", atlas, look.rivers); !read) {
            return std::unexpected{ std::move(read.error()) };
        }
        const std::expected<YAML::Node, core::Error> bridge = MapAt(root, "bridge");
        if (!bridge) {
            return std::unexpected{ bridge.error() };
        }
        internal::NoteUnknownKeys(LOOK_READER, *bridge, "bridge", "key", { "across", "down" }, warnings);
        const std::expected<core::atlas::SpriteId, core::Error> across = SpriteIn(*bridge, "across", "bridge", atlas);
        if (!across) {
            return std::unexpected{ across.error() };
        }
        const std::expected<core::atlas::SpriteId, core::Error> down = SpriteIn(*bridge, "down", "bridge", atlas);
        if (!down) {
            return std::unexpected{ down.error() };
        }
        look.bridgeAcross = *across;
        look.bridgeDown = *down;
        const YAML::Node ford = root["ford"];
        if (!ford) {
            return RefuseLook(root, "the look has no ford");
        }
        const std::expected<core::atlas::SpriteId, core::Error> fordSprite = LookSpriteAt(ford, "ford", atlas);
        if (!fordSprite) {
            return std::unexpected{ fordSprite.error() };
        }
        look.ford = *fordSprite;
        if (auto read = ReadMarkers(root, atlas, map, look); !read) {
            return std::unexpected{ std::move(read.error()) };
        }
        if (auto read = ReadRoute(root, atlas, warnings, look.route); !read) {
            return std::unexpected{ std::move(read.error()) };
        }
        return look;
    }
    catch (const YAML::Exception& error) {
        return std::unexpected{ internal::ErrorOf(LOOK_READER, error) };
    }
}

std::expected<core::WorldLook, core::Error> LoadWorldLook(
    const std::filesystem::path& folder,
    const core::Manifest& manifest,
    const core::WorldMap& map
) {
    std::vector<std::string> warnings;
    std::expected<core::WorldLook, core::Error> look =
        internal::ReadText(folder / "world_look.yaml").and_then([&](const std::string& text) {
            return ParseWorldLook(text, manifest, map, &warnings);
        });
    for (const std::string& warning : warnings) {
        PN_LOG_WARNING("{}", warning);
    }
    return look;
}

} // namespace data
