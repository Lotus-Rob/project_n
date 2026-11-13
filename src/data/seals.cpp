#include "data/seals.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/tick_clock.hpp"
#include "data/internal/yaml.hpp"

namespace data {

namespace {

constexpr std::string_view WHAT = "seals";

// Bounds that keep every number a sane f32 and every loop short; the spec leaves them to the builder.
constexpr i64 MOST_ITEMS = 360; // ticks, dots, glyphs or flourishes round one ring, star points or polygon sides
constexpr i64 MOST_SIDES = core::MAX_SEAL_FRAME_SIDES;                                     // a frame's sides
constexpr std::size_t MOST_SPRITES = std::tuple_size_v<decltype(core::SealPart::sprites)>; // a flourish's sprites
constexpr f64 MOST_DEGREES = 360.0;                                                        // an angle or a dash
constexpr f64 MOST_SPIN = 720.0;                                                           // degrees a second
constexpr f64 MOST_WORLD = 100'000.0;
constexpr f64 MOST_TIME = 3'600.0;
constexpr f64 MOST_REACH = 4.0;
constexpr f64 MOST_SCALE = 10.0;
constexpr f64 MOST_HALO_REACH = 0.1; // a tenth of the seal's size: past it every layer's band would widen far
constexpr f64 LEAST_PERIOD = 1.0 / static_cast<f64>(core::TICKS_PER_SECOND);
// A shard's slowest turn: one turn in 100 hours. Slower would only make its cycle a number past an f32.
constexpr f64 LEAST_SHARD_SPIN = 0.001;
// The least satellite: a thousandth of its seal, so nested twice it stays a normal f32 on a seal above about 1.2e-32 across
// (the layout skips a smaller one).
constexpr f64 LEAST_SATELLITE = 0.001;

constexpr internal::Range SIZE{ .low = 0.0, .lowOpen = true, .high = 1.0 };
constexpr internal::Range SATELLITE_SIZE{ .low = LEAST_SATELLITE, .lowOpen = false, .high = 1.0 };
constexpr internal::Range ANGLE{ .low = -MOST_DEGREES, .lowOpen = false, .high = MOST_DEGREES };
constexpr internal::Range ARC{ .low = 0.0, .lowOpen = false, .high = MOST_DEGREES };
constexpr internal::Range SPIN{ .low = -MOST_SPIN, .lowOpen = false, .high = MOST_SPIN };
constexpr internal::Range WORLD{ .low = 0.0, .lowOpen = false, .high = MOST_WORLD };
constexpr internal::Range WORLD_SIZE{ .low = 0.0, .lowOpen = true, .high = MOST_WORLD };
constexpr internal::Range TIME{ .low = 0.0, .lowOpen = false, .high = MOST_TIME };
constexpr internal::Range LIFETIME{ .low = 0.0, .lowOpen = true, .high = MOST_TIME };
constexpr internal::Range WASH_REACH{ .low = 0.0, .lowOpen = true, .high = MOST_REACH };
constexpr internal::Range SCALE{ .low = 0.0, .lowOpen = false, .high = MOST_SCALE };
constexpr internal::Range RATIO{ .low = 0.0, .lowOpen = true, .high = 1.0 };
constexpr internal::Range HALO_REACH{ .low = 0.0, .lowOpen = false, .high = MOST_HALO_REACH };
constexpr internal::Range PILLAR_BAND{ .low = 0.0, .lowOpen = true, .high = static_cast<f64>(core::MOST_SEAL_PILLAR_BAND) };

constexpr std::array<std::string_view, 3> PLANE_NAMES{ { "ground", "upright", "overhead" } };
constexpr std::array<std::string_view, 7> PART_NAMES{ { "circle", "ticks", "dots", "script", "star", "polygon", "flourish" } };
constexpr std::array<std::string_view, 3> MOUNT_NAMES{ { "fixed", "with_layer", "orbit" } };
constexpr std::array<std::string_view, 2> ORDER_NAMES{ { "outer_first", "inner_first" } };
constexpr std::array<std::string_view, 2> FACING_NAMES{ { "outward", "inward" } };

std::unexpected<core::Error> RefuseSeals(const YAML::Node& node, std::string_view why) {
    return std::unexpected{ internal::RefusalAt(WHAT, node, why) };
}

// The sprite `node` names, which must be in the atlas and in `manifest`.
std::expected<core::atlas::SpriteId, core::Error> SpriteAt(
    const YAML::Node& node,
    std::string_view owner,
    const core::Manifest& manifest
) {
    const std::string name = node.IsScalar() ? node.Scalar() : std::string{};
    const std::optional<core::atlas::SpriteId> sprite = core::SpriteNamed(name);
    if (!sprite || std::to_underlying(*sprite) >= manifest.sprites.size()) {
        return RefuseSeals(node, std::format("{} names the sprite \"{}\", which isn't in the atlas", owner, name));
    }
    return *sprite;
}

// Reads one map's values, keeping the first refusal: each read after it does nothing, so a reader reads every field and
// checks once (Result()). An absent key leaves its field as it was unless the key is required.
class Fields {
public:
    Fields(
        const YAML::Node& map,
        std::string owner,
        std::initializer_list<std::string_view> known,
        std::vector<std::string>* warnings
    )
      : m_map{ map }
      , m_owner{ std::move(owner) } {
        if (!map.IsMap()) {
            m_error = internal::RefusalAt(WHAT, map, std::format("{} isn't a map", m_owner));
            return;
        }
        internal::NoteUnknownKeys(WHAT, map, m_owner, "key", known, warnings);
    }

