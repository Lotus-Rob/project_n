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

/// The map at `key` in `root`, or a refusal when it is missing or isn't one. `owner` names `root` in refusals ("the
/// look" when empty).
std::expected<YAML::Node, core::Error> MapAt(const YAML::Node& root, const char* key, std::string_view owner = {}) {
    const YAML::Node node = root[key];
    if (!node) {
        return RefuseLook(root, std::format("{} has no {}", owner.empty() ? "the look" : owner, key));
    }
    if (!node.IsMap()) {
        return RefuseLook(
            node, owner.empty() ? std::format("{} isn't a map", key) : std::format("{}'s {} isn't a map", owner, key)
        );
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

// Far more than a tile (256 units) and few enough dots to fit the route buffer; it also keeps the f32 cast defined.
constexpr f64 MAX_ROUTE_SPACING = 4096.0;

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
    const std::expected<f64, core::Error> spacing = internal::RequiredNumber(
        LOOK_READER, *node, "route", "spacing", { .low = 0.0, .lowOpen = true, .high = MAX_ROUTE_SPACING }
    );
    if (!spacing) {
        return std::unexpected{ spacing.error() };
    }
    route = { .dot = *dot, .spacing = static_cast<f32>(*spacing), .marker = *marker, .cursor = *cursor };
    return {};
}

/// The colour at `key` in `map`: three whole numbers from 0 to 255, which `owner` names in refusals.
std::expected<std::array<u8, 3>, core::Error> ColourIn(const YAML::Node& map, const char* key, std::string_view owner) {
    const YAML::Node node = map[key];
    if (!node) {
        return RefuseLook(map, std::format("{} has no {}", owner, key));
    }
    const auto refuse = [&] {
        return RefuseLook(node, std::format("{}'s {} isn't three whole numbers from 0 to 255", owner, key));
    };
    if (!node.IsSequence() || node.size() != 3) {
        return refuse();
    }
    std::array<u8, 3> colour{};
    for (std::size_t channel = 0; channel < colour.size(); ++channel) {
        i32 value = 0;
        if (!YAML::convert<i32>::decode(node[channel], value) || !std::in_range<u8>(value)) {
            return refuse();
        }
        colour[channel] = static_cast<u8>(value);
    }
    return colour;
}

constexpr internal::Range CLOUD_WIDTH{ .low = 0.0, .lowOpen = true, .high = 4.0 };
constexpr internal::Range BOB_AMPLITUDE{ .low = 0.0, .lowOpen = false, .high = 0.5 };
constexpr internal::Range POSITIVE{ .low = 0.0, .lowOpen = true };
constexpr internal::Range DRIFT{ .low = 0.0, .lowOpen = false, .high = 2.0 };
constexpr internal::Range FADE{ .low = 0.0, .lowOpen = true, .high = 5.0 };

/// The number at `key` in `map` within `range`, which `owner` names in refusals, as an f32.
std::expected<f32, core::Error> FogNumber(
    const YAML::Node& map,
    std::string_view owner,
    const char* key,
    const internal::Range& range
) {
    return internal::RequiredNumber(LOOK_READER, map, owner, key, range).transform([](f64 value) {
        return static_cast<f32>(value);
    });
}

/// A strength from 0 to 1 and the colour it tints towards.
struct Tint {
    f32 strength;
    std::array<u8, 3> colour;
};

/// The tint of the map `section` of `fog`, whose colour is at `colourKey`; an unknown key of the section is noted in
/// `warnings`.
std::expected<Tint, core::Error> TintIn(
    const YAML::Node& fog,
    const char* section,
    const char* colourKey,
    std::vector<std::string>* warnings
) {
    const std::expected<YAML::Node, core::Error> map = MapAt(fog, section, "fog");
    if (!map) {
        return std::unexpected{ map.error() };
    }
    internal::NoteUnknownKeys(LOOK_READER, *map, section, "key", { "strength", colourKey }, warnings);
    const std::expected<f32, core::Error> strength = FogNumber(*map, section, "strength", internal::FRACTION);
    if (!strength) {
        return std::unexpected{ strength.error() };
    }
    return ColourIn(*map, colourKey, section).transform([&](const std::array<u8, 3>& colour) {
        return Tint{ .strength = *strength, .colour = colour };
    });
}

std::expected<void, core::Error> ReadFogMotion(const YAML::Node& node, std::vector<std::string>* warnings, core::FogLook& fog) {
    const std::expected<f32, core::Error> width = FogNumber(node, "fog", "cloud_width", CLOUD_WIDTH);
    if (!width) {
        return std::unexpected{ width.error() };
    }
    const std::expected<YAML::Node, core::Error> bob = MapAt(node, "bob", "fog");
    if (!bob) {
        return std::unexpected{ bob.error() };
    }
    internal::NoteUnknownKeys(LOOK_READER, *bob, "bob", "key", { "amplitude", "period" }, warnings);
    const std::expected<f32, core::Error> amplitude = FogNumber(*bob, "bob", "amplitude", BOB_AMPLITUDE);
    if (!amplitude) {
        return std::unexpected{ amplitude.error() };
    }
    const std::expected<f32, core::Error> period = FogNumber(*bob, "bob", "period", POSITIVE);
    if (!period) {
        return std::unexpected{ period.error() };
    }
    const std::expected<f32, core::Error> drift = FogNumber(node, "fog", "drift", DRIFT);
    if (!drift) {
        return std::unexpected{ drift.error() };
    }
    const std::expected<f32, core::Error> fade = FogNumber(node, "fog", "fade_seconds", FADE);
    if (!fade) {
        return std::unexpected{ fade.error() };
    }
    fog.cloudWidth = *width;
    fog.bobAmplitude = *amplitude;
    fog.bobPeriod = *period;
    fog.drift = *drift;
    fog.fadeSeconds = *fade;
    return {};
}

std::expected<void, core::Error> ReadFogColours(
    const YAML::Node& node,
    std::vector<std::string>* warnings,
    core::FogLook& fog
) {
    const std::expected<Tint, core::Error> drain = TintIn(node, "drain", "sepia", warnings);
    if (!drain) {
        return std::unexpected{ drain.error() };
    }
    const std::expected<Tint, core::Error> mist = TintIn(node, "mist", "colour", warnings);
    if (!mist) {
        return std::unexpected{ mist.error() };
    }
    const std::expected<std::array<u8, 3>, core::Error> shade = ColourIn(node, "shade", "fog");
    if (!shade) {
        return std::unexpected{ shade.error() };
    }
    fog.drain = drain->strength;
    fog.sepia = drain->colour;
    fog.mistStrength = mist->strength;
    fog.mist = mist->colour;
    fog.shade = *shade;
    return {};
}

std::expected<void, core::Error> ReadFog(
    const YAML::Node& root,
    const Atlas& atlas,
    std::vector<std::string>* warnings,
    core::FogLook& fog
) {
    const std::expected<YAML::Node, core::Error> node = MapAt(root, "fog");
    if (!node) {
        return std::unexpected{ node.error() };
    }
    internal::NoteUnknownKeys(
        LOOK_READER,
        *node,
        "fog",
        "key",
        { "clouds", "cloud_width", "bob", "drift", "fade_seconds", "drain", "mist", "shade" },
        warnings
    );
    if (!(*node)["clouds"]) {
        return RefuseLook(*node, "fog has no clouds");
    }
    std::expected<std::vector<core::atlas::SpriteId>, core::Error> sprites =
        SpriteList((*node)["clouds"], "fog clouds", atlas, 0);
    if (!sprites) {
        return std::unexpected{ sprites.error() };
    }
    fog.clouds = std::move(*sprites);
    if (auto read = ReadFogMotion(*node, warnings, fog); !read) {
        return read;
    }
    return ReadFogColours(*node, warnings, fog);
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
            LOOK_READER, root, "", "key", { "kinds", "roads", "rivers", "bridge", "ford", "markers", "route", "fog" }, warnings
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
        if (auto read = ReadFog(root, atlas, warnings, look.fog); !read) {
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
