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
constexpr internal::Range UP_TO_TWO{ .low = 0.0, .lowOpen = false, .high = 2.0 };
constexpr internal::Range ABOVE_ZERO_TO_TWO{ .low = 0.0, .lowOpen = true, .high = 2.0 };
/// How wide the edge between two kinds may blend, in tiles: the ground shader blends an edge only with the tiles on
/// either side of it, so anything wider would look the same as one tile.
constexpr internal::Range EDGE_SOFT{ .low = 0.0, .lowOpen = true, .high = 1.0 };
/// How far from the shore water may take to turn deep, in tiles: the shore distance stops at 4 tiles and the shoreline
/// lies half a tile from the first water tile's centre, so past 3.5 the deep colour would never be reached.
constexpr internal::Range DEPTH_TILES{ .low = 0.0, .lowOpen = true, .high = 3.5 };
constexpr internal::Range STAMP_SIZE{ .low = 0.0, .lowOpen = true, .high = 4.0 };
constexpr internal::Range PUFF_WIDTH{ .low = 0.0, .lowOpen = true, .high = 2.0 };
constexpr internal::Range FADE{ .low = 0.0, .lowOpen = true, .high = 5.0 };

/// The number at `key` in `map` within `range`, which `owner` names in refusals, as an f32.
std::expected<f32, core::Error> NumberIn(
    const YAML::Node& map,
    std::string_view owner,
    const char* key,
    const internal::Range& range
) {
    return internal::RequiredNumber(LOOK_READER, map, owner, key, range).transform([](f64 value) {
        return static_cast<f32>(value);
    });
}

/// How the two numbers of a pair relate.
enum class Order : u8 {
    /// The first is no more than the second.
    Rising,
    /// The first is no less than the second.
    Falling
};

/// Two numbers at `key` in `map`, each within `range` and related as `order` says, which `owner` names in refusals.
std::expected<std::array<f32, 2>, core::Error> PairIn(
    const YAML::Node& map,
    const char* key,
    std::string_view owner,
    const internal::Range& range,
    Order order
) {
    const YAML::Node node = map[key];
    if (!node) {
        return RefuseLook(map, std::format("{} has no {}", owner, key));
    }
    const auto refuse = [&] {
        return RefuseLook(
            node,
            std::format(
                "{}'s {} isn't two numbers, each {}, the first {} the second",
                owner,
                key,
                internal::DescribeRange(range),
                order == Order::Rising ? "no more than" : "no less than"
            )
        );
    };
    if (!node.IsSequence() || node.size() != 2) {
        return refuse();
    }
    std::array<f64, 2> values{};
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!YAML::convert<f64>::decode(node[index], values.at(index)) || !std::isfinite(values.at(index))) {
            return refuse();
        }
        const f64 value = values.at(index);
        const bool tooLow = range.lowOpen ? value <= range.low : value < range.low;
        if (tooLow || value > range.high) {
            return refuse();
        }
    }
    if (order == Order::Rising ? values[0] > values[1] : values[0] < values[1]) {
        return refuse();
    }
    return std::array<f32, 2>{ { static_cast<f32>(values[0]), static_cast<f32>(values[1]) } };
}

/// The true or false at `key` in `map`, which `owner` names in refusals.
std::expected<bool, core::Error> FlagIn(const YAML::Node& map, const char* key, std::string_view owner) {
    const YAML::Node node = map[key];
    if (!node) {
        return RefuseLook(map, std::format("{} has no {}", owner, key));
    }
    bool value = false;
    if (!YAML::convert<bool>::decode(node, value)) {
        return RefuseLook(node, std::format("{}'s {} isn't true or false", owner, key));
    }
    return value;
}