    template<typename Number>
    void Read(const char* key, Number& field, const internal::Range& range, bool required = false) {
        static_assert(std::is_floating_point_v<Number>, "a tuning value is an f32 or an f64");
        if (m_error || !Present(key, required)) {
            return;
        }
        const std::expected<f64, core::Error> value = internal::TunedNumber(WHAT, m_map, m_owner, key, 0.0, range);
        if (!value) {
            m_error = value.error();
            return;
        }
        // A number in range can leave it as it narrows: 1e-50 is above 0, but an f32 holds it as 0.
        if (std::abs(*value) > static_cast<f64>(std::numeric_limits<Number>::max())) {
            Fail(m_map[key], std::format("{}'s {} is too large for an f32", m_owner, key));
            return;
        }
        const auto narrowed = static_cast<Number>(*value);
        if (range.lowOpen && !(std::isnormal(narrowed) && static_cast<f64>(narrowed) > range.low)) {
            Fail(m_map[key], std::format("{}'s {} is too small for an f32", m_owner, key));
            return;
        }
        field = narrowed;
    }

    void Whole(const char* key, u32& field, i64 least, i64 most, bool required = false) {
        if (m_error || !Present(key, required)) {
            return;
        }
        const std::expected<i64, core::Error> value = internal::WholeNumber(WHAT, m_map, m_owner, key, true, most);
        if (!value) {
            m_error = value.error();
            return;
        }
        if (*value < least) {
            Fail(m_map[key], std::format("{}'s {} isn't a whole number from {} to {}", m_owner, key, least, most));
            return;
        }
        field = static_cast<u32>(*value);
    }

    // A stream's period, in seconds: at least a tick, so the stream comes round at most once a tick and its laps stay a
    // count an f64 holds exactly.
    void Period(const char* key, f64& field, bool required) {
        Read(key, field, LIFETIME, required);
        if (!m_error && m_map[key] && field < LEAST_PERIOD) {
            Fail(m_map[key], std::format("{}'s {} is under a tick (1/{} s)", m_owner, key, core::TICKS_PER_SECOND));
        }
    }

    template<typename Enum, std::size_t Count>
    void Named(const char* key, const std::array<std::string_view, Count>& names, Enum& field) {
        if (m_error) {
            return;
        }
        if (const std::expected<void, core::Error> read = internal::ReadNamed(WHAT, m_map, m_owner, key, names, field); !read) {
            m_error = read.error();
        }
    }

    // Which of the `colours` colours of `seal` the `colour` key picks (0 when absent).
    void Slot(u8& field, u8 colours, std::string_view seal) {
        u32 slot = 0;
        Whole("colour", slot, 0, 1);
        if (!m_error && slot >= colours) {
            const std::string has = colours == 1 ? "colour 0" : std::format("colours 0 to {}", colours - 1);
            Fail(m_map["colour"], std::format("{} takes colour {}, but {} has only {}", m_owner, slot, seal, has));
            return;
        }
        field = static_cast<u8>(slot);
    }

    void Sprite(const char* key, core::atlas::SpriteId& field, const core::Manifest& manifest) {
        if (m_error || !Present(key, true)) {
            return;
        }
        const std::expected<core::atlas::SpriteId, core::Error> sprite = SpriteAt(m_map[key], m_owner, manifest);
        if (!sprite) {
            m_error = sprite.error();
            return;
        }
        field = *sprite;
    }

    void Fail(const YAML::Node& node, std::string_view why) {
        Keep(internal::RefusalAt(WHAT, node, why));
    }

    // Keeps `error` unless an earlier refusal is kept.
    void Keep(const core::Error& error) {
        if (!m_error) {
            m_error = error;
        }
    }

    [[nodiscard]] bool Failed() const {
        return m_error.has_value();
    }

    // The first refusal, if any.
    [[nodiscard]] std::expected<void, core::Error> Result() const {
        if (m_error) {
            return std::unexpected{ *m_error };
        }
        return {};
    }

    [[nodiscard]] const YAML::Node& Map() const {
        return m_map;
    }

    [[nodiscard]] const std::string& Owner() const {
        return m_owner;
    }

private:
    // Whether `key` is there to read; refuses when it is required and absent.
    bool Present(const char* key, bool required) {
        if (m_map[key]) {
            return true;
        }
        if (required) {
            Fail(m_map, std::format("{} has no {}", m_owner, key));
        }
        return false;
    }

