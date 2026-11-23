#include "data/animations.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "data/internal/yaml.hpp"

namespace data {

namespace {

constexpr std::string_view WHAT = "animations";

// The most world units a frame may show for.
constexpr f64 MOST_STRIDE = 1.0e6;

using Failure = std::unexpected<core::Error>;

/// Whether `node` is a map whose keys are all in `known`; else the refusal for the first that isn't.
std::expected<void, core::Error> OnlyKeys(
    const YAML::Node& node,
    std::string_view owner,
    std::initializer_list<std::string_view> known
) {
    for (const auto& pair : node) {
        const std::string key = pair.first.IsScalar() ? pair.first.Scalar() : std::string{};
        if (std::ranges::find(known, key) == known.end()) {
            return Failure{ internal::RefusalAt(WHAT, pair.first, std::format("{} has no key \"{}\"", owner, key)) };
        }
    }
    return {};
}

/// The first scene holding `standing` that lacks `frame`, if any.
const core::SceneInfo* SceneMissing(
    const core::Manifest& manifest,
    core::atlas::SpriteId standing,
    core::atlas::SpriteId frame
) {
    for (const core::SceneInfo& scene : manifest.scenes) {
        if (std::ranges::contains(scene.sprites, standing) && !std::ranges::contains(scene.sprites, frame)) {
            return &scene;
        }
    }
    return nullptr;
}

std::expected<core::WalkClip, core::Error> ReadWalk(
    const YAML::Node& walk,
    core::atlas::SpriteId standing,
    std::string_view owner,
    const core::Manifest& manifest
) {
    if (!walk.IsMap()) {
        return Failure{ internal::RefusalAt(WHAT, walk, std::format("{}'s walk isn't a map", owner)) };
    }
    const std::string name = std::format("{}'s walk", owner);
    if (const std::expected<void, core::Error> keys = OnlyKeys(walk, name, { "frames", "stride" }); !keys) {
        return Failure{ keys.error() };
    }
    const YAML::Node frames = walk["frames"];
    if (!frames) {
        return Failure{ internal::RefusalAt(WHAT, walk, std::format("{} has no frames", name)) };
    }
    if (!frames.IsSequence() || frames.size() == 0 || frames.size() > core::MAX_WALK_FRAMES) {
        return Failure{
            internal::RefusalAt(WHAT, frames, std::format("{} needs 1 to {} frames in a list", name, core::MAX_WALK_FRAMES))
        };
    }
    core::WalkClip clip;
    clip.standing = standing;
    for (const YAML::Node& item : frames) {
        const std::expected<core::atlas::SpriteId, core::Error> frame = internal::ReadSprite(WHAT, item, name, manifest);
        if (!frame) {
            return Failure{ frame.error() };
        }
        if (const core::SceneInfo* scene = SceneMissing(manifest, standing, *frame)) {
            return Failure{ internal::RefusalAt(
                WHAT,
                item,
                std::format(
                    R"({} has the frame "{}", which the scene "{}" holding "{}" lacks)",
                    name,
                    core::atlas::SPRITE_NAMES.at(std::to_underlying(*frame)),
                    scene->name,
                    owner
                )
            ) };
        }
        clip.frames.push_back(*frame);
    }
    const std::expected<f64, core::Error> stride =
        internal::RequiredNumber(WHAT, walk, name, "stride", { .low = 0.0, .lowOpen = true, .high = MOST_STRIDE });
    if (!stride) {
        return Failure{ stride.error() };
    }
    clip.stride = static_cast<f32>(*stride);
    if (!std::isnormal(clip.stride)) {
        return Failure{ internal::RefusalAt(WHAT, walk["stride"], std::format("{}'s stride is too small for an f32", name)) };
    }
    return clip;
}

} // namespace

std::expected<core::Animations, core::Error> ParseAnimations(std::string_view yaml, const core::Manifest& manifest) {
    try {
        const YAML::Node root = YAML::Load(std::string{ yaml });
        core::Animations table;
        if (root.IsNull()) {
            return table;
        }
        if (!root.IsMap()) {
            return Failure{ internal::RefusalAt(WHAT, root, "not a map of standing sprites") };
        }
        for (const auto& pair : root) {
            const std::string owner = pair.first.IsScalar() ? pair.first.Scalar() : std::string{};
            const std::expected<core::atlas::SpriteId, core::Error> standing =
                internal::ReadSprite(WHAT, pair.first, "the table", manifest);
            if (!standing) {
                return Failure{ standing.error() };
            }
            const YAML::Node& entry = pair.second;
            if (!entry.IsMap()) {
                return Failure{ internal::RefusalAt(WHAT, entry, std::format("{} isn't a map", owner)) };
            }
            if (const std::expected<void, core::Error> keys = OnlyKeys(entry, owner, { "walk" }); !keys) {
                return Failure{ keys.error() };
            }
            const YAML::Node walk = entry["walk"];
            if (!walk) {
                return Failure{ internal::RefusalAt(WHAT, entry, std::format("{} has no walk", owner)) };
            }
            if (table.WalkOf(*standing) != nullptr) {
                return Failure{ internal::RefusalAt(WHAT, pair.first, std::format("{} has two entries", owner)) };
            }
            std::expected<core::WalkClip, core::Error> clip = ReadWalk(walk, *standing, owner, manifest);
            if (!clip) {
                return Failure{ clip.error() };
            }
            table.walks.push_back(std::move(*clip));
        }
        return table;
    }
    catch (const YAML::Exception& error) {
        return Failure{ internal::ErrorOf(WHAT, error) };
    }
}

std::expected<core::Animations, core::Error> LoadAnimations(
    const std::filesystem::path& folder,
    const core::Manifest& manifest
) {
    return internal::ReadText(folder / "animations.yaml").and_then([&](const std::string& text) {
        return ParseAnimations(text, manifest);
    });
}

} // namespace data