/// One kind's stamps: `node` is its `stamps:` map, which `owner` names in refusals.
std::expected<core::StampLook, core::Error> ReadStamps(
    const YAML::Node& node,
    std::string_view owner,
    const Atlas& atlas,
    std::vector<std::string>* warnings
) {
    internal::NoteUnknownKeys(
        LOOK_READER, node, owner, "key", { "sprites", "density", "size", "jitter", "thin_below", "mist" }, warnings
    );
    if (!node["sprites"]) {
        return RefuseLook(node, std::format("{} has no sprites", owner));
    }
    std::expected<std::vector<core::atlas::SpriteId>, core::Error> sprites =
        SpriteList(node["sprites"], std::format("{} sprites", owner), atlas, 0);
    if (!sprites) {
        return std::unexpected{ sprites.error() };
    }
    const std::expected<f32, core::Error> density = NumberIn(node, owner, "density", internal::FRACTION);
    if (!density) {
        return std::unexpected{ density.error() };
    }
    const std::expected<std::array<f32, 2>, core::Error> size = PairIn(node, "size", owner, STAMP_SIZE, Order::Rising);
    if (!size) {
        return std::unexpected{ size.error() };
    }
    const std::expected<f32, core::Error> jitter = NumberIn(node, owner, "jitter", internal::FRACTION);
    if (!jitter) {
        return std::unexpected{ jitter.error() };
    }
    const std::expected<f32, core::Error> thinBelow = NumberIn(node, owner, "thin_below", UP_TO_TWO);
    if (!thinBelow) {
        return std::unexpected{ thinBelow.error() };
    }
    const std::expected<bool, core::Error> mist = FlagIn(node, "mist", owner);
    if (!mist) {
        return std::unexpected{ mist.error() };
    }
    return core::StampLook{ .sprites = std::move(*sprites),
                            .density = *density,
                            .size = *size,
                            .jitter = *jitter,
                            .thinBelow = *thinBelow,
                            .mist = *mist };
}

/// The look of the kind `name`, whose map is `node`.
std::expected<core::KindLook, core::Error> ReadKind(
    const YAML::Node& node,
    const std::string& name,
    const Atlas& atlas,
    std::vector<std::string>* warnings
) {
    const std::string owner = std::format("kind {}", name);
    if (!node.IsMap()) {
        return RefuseLook(node, std::format("{} isn't a map", owner));
    }
    internal::NoteUnknownKeys(LOOK_READER, node, owner, "key", { "wash", "strength", "stamps" }, warnings);
    const std::expected<std::array<u8, 3>, core::Error> wash = ColourIn(node, "wash", owner);
    if (!wash) {
        return std::unexpected{ wash.error() };
    }
    const std::expected<f32, core::Error> strength = NumberIn(node, owner, "strength", internal::FRACTION);
    if (!strength) {
        return std::unexpected{ strength.error() };
    }
    const std::expected<YAML::Node, core::Error> stamps = MapAt(node, "stamps", owner);
    if (!stamps) {
        return std::unexpected{ stamps.error() };
    }
    std::expected<core::StampLook, core::Error> read = ReadStamps(*stamps, std::format("{} stamps", owner), atlas, warnings);
    if (!read) {
        return std::unexpected{ read.error() };
    }
    return core::KindLook{ .wash = *wash, .strength = *strength, .stamps = std::move(*read) };
}