    YAML::Node m_map;
    std::string m_owner;
    std::optional<core::Error> m_error = std::nullopt;
};

// What every part of a seal reads against.
struct Context {
    const core::Manifest* manifest = nullptr;
    // The glyph sets' names, in core::Seals::glyphSets' order.
    const std::vector<std::string>* glyphSets = nullptr;
    std::vector<std::string>* warnings = nullptr;
};

// A satellite as written: the seal it names, resolved once every seal is read.
struct Link {
    std::string name;
    YAML::Node node;
    std::size_t target = 0;
};

// A seal as read, before its satellites are checked.
struct ReadSeal {
    std::string name;
    YAML::Node node;
    core::SealLook look;
    std::vector<Link> links;
};

// Warns of each key of `node` that a part of `kind` doesn't take, those of other kinds included.
void NotePartKeys(const YAML::Node& node, std::string_view owner, core::SealPartKind kind, std::vector<std::string>* warnings) {
    using Kind = core::SealPartKind;
    if (kind == Kind::Circle) {
        internal::NoteUnknownKeys(
            WHAT, node, owner, "key", { "kind", "colour", "radius", "rotation", "width", "dash", "gap" }, warnings
        );
    }
    else if (kind == Kind::Ticks) {
        internal::NoteUnknownKeys(
            WHAT, node, owner, "key", { "kind", "colour", "radius", "rotation", "count", "length", "width" }, warnings
        );
    }
    else if (kind == Kind::Dots) {
        internal::NoteUnknownKeys(
            WHAT, node, owner, "key", { "kind", "colour", "radius", "rotation", "count", "size" }, warnings
        );
    }
    else if (kind == Kind::Script) {
        internal::NoteUnknownKeys(
            WHAT, node, owner, "key", { "kind", "colour", "radius", "rotation", "glyphs", "count", "size", "facing" }, warnings
        );
    }
    else if (kind == Kind::Star) {
        internal::NoteUnknownKeys(
            WHAT, node, owner, "key", { "kind", "colour", "radius", "rotation", "points", "step", "width" }, warnings
        );
    }
    else if (kind == Kind::Polygon) {
        internal::NoteUnknownKeys(
            WHAT, node, owner, "key", { "kind", "colour", "radius", "rotation", "sides", "width" }, warnings
        );
    }
    else {
        internal::NoteUnknownKeys(
            WHAT, node, owner, "key", { "kind", "colour", "radius", "rotation", "sprites", "count", "size", "spill" }, warnings
        );
    }
}

// A script's glyph set, by name among `context`'s.
void ReadGlyphs(Fields& fields, const Context& context, core::SealPart& part) {
    fields.Named("facing", FACING_NAMES, part.inward);
    const YAML::Node glyphs = fields.Map()["glyphs"];
    if (fields.Failed()) {
        return;
    }
    if (!glyphs) {
        fields.Fail(fields.Map(), std::format("{} has no glyphs", fields.Owner()));
        return;
    }
    const std::string name = glyphs.IsScalar() ? glyphs.Scalar() : std::string{};
    const auto found = std::ranges::find(*context.glyphSets, name);
    if (found == context.glyphSets->end()) {
        fields.Fail(glyphs, std::format("{} names the glyph set \"{}\", which isn't there", fields.Owner(), name));
        return;
    }
    part.glyphSet = static_cast<u32>(found - context.glyphSets->begin());
}

// A star's points and step, which must be below half its points.
void ReadStar(Fields& fields, core::SealPart& part) {
    fields.Whole("points", part.count, 3, MOST_ITEMS, true);
    fields.Whole("step", part.step, 1, MOST_ITEMS, true);
    if (!fields.Failed() && part.step * 2 >= part.count) {
        fields.Fail(fields.Map()["step"], std::format("{}'s step isn't below half its points", fields.Owner()));
    }
}

// A flourish's spill and its 1 to 3 sprites, repeated in turn to fill all three.
void ReadFlourish(Fields& fields, const Context& context, core::SealPart& part) {
    fields.Read("spill", part.spill, internal::FRACTION);
    const YAML::Node sprites = fields.Map()["sprites"];
    if (fields.Failed()) {
        return;
    }
    if (!sprites || !sprites.IsSequence() || sprites.size() == 0 || sprites.size() > MOST_SPRITES) {
        fields.Fail(
            sprites ? sprites : fields.Map(), std::format("{}'s sprites isn't a list of 1 to 3 sprites", fields.Owner())
        );
        return;
    }
    for (std::size_t index = 0; index < part.sprites.size(); ++index) {
        const std::expected<core::atlas::SpriteId, core::Error> sprite =
            SpriteAt(sprites[index % sprites.size()], fields.Owner(), *context.manifest);
        if (!sprite) {
            fields.Keep(sprite.error());
            return;
        }
        part.sprites.at(index) = *sprite;
    }
}

// The sizes and counts a part of its kind takes besides its kind's own keys.
void ReadShape(Fields& fields, core::SealPart& part) {
    using Kind = core::SealPartKind;
    const Kind kind = part.kind;
    fields.Read("radius", part.radius, SIZE, true);
    fields.Read("rotation", part.rotation, ANGLE);
    if (kind == Kind::Circle || kind == Kind::Ticks || kind == Kind::Star || kind == Kind::Polygon) {
        fields.Read("width", part.width, SIZE, true);
    }
    if (kind == Kind::Dots || kind == Kind::Script || kind == Kind::Flourish) {
        fields.Read("size", part.size, SIZE, true);
    }
    if (kind == Kind::Ticks || kind == Kind::Dots || kind == Kind::Script || kind == Kind::Flourish) {
        fields.Whole("count", part.count, 1, MOST_ITEMS, true);
    }
}

std::expected<core::SealPart, core::Error> ReadPart(
    const YAML::Node& node,
    const std::string& owner,
    const ReadSeal& seal,
    const Context& context
) {
    if (!node.IsMap()) {
        return RefuseSeals(node, std::format("{} isn't a map", owner));
    }
    if (!node["kind"]) {
        return RefuseSeals(node, std::format("{} has no kind", owner));
    }
    core::SealPart part;
    if (const std::expected<void, core::Error> read = internal::ReadNamed(WHAT, node, owner, "kind", PART_NAMES, part.kind);
        !read) {
        return std::unexpected{ read.error() };
    }
    using Kind = core::SealPartKind;
    NotePartKeys(node, owner, part.kind, context.warnings);
    Fields fields(node, owner, {}, nullptr);
    fields.Slot(part.slot, seal.look.colourCount, seal.name);
    ReadShape(fields, part);
    if (part.kind == Kind::Circle) {
        fields.Read("dash", part.dash, ARC);
        fields.Read("gap", part.gap, ARC);
    }
    else if (part.kind == Kind::Ticks) {
        fields.Read("length", part.size, SIZE, true);
    }
    else if (part.kind == Kind::Script) {
        ReadGlyphs(fields, context, part);
    }
    else if (part.kind == Kind::Star) {
        ReadStar(fields, part);
    }
    else if (part.kind == Kind::Polygon) {
        fields.Whole("sides", part.count, 3, MOST_ITEMS, true);
    }
    else if (part.kind == Kind::Flourish) {
        ReadFlourish(fields, context, part);
    }
    return fields.Result().transform([&part] { return part; });
}

std::expected<core::SealLayer, core::Error> ReadLayer(
    const YAML::Node& node,
    std::size_t index,
    const ReadSeal& seal,
    const Context& context
) {
    const std::string owner = std::format("{} layer {}", seal.name, index);
    Fields fields(node, owner, { "inner", "outer", "height", "spin", "opacity", "frame", "parts" }, context.warnings);
    if (const std::expected<void, core::Error> map = fields.Result(); !map) {
        return std::unexpected{ map.error() };
    }
    core::SealLayer layer;
    fields.Read("inner", layer.inner, internal::FRACTION, true);
    fields.Read("outer", layer.outer, SIZE, true);
    fields.Read("height", layer.height, WORLD);
    fields.Read("spin", layer.spin, SPIN);
    fields.Read("opacity", layer.opacity, internal::FRACTION);
    if (!fields.Failed() && layer.inner >= layer.outer) {
        fields.Fail(node, std::format("{}'s inner radius isn't below its outer one", owner));
    }
    if (const YAML::Node frame = node["frame"]; frame && !fields.Failed()) {
        Fields sides(frame, owner + "'s frame", { "sides", "rotation" }, context.warnings);
        sides.Whole("sides", layer.frameSides, 0, MOST_SIDES, true);
        sides.Read("rotation", layer.frameRotation, ANGLE);
        if (!sides.Failed() && layer.frameSides > 0 && layer.frameSides < 3) {
            sides.Fail(frame["sides"], std::format("{}'s frame's sides isn't 0 (round) or from 3 to {}", owner, MOST_SIDES));
        }
        if (const std::expected<void, core::Error> read = sides.Result(); !read) {
            return std::unexpected{ read.error() };
        }
    }
    if (const std::expected<void, core::Error> read = fields.Result(); !read) {
        return std::unexpected{ read.error() };
    }
    if (const YAML::Node parts = node["parts"]) {
        if (!parts.IsSequence() || parts.size() > core::MAX_SEAL_PARTS) {
            return RefuseSeals(parts, std::format("{}'s parts isn't a list of at most {} parts", owner, core::MAX_SEAL_PARTS));
        }
        layer.parts.reserve(parts.size());
        for (std::size_t part = 0; part < parts.size(); ++part) {
            std::expected<core::SealPart, core::Error> read =
                ReadPart(parts[part], std::format("{} part {}", owner, part), seal, context);
            if (!read) {
                return std::unexpected{ read.error() };
            }
            layer.parts.push_back(*read);
        }
    }
    return layer;
}

std::expected<void, core::Error> ReadLayers(const YAML::Node& node, ReadSeal& seal, const Context& context) {
    const YAML::Node layers = node["layers"];
    if (!layers || !layers.IsSequence() || layers.size() == 0 || layers.size() > core::MAX_SEAL_LAYERS) {
        return RefuseSeals(
            layers ? layers : node, std::format("{}'s layers isn't a list of 1 to {} layers", seal.name, core::MAX_SEAL_LAYERS)
        );
    }
    seal.look.layers.reserve(layers.size());
    for (std::size_t index = 0; index < layers.size(); ++index) {
        std::expected<core::SealLayer, core::Error> layer = ReadLayer(layers[index], index, seal, context);
        if (!layer) {
            return std::unexpected{ layer.error() };
        }
        seal.look.layers.push_back(std::move(*layer));
    }
    return {};
}

std::expected<void, core::Error> ReadLight(const YAML::Node& node, ReadSeal& seal, const Context& context) {
    core::SealLight& light = seal.look.light;
    Fields fields(node, seal.name + "'s light", { "colour", "pillars", "sparks", "shards" }, context.warnings);
    if (const std::expected<void, core::Error> map = fields.Result(); !map) {
        return map;
    }
    fields.Slot(light.slot, seal.look.colourCount, seal.name);
    constexpr i64 MOST = core::MAX_SEAL_PARTICLES;
    if (const YAML::Node pillars = node["pillars"]; pillars && !fields.Failed()) {
        Fields stream(
            pillars,
            seal.name + "'s pillar stream",
            { "count", "height", "width", "fade", "cycle", "rest", "band" },
            context.warnings
        );
        stream.Whole("count", light.pillars, 0, MOST);
        stream.Read("height", light.pillarHeight, WORLD_SIZE, light.pillars > 0);
        stream.Read("width", light.pillarWidth, SIZE, light.pillars > 0);
        stream.Read("fade", light.pillarFade, internal::FRACTION, light.pillars > 0);
        stream.Period("cycle", light.pillarCycle, light.pillars > 0);
        stream.Read("rest", light.pillarRest, internal::FRACTION);
        stream.Read("band", light.pillarBand, PILLAR_BAND);
        if (const std::expected<void, core::Error> read = stream.Result(); !read) {
            return read;
        }
    }
    if (const YAML::Node sparks = node["sparks"]; sparks && !fields.Failed()) {
        Fields stream(sparks, seal.name + "'s spark stream", { "count", "rise", "life", "size" }, context.warnings);
        stream.Whole("count", light.sparks, 0, MOST);
        stream.Read("rise", light.sparkRise, WORLD, light.sparks > 0);
        stream.Period("life", light.sparkLife, light.sparks > 0);
        stream.Read("size", light.sparkSize, SIZE, light.sparks > 0);
        if (const std::expected<void, core::Error> read = stream.Result(); !read) {
            return read;
        }
    }
    if (const YAML::Node shards = node["shards"]; shards && !fields.Failed()) {
        Fields stream(
            shards, seal.name + "'s shard stream", { "count", "drift", "spin", "size", "thin", "edge" }, context.warnings
        );
        stream.Whole("count", light.shards, 0, MOST);
        stream.Read("drift", light.shardDrift, WORLD, light.shards > 0);
        stream.Read("spin", light.shardSpin, SPIN, light.shards > 0);
        stream.Read("size", light.shardSize, SIZE, light.shards > 0);
        stream.Read("thin", light.shardThin, RATIO);
        stream.Read("edge", light.shardEdge, RATIO);
        if (const f64 spin = std::abs(static_cast<f64>(light.shardSpin));
            !stream.Failed() && spin > 0.0 && spin < LEAST_SHARD_SPIN) {
            stream.Fail(
                shards["spin"],
                std::format("{}'s spin isn't 0 or at least {} degrees a second either way", stream.Owner(), LEAST_SHARD_SPIN)
            );
        }
        if (const std::expected<void, core::Error> read = stream.Result(); !read) {
            return read;
        }
    }
    if (const u32 particles = light.pillars + light.sparks + light.shards; particles > core::MAX_SEAL_PARTICLES) {
        fields.Fail(node, std::format("{}'s light has {} particles, past {}", seal.name, particles, core::MAX_SEAL_PARTICLES));
    }
    return fields.Result();
}

std::expected<void, core::Error> ReadSatellites(const YAML::Node& node, ReadSeal& seal, const Context& context) {
    if (!node.IsSequence() || node.size() > core::MAX_SEAL_SATELLITES) {
        return RefuseSeals(
            node, std::format("{}'s satellites isn't a list of at most {} satellites", seal.name, core::MAX_SEAL_SATELLITES)
        );
    }
    for (std::size_t index = 0; index < node.size(); ++index) {
        const YAML::Node entry = node[index];
        Fields fields(
            entry,
            std::format("{}'s satellite {}", seal.name, index),
            { "seal", "layer", "angle", "size", "mount", "orbit" },
            context.warnings
        );
        if (const std::expected<void, core::Error> map = fields.Result(); !map) {
            return map;
        }
        core::SealSatellite satellite;
        fields.Whole("layer", satellite.layer, 0, static_cast<i64>(seal.look.layers.size()) - 1);
        fields.Read("angle", satellite.angle, ANGLE);
        fields.Read("size", satellite.size, SATELLITE_SIZE, true);
        fields.Named("mount", MOUNT_NAMES, satellite.mount);
        fields.Read("orbit", satellite.orbit, SPIN);
        const YAML::Node named = entry["seal"];
        if (!fields.Failed() && !named) {
            fields.Fail(entry, std::format("{} has no seal", fields.Owner()));
        }
        if (const std::expected<void, core::Error> read = fields.Result(); !read) {
            return read;
        }
        const std::string name = named.IsScalar() ? named.Scalar() : std::string{};
        if (name == seal.name) {
            return RefuseSeals(named, std::format("{}'s satellite names its own seal", seal.name));
        }
        satellite.seal = core::SealKeyOf(name);
        seal.look.satellites.push_back(satellite);
        seal.links.push_back({ .name = name, .node = named });
    }
    return {};
}

std::expected<void, core::Error> ReadColours(const YAML::Node& node, ReadSeal& seal) {
    core::SealLook& look = seal.look;
    const YAML::Node colours = node["colours"];
    if (!colours || !colours.IsSequence() || colours.size() == 0 || colours.size() > look.colours.size()) {
        return RefuseSeals(colours ? colours : node, std::format("{}'s colours isn't a list of 1 or 2 colours", seal.name));
    }
    look.colourCount = static_cast<u8>(colours.size());
    for (std::size_t slot = 0; slot < colours.size(); ++slot) {
        const std::expected<core::GlowColour, core::Error> colour = internal::ReadGlowColour(WHAT, colours[slot], seal.name);
        if (!colour) {
            return std::unexpected{ colour.error() };
        }
        look.colours.at(slot) = *colour;
    }
    return {};
}

// The look's plane, lift, wash and life: the keys of a seal that are plain values.
std::expected<void, core::Error> ReadPlainKeys(Fields& fields, core::SealLook& look, const Context& context) {
    const std::string& name = fields.Owner();
    fields.Named("plane", PLANE_NAMES, look.plane);
    fields.Read("lift", look.lift, WORLD);
    if (const YAML::Node wash = fields.Map()["wash"]; wash && !fields.Failed()) {
        Fields ink(wash, name + "'s wash", { "darkness", "reach", "softness" }, context.warnings);
        ink.Read("darkness", look.washDarkness, internal::FRACTION);
        ink.Read("reach", look.washReach, WASH_REACH);
        ink.Read("softness", look.washSoftness, internal::FRACTION);
        if (const std::expected<void, core::Error> read = ink.Result(); !read) {
            return read;
        }
    }
    if (const YAML::Node life = fields.Map()["life"]; life && !fields.Failed()) {
        Fields times(life, name + "'s life", { "appear", "appear_curve", "fade", "fade_curve", "order" }, context.warnings);
        times.Read("appear", look.appear, TIME);
        times.Named("appear_curve", internal::CURVE_NAMES, look.appearCurve);
        times.Read("fade", look.fade, TIME);
        times.Named("fade_curve", internal::CURVE_NAMES, look.fadeCurve);
        times.Named("order", ORDER_NAMES, look.innerFirst);
        if (const std::expected<void, core::Error> read = times.Result(); !read) {
            return read;
        }
    }
    return fields.Result();
}

std::expected<void, core::Error> ReadEmblem(const YAML::Node& node, ReadSeal& seal, const Context& context) {
    core::SealEmblem emblem;
    Fields fields(node, seal.name + "'s emblem", { "sprite", "size", "spin", "colour" }, context.warnings);
    fields.Sprite("sprite", emblem.sprite, *context.manifest);
    fields.Read("size", emblem.size, SIZE, true);
    fields.Read("spin", emblem.spin, SPIN);
    fields.Slot(emblem.slot, seal.look.colourCount, seal.name);
    if (const std::expected<void, core::Error> read = fields.Result(); !read) {
        return read;
    }
    seal.look.emblem = emblem;
    return {};
}

std::expected<ReadSeal, core::Error> ReadLook(const std::string& name, const YAML::Node& node, const Context& context) {
    ReadSeal seal{ .name = name, .node = node, .look = {}, .links = {} };
    Fields fields(
        node, name, { "plane", "lift", "wash", "colours", "life", "layers", "emblem", "satellites", "light" }, context.warnings
    );
    // Before any key is looked up: a key of a node that isn't a map is yaml-cpp's error, without the line's reason.
    if (const std::expected<void, core::Error> map = fields.Result(); !map) {
        return std::unexpected{ map.error() };
    }
    std::expected<void, core::Error> read =
        ReadPlainKeys(fields, seal.look, context).and_then([&] { return ReadColours(node, seal); }).and_then([&] {
            return ReadLayers(node, seal, context);
        });
    if (const YAML::Node emblem = node["emblem"]; emblem && read) {
        read = ReadEmblem(emblem, seal, context);
    }
    if (const YAML::Node satellites = node["satellites"]; satellites && read) {
        read = ReadSatellites(satellites, seal, context);
    }
    if (const YAML::Node light = node["light"]; light && read) {
        read = ReadLight(light, seal, context);
    }
    return read.transform([&seal] { return std::move(seal); });
}

std::expected<std::vector<std::vector<core::atlas::SpriteId>>, core::Error> ReadGlyphSets(
    const YAML::Node& node,
    const core::Manifest& manifest,
    std::vector<std::string>& names
) {
    if (!node.IsMap()) {
        return RefuseSeals(node, "glyph_sets isn't a map");
    }
    if (node.size() > core::MAX_SEAL_GLYPH_SETS) {
        return RefuseSeals(node, std::format("glyph_sets has {} sets, past {}", node.size(), core::MAX_SEAL_GLYPH_SETS));
    }
    std::vector<std::vector<core::atlas::SpriteId>> sets;
    for (const auto& entry : node) {
        const auto name = entry.first.as<std::string>();
        const std::string owner = std::format("glyph set {}", name);
        if (!entry.second.IsSequence() || entry.second.size() == 0 || entry.second.size() > core::MAX_GLYPH_SET_SPRITES) {
            return RefuseSeals(
                entry.second, std::format("{} isn't a list of 1 to {} sprites", owner, core::MAX_GLYPH_SET_SPRITES)
            );
        }
        std::vector<core::atlas::SpriteId>& sprites = sets.emplace_back();
        sprites.reserve(entry.second.size());
        for (const YAML::Node& sprite : entry.second) {
            const std::expected<core::atlas::SpriteId, core::Error> read = SpriteAt(sprite, owner, manifest);
            if (!read) {
                return std::unexpected{ read.error() };
            }
            sprites.push_back(*read);
        }
        names.push_back(name);
    }
    return sets;
}

enum class Visit : u8 {
    No,
    Going,
    Done
};

// The deepest a seal's satellites nest under `seal`, all of whose satellites are done (0 with none), or the refusal of a
// nesting deeper than core::MAX_SATELLITE_DEPTH, at the satellite that goes too deep.
std::expected<u32, core::Error> DepthBelow(
    const std::vector<ReadSeal>& seals,
    const std::vector<u32>& depths,
    std::size_t seal
) {
    u32 deepest = 0;
    for (const Link& link : seals.at(seal).links) {
        const u32 depth = 1 + depths.at(link.target);
        if (depth > core::MAX_SATELLITE_DEPTH) {
            return RefuseSeals(
                link.node,
                std::format("{}'s satellites nest {} deep, past {}", seals.at(seal).name, depth, core::MAX_SATELLITE_DEPTH)
            );
        }
        deepest = std::max(deepest, depth);
    }
    return deepest;
}

// Every satellite names a seal of the table, and none forms a loop or nests too deep. A walk depth first with its own
// stack, so a long chain in a broken file can't overflow the call stack: a seal is done once all its satellites are.
std::expected<void, core::Error> CheckSatellites(std::vector<ReadSeal>& seals) {
    for (ReadSeal& seal : seals) {
        for (Link& link : seal.links) {
            const auto found = std::ranges::find(seals, link.name, &ReadSeal::name);
            if (found == seals.end()) {
                return RefuseSeals(
                    link.node, std::format("{}'s satellite names the seal \"{}\", which isn't there", seal.name, link.name)
                );
            }
            link.target = static_cast<std::size_t>(found - seals.begin());
        }
    }
    std::vector<Visit> visits(seals.size(), Visit::No);
    std::vector<u32> depths(seals.size(), 0);
    std::vector<std::pair<std::size_t, std::size_t>> walk; // each seal on the way down, and its next satellite
    for (std::size_t start = 0; start < seals.size(); ++start) {
        if (visits.at(start) != Visit::No) {
            continue;
        }
        visits.at(start) = Visit::Going;
        walk.emplace_back(start, 0);
        while (!walk.empty()) {
            auto& [seal, next] = walk.back();
            const std::vector<Link>& links = seals.at(seal).links;
            if (next < links.size()) {
                const Link& link = links.at(next);
                ++next;
                if (visits.at(link.target) == Visit::Going) {
                    return RefuseSeals(
                        link.node, std::format("{}'s satellite {} forms a loop", seals.at(seal).name, link.name)
                    );
                }
                if (visits.at(link.target) == Visit::No) {
                    visits.at(link.target) = Visit::Going;
                    walk.emplace_back(link.target, 0);
                }
                continue;
            }
            const std::expected<u32, core::Error> depth = DepthBelow(seals, depths, seal);
            if (!depth) {
                return std::unexpected{ depth.error() };
            }
            depths.at(seal) = *depth;
            visits.at(seal) = Visit::Done;
            walk.pop_back();
        }
    }
    return {};
}

std::expected<void, core::Error> ReadSeals(const YAML::Node& node, const Context& context, core::Seals& table) {
    if (!node.IsMap()) {
        return RefuseSeals(node, "seals isn't a map");
    }
    if (node.size() > core::MAX_SEAL_LOOKS) {
        return RefuseSeals(node, std::format("the seals table has {} seals, past {}", node.size(), core::MAX_SEAL_LOOKS));
    }
    std::vector<ReadSeal> seals;
    for (const auto& entry : node) {
        std::expected<ReadSeal, core::Error> seal = ReadLook(entry.first.as<std::string>(), entry.second, context);
        if (!seal) {
            return std::unexpected{ seal.error() };
        }
        seals.push_back(std::move(*seal));
    }
    if (const std::expected<void, core::Error> checked = CheckSatellites(seals); !checked) {
        return checked;
    }
    std::ranges::sort(seals, {}, [](const ReadSeal& seal) { return core::SealKeyOf(seal.name); });
    for (std::size_t index = 1; index < seals.size(); ++index) {
        if (core::SealKeyOf(seals.at(index - 1).name) == core::SealKeyOf(seals.at(index).name)) {
            return RefuseSeals(
                seals.at(index).node,
                std::format("the seals {} and {} share a key; rename one", seals.at(index - 1).name, seals.at(index).name)
            );
        }
    }
    table.looks.reserve(seals.size());
    for (ReadSeal& seal : seals) {
        table.looks.emplace_back(core::SealKeyOf(seal.name), std::move(seal.look));
    }
    return {};
}

} // namespace

std::expected<core::Seals, core::Error> ParseSeals(
    std::string_view text,
    const core::Manifest& manifest,
    std::vector<std::string>* warnings
) {
    try {
        const YAML::Node root = YAML::Load(std::string{ text });
        if (!root.IsMap()) {
            return RefuseSeals(root, "not a map of seals");
        }
        internal::NoteUnknownKeys(
            WHAT,
            root,
            "",
            "section",
            { "view_ratio", "above_opacity", "spin_scale", "brightness", "halo", "glyph_sets", "seals" },
            warnings
        );
        core::Seals table;
        Fields fields(root, "the seals table", {}, nullptr); // its unknown keys are the sections' warnings above
        fields.Read("view_ratio", table.viewRatio, RATIO);
        fields.Read("above_opacity", table.aboveOpacity, internal::FRACTION);
        fields.Read("spin_scale", table.spinScale, SCALE);
        fields.Read("brightness", table.brightness, SCALE);
        if (const std::expected<void, core::Error> read = fields.Result(); !read) {
            return std::unexpected{ read.error() };
        }
        if (const YAML::Node halo = root["halo"]) {
            Fields glow(halo, "the seals table's halo", { "reach", "strength" }, warnings);
            glow.Read("reach", table.haloReach, HALO_REACH);
            glow.Read("strength", table.haloStrength, internal::FRACTION);
            if (const std::expected<void, core::Error> read = glow.Result(); !read) {
                return std::unexpected{ read.error() };
            }
        }
        std::vector<std::string> glyphNames;
        if (const YAML::Node sets = root["glyph_sets"]) {
            std::expected<std::vector<std::vector<core::atlas::SpriteId>>, core::Error> read =
                ReadGlyphSets(sets, manifest, glyphNames);
            if (!read) {
                return std::unexpected{ read.error() };
            }
            table.glyphSets = std::move(*read);
        }
        if (const YAML::Node seals = root["seals"]) {
            const Context context{ .manifest = &manifest, .glyphSets = &glyphNames, .warnings = warnings };
            if (const std::expected<void, core::Error> read = ReadSeals(seals, context, table); !read) {
                return std::unexpected{ read.error() };
            }
        }
        return table;
    }
    catch (const YAML::Exception& error) {
        return std::unexpected{ internal::ErrorOf(WHAT, error) };
    }
}

std::expected<core::Seals, core::Error> LoadSeals(const std::filesystem::path& folder, const core::Manifest& manifest) {
    std::vector<std::string> warnings;
    std::expected<core::Seals, core::Error> seals =
        internal::ReadText(folder / "seals.yaml").and_then([&](const std::string& text) {
            return ParseSeals(text, manifest, &warnings);
        });
    for (const std::string& warning : warnings) {
        PN_LOG_WARNING("{}", warning);
    }
    return seals;
}

} // namespace data