std::expected<void, core::Error> ReadLookKinds(
    const YAML::Node& root,
    const Atlas& atlas,
    const core::WorldMap& map,
    std::vector<std::string>* warnings,
    core::WorldLook& look
) {
    const std::expected<YAML::Node, core::Error> kinds = MapAt(root, "kinds");
    if (!kinds) {
        return std::unexpected{ kinds.error() };
    }
    if (kinds->size() > core::MAX_GROUND_KINDS) {
        return RefuseLook(
            *kinds, std::format("kinds has {} kinds, more than the {} the ground holds", kinds->size(), core::MAX_GROUND_KINDS)
        );
    }
    for (const core::TerrainKind& kind : map.Kinds()) {
        const YAML::Node entry = (*kinds)[kind.name];
        if (!entry) {
            return RefuseLook(*kinds, std::format("the map's kind {} has no look in kinds", kind.name));
        }
        std::expected<core::KindLook, core::Error> read = ReadKind(entry, kind.name, atlas, warnings);
        if (!read) {
            return std::unexpected{ read.error() };
        }
        look.kinds.push_back(std::move(*read));
    }
    // A kind the map doesn't use is accepted, but its sprites must exist: a typo would hide until a map used it.
    for (const auto& entry : *kinds) {
        const std::string name = entry.first.Scalar();
        if (std::ranges::any_of(map.Kinds(), [&](const core::TerrainKind& kind) { return kind.name == name; })) {
            continue;
        }
        if (const auto read = ReadKind(entry.second, name, atlas, warnings); !read) {
            return std::unexpected{ read.error() };
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
    const std::expected<f32, core::Error> strength = NumberIn(*map, section, "strength", internal::FRACTION);
    if (!strength) {
        return std::unexpected{ strength.error() };
    }
    return ColourIn(*map, colourKey, section).transform([&](const std::array<u8, 3>& colour) {
        return Tint{ .strength = *strength, .colour = colour };
    });
}

std::expected<void, core::Error> ReadFogMotion(const YAML::Node& node, std::vector<std::string>* warnings, core::FogLook& fog) {
    const std::expected<f32, core::Error> width = NumberIn(node, "fog", "cloud_width", CLOUD_WIDTH);
    if (!width) {
        return std::unexpected{ width.error() };
    }
    const std::expected<YAML::Node, core::Error> bob = MapAt(node, "bob", "fog");
    if (!bob) {
        return std::unexpected{ bob.error() };
    }
    internal::NoteUnknownKeys(LOOK_READER, *bob, "bob", "key", { "amplitude", "period" }, warnings);
    const std::expected<f32, core::Error> amplitude = NumberIn(*bob, "bob", "amplitude", BOB_AMPLITUDE);
    if (!amplitude) {
        return std::unexpected{ amplitude.error() };
    }
    const std::expected<f32, core::Error> period = NumberIn(*bob, "bob", "period", POSITIVE);
    if (!period) {
        return std::unexpected{ period.error() };
    }
    const std::expected<f32, core::Error> drift = NumberIn(node, "fog", "drift", DRIFT);
    if (!drift) {
        return std::unexpected{ drift.error() };
    }
    const std::expected<f32, core::Error> fade = NumberIn(node, "fog", "fade_seconds", FADE);
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

/// The index of the map's kind named by `key` in `map`, which `owner` names in refusals.
std::expected<u8, core::Error> KindIn(
    const YAML::Node& map,
    const char* key,
    std::string_view owner,
    const core::WorldMap& world
) {
    const YAML::Node node = map[key];
    if (!node) {
        return RefuseLook(map, std::format("{} has no {}", owner, key));
    }
    const std::string& name = node.Scalar();
    const std::span<const core::TerrainKind> kinds = world.Kinds();
    const auto found = std::ranges::find(kinds, name, &core::TerrainKind::name);
    if (found == kinds.end()) {
        return RefuseLook(node, std::format("{}'s {} \"{}\" isn't a kind of the map", owner, key, name));
    }
    return static_cast<u8>(found - kinds.begin());
}

std::expected<void, core::Error> ReadWater(
    const YAML::Node& ground,
    const core::WorldMap& map,
    std::vector<std::string>* warnings,
    core::GroundLook& look
) {
    const std::expected<YAML::Node, core::Error> water = MapAt(ground, "water", "ground");
    if (!water) {
        return std::unexpected{ water.error() };
    }
    internal::NoteUnknownKeys(LOOK_READER, *water, "water", "key", { "shallow", "deep", "depth_tiles" }, warnings);
    const std::expected<u8, core::Error> shallow = KindIn(*water, "shallow", "water", map);
    if (!shallow) {
        return std::unexpected{ shallow.error() };
    }
    const std::expected<u8, core::Error> deep = KindIn(*water, "deep", "water", map);
    if (!deep) {
        return std::unexpected{ deep.error() };
    }
    if (*shallow == *deep) {
        return RefuseLook((*water)["deep"], "water's deep is the same kind as its shallow");
    }
    const std::expected<f32, core::Error> depth = NumberIn(*water, "water", "depth_tiles", DEPTH_TILES);
    if (!depth) {
        return std::unexpected{ depth.error() };
    }
    look.shallowKind = *shallow;
    look.deepKind = *deep;
    look.depthTiles = *depth;
    return {};
}

std::expected<void, core::Error> ReadShoreAndGrid(
    const YAML::Node& ground,
    std::vector<std::string>* warnings,
    core::GroundLook& look
) {
    const std::expected<YAML::Node, core::Error> shore = MapAt(ground, "shore", "ground");
    if (!shore) {
        return std::unexpected{ shore.error() };
    }
    internal::NoteUnknownKeys(LOOK_READER, *shore, "shore", "key", { "colour", "strength", "width" }, warnings);
    const std::expected<std::array<u8, 3>, core::Error> shoreColour = ColourIn(*shore, "colour", "shore");
    if (!shoreColour) {
        return std::unexpected{ shoreColour.error() };
    }
    const std::expected<f32, core::Error> shoreStrength = NumberIn(*shore, "shore", "strength", internal::FRACTION);
    if (!shoreStrength) {
        return std::unexpected{ shoreStrength.error() };
    }
    const std::expected<f32, core::Error> shoreWidth = NumberIn(*shore, "shore", "width", ABOVE_ZERO_TO_TWO);
    if (!shoreWidth) {
        return std::unexpected{ shoreWidth.error() };
    }
    const std::expected<YAML::Node, core::Error> grid = MapAt(ground, "grid", "ground");
    if (!grid) {
        return std::unexpected{ grid.error() };
    }
    internal::NoteUnknownKeys(LOOK_READER, *grid, "grid", "key", { "colour", "strength", "fade" }, warnings);
    const std::expected<std::array<u8, 3>, core::Error> gridColour = ColourIn(*grid, "colour", "grid");
    if (!gridColour) {
        return std::unexpected{ gridColour.error() };
    }
    const std::expected<f32, core::Error> gridStrength = NumberIn(*grid, "grid", "strength", internal::FRACTION);
    if (!gridStrength) {
        return std::unexpected{ gridStrength.error() };
    }
    const std::expected<std::array<f32, 2>, core::Error> fade = PairIn(*grid, "fade", "grid", UP_TO_TWO, Order::Falling);
    if (!fade) {
        return std::unexpected{ fade.error() };
    }
    look.shore = *shoreColour;
    look.shoreStrength = *shoreStrength;
    look.shoreWidth = *shoreWidth;
    look.grid = *gridColour;
    look.gridStrength = *gridStrength;
    look.gridFade = *fade;
    return {};
}

std::expected<void, core::Error> ReadGround(
    const YAML::Node& root,
    const core::WorldMap& map,
    std::vector<std::string>* warnings,
    core::GroundLook& look
) {
    const std::expected<YAML::Node, core::Error> ground = MapAt(root, "ground");
    if (!ground) {
        return std::unexpected{ ground.error() };
    }
    internal::NoteUnknownKeys(
        LOOK_READER,
        *ground,
        "ground",
        "key",
        { "paper", "grain", "edge_warp", "edge_soft", "blotch", "water", "shore", "grid" },
        warnings
    );
    const std::expected<std::array<u8, 3>, core::Error> paper = ColourIn(*ground, "paper", "ground");
    if (!paper) {
        return std::unexpected{ paper.error() };
    }
    const std::expected<f32, core::Error> grain = NumberIn(*ground, "ground", "grain", internal::FRACTION);
    if (!grain) {
        return std::unexpected{ grain.error() };
    }
    const std::expected<f32, core::Error> warp = NumberIn(*ground, "ground", "edge_warp", UP_TO_TWO);
    if (!warp) {
        return std::unexpected{ warp.error() };
    }
    const std::expected<f32, core::Error> soft = NumberIn(*ground, "ground", "edge_soft", EDGE_SOFT);
    if (!soft) {
        return std::unexpected{ soft.error() };
    }
    const std::expected<f32, core::Error> blotch = NumberIn(*ground, "ground", "blotch", internal::FRACTION);
    if (!blotch) {
        return std::unexpected{ blotch.error() };
    }
    look.paper = *paper;
    look.grain = *grain;
    look.edgeWarp = *warp;
    look.edgeSoft = *soft;
    look.blotch = *blotch;
    if (auto read = ReadWater(*ground, map, warnings, look); !read) {
        return read;
    }
    return ReadShoreAndGrid(*ground, warnings, look);
}

std::expected<void, core::Error> ReadVeil(const YAML::Node& root, std::vector<std::string>* warnings, core::VeilLook& veil) {
    const std::expected<YAML::Node, core::Error> node = MapAt(root, "veil");
    if (!node) {
        return std::unexpected{ node.error() };
    }
    internal::NoteUnknownKeys(LOOK_READER, *node, "veil", "key", { "colour", "scale", "strength" }, warnings);
    const std::expected<std::array<u8, 3>, core::Error> colour = ColourIn(*node, "colour", "veil");
    if (!colour) {
        return std::unexpected{ colour.error() };
    }
    const std::expected<f32, core::Error> scale = NumberIn(*node, "veil", "scale", POSITIVE);
    if (!scale) {
        return std::unexpected{ scale.error() };
    }
    const std::expected<f32, core::Error> strength = NumberIn(*node, "veil", "strength", internal::FRACTION);
    if (!strength) {
        return std::unexpected{ strength.error() };
    }
    veil = { .colour = *colour, .scale = *scale, .strength = *strength };
    return {};
}

std::expected<void, core::Error> ReadMist(
    const YAML::Node& root,
    const Atlas& atlas,
    std::vector<std::string>* warnings,
    core::MistLook& mist
) {
    const std::expected<YAML::Node, core::Error> node = MapAt(root, "mist");
    if (!node) {
        return std::unexpected{ node.error() };
    }
    internal::NoteUnknownKeys(
        LOOK_READER, *node, "mist", "key", { "sprites", "colour", "strength", "chance", "width" }, warnings
    );
    if (!(*node)["sprites"]) {
        return RefuseLook(*node, "mist has no sprites");
    }
    std::expected<std::vector<core::atlas::SpriteId>, core::Error> sprites =
        SpriteList((*node)["sprites"], "mist sprites", atlas, 0);
    if (!sprites) {
        return std::unexpected{ sprites.error() };
    }
    const std::expected<std::array<u8, 3>, core::Error> colour = ColourIn(*node, "colour", "mist");
    if (!colour) {
        return std::unexpected{ colour.error() };
    }
    const std::expected<f32, core::Error> strength = NumberIn(*node, "mist", "strength", internal::FRACTION);
    if (!strength) {
        return std::unexpected{ strength.error() };
    }
    const std::expected<f32, core::Error> chance = NumberIn(*node, "mist", "chance", internal::FRACTION);
    if (!chance) {
        return std::unexpected{ chance.error() };
    }
    const std::expected<std::array<f32, 2>, core::Error> width = PairIn(*node, "width", "mist", PUFF_WIDTH, Order::Rising);
    if (!width) {
        return std::unexpected{ width.error() };
    }
    mist = { .sprites = std::move(*sprites), .colour = *colour, .strength = *strength, .chance = *chance, .width = *width };
    return {};
}

std::expected<void, core::Error> ReadRiver(const YAML::Node& root, std::vector<std::string>* warnings, core::RiverLook& river) {
    const std::expected<YAML::Node, core::Error> node = MapAt(root, "river");
    if (!node) {
        return std::unexpected{ node.error() };
    }
    internal::NoteUnknownKeys(
        LOOK_READER, *node, "river", "key", { "width", "edge", "colour", "edge_colour", "edge_strength" }, warnings
    );
    const std::expected<f32, core::Error> width = NumberIn(*node, "river", "width", ABOVE_ZERO_TO_TWO);
    if (!width) {
        return std::unexpected{ width.error() };
    }
    const std::expected<f32, core::Error> edge = NumberIn(*node, "river", "edge", ABOVE_ZERO_TO_TWO);
    if (!edge) {
        return std::unexpected{ edge.error() };
    }
    const std::expected<std::array<u8, 3>, core::Error> colour = ColourIn(*node, "colour", "river");
    if (!colour) {
        return std::unexpected{ colour.error() };
    }
    const std::expected<std::array<u8, 3>, core::Error> edgeColour = ColourIn(*node, "edge_colour", "river");
    if (!edgeColour) {
        return std::unexpected{ edgeColour.error() };
    }
    const std::expected<f32, core::Error> edgeStrength = NumberIn(*node, "river", "edge_strength", internal::FRACTION);
    if (!edgeStrength) {
        return std::unexpected{ edgeStrength.error() };
    }
    river = { .width = *width, .edge = *edge, .colour = *colour, .edgeColour = *edgeColour, .edgeStrength = *edgeStrength };
    return {};
}

/// The paintings `node` names by sort or key, in the file's order; `owner` names them in refusals. When `known` is given,
/// a name it rejects is refused.
template<typename Known>
std::expected<std::vector<std::pair<std::string, core::atlas::SpriteId>>, core::Error> PaintingsIn(
    const YAML::Node& node,
    std::string_view owner,
    const Atlas& atlas,
    const Known& known
) {
    if (!node.IsMap()) {
        return RefuseLook(node, std::format("{} isn't a map", owner));
    }
    std::vector<std::pair<std::string, core::atlas::SpriteId>> paintings;
    for (const auto& entry : node) {
        const std::string name = entry.first.Scalar();
        const std::expected<core::atlas::SpriteId, core::Error> sprite =
            LookSpriteAt(entry.second, std::format("{} {}", owner, name), atlas);
        if (!sprite) {
            return std::unexpected{ sprite.error() };
        }
        if constexpr (!std::is_same_v<Known, std::nullptr_t>) {
            if (!known(name)) {
                return RefuseLook(entry.first, std::format("{} names {}, which no place of the map has", owner, name));
            }
        }
        paintings.emplace_back(name, *sprite);
    }
    return paintings;
}

std::expected<void, core::Error> ReadPlaces(
    const YAML::Node& root,
    const Atlas& atlas,
    const core::WorldMap& map,
    std::vector<std::string>* warnings,
    core::PlaceLook& places
) {
    const std::expected<YAML::Node, core::Error> node = MapAt(root, "places");
    if (!node) {
        return std::unexpected{ node.error() };
    }
    internal::NoteUnknownKeys(LOOK_READER, *node, "places", "key", { "city", "dangers", "by_key" }, warnings);
    const std::expected<core::atlas::SpriteId, core::Error> city = SpriteIn(*node, "city", "places", atlas);
    if (!city) {
        return std::unexpected{ city.error() };
    }
    const std::expected<YAML::Node, core::Error> dangers = MapAt(*node, "dangers", "places");
    if (!dangers) {
        return std::unexpected{ dangers.error() };
    }
    const std::expected<YAML::Node, core::Error> byKey = MapAt(*node, "by_key", "places");
    if (!byKey) {
        return std::unexpected{ byKey.error() };
    }
    const auto isKey = [&](const std::string& key) {
        return std::ranges::any_of(map.Places(), [&](const core::Place& place) { return place.key == key; });
    };
    auto dangerPaintings = PaintingsIn(*dangers, "painting", atlas, nullptr);
    if (!dangerPaintings) {
        return std::unexpected{ dangerPaintings.error() };
    }
    auto keyPaintings = PaintingsIn(*byKey, "painting for key", atlas, isKey);
    if (!keyPaintings) {
        return std::unexpected{ keyPaintings.error() };
    }
    for (const core::Place& place : map.Places()) {
        if (place.kind == core::PlaceKind::Danger &&
            !std::ranges::contains(*dangerPaintings, place.sort, &std::pair<std::string, core::atlas::SpriteId>::first)) {
            return RefuseLook(*dangers, std::format("the map's danger sort {} has no painting in dangers", place.sort));
        }
    }
    places = { .city = *city, .dangers = std::move(*dangerPaintings), .byKey = std::move(*keyPaintings) };
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
            LOOK_READER,
            root,
            "",
            "key",
            { "kinds", "roads", "bridge", "ford", "markers", "route", "fog", "ground", "veil", "mist", "river", "places" },
            warnings
        );
        core::WorldLook look;
        if (auto read = ReadLookKinds(root, atlas, map, warnings, look); !read) {
            return std::unexpected{ std::move(read.error()) };
        }
        if (auto read = ReadMasks(root, "roads", atlas, look.roads); !read) {
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
        if (auto read = ReadGround(root, map, warnings, look.ground); !read) {
            return std::unexpected{ std::move(read.error()) };
        }
        if (auto read = ReadVeil(root, warnings, look.veil); !read) {
            return std::unexpected{ std::move(read.error()) };
        }
        if (auto read = ReadMist(root, atlas, warnings, look.mist); !read) {
            return std::unexpected{ std::move(read.error()) };
        }
        if (auto read = ReadRiver(root, warnings, look.river); !read) {
            return std::unexpected{ std::move(read.error()) };
        }
        if (auto read = ReadPlaces(root, atlas, map, warnings, look.places); !read) {
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
