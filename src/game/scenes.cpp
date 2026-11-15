#include "game/scenes.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "game/scenes/arena.hpp"
#include "game/scenes/cells.hpp"
#include "game/scenes/painted.hpp"
#include "game/scenes/stress.hpp"
#include "game/scenes/world.hpp"

namespace game {

namespace {

constexpr ArenaScene ARENA_INSTANCE{};
constexpr StressScene STRESS_INSTANCE{};
constexpr PaintedScene PAINTED_INSTANCE{};
constexpr CellsScene CELLS_INSTANCE{};
constexpr WorldScene WORLD_INSTANCE{};

// F2's order, each at its key's place.
constexpr auto SCENE_LIST =
    std::to_array<const Scene*>({ &ARENA_INSTANCE, &STRESS_INSTANCE, &PAINTED_INSTANCE, &CELLS_INSTANCE, &WORLD_INSTANCE });

// Each key constant names its scene's place, and every listed scene has one.
constexpr bool KeyedAt(core::SceneKey key, const Scene* scene) {
    return std::to_underlying(key) < SCENE_LIST.size() && SCENE_LIST.at(std::to_underlying(key)) == scene;
}
static_assert(KeyedAt(ARENA_SCENE, &ARENA_INSTANCE));
static_assert(KeyedAt(STRESS_SCENE, &STRESS_INSTANCE));
static_assert(KeyedAt(PAINTED_SCENE, &PAINTED_INSTANCE));
static_assert(KeyedAt(CELLS_SCENE, &CELLS_INSTANCE));
static_assert(KeyedAt(WORLD_SCENE, &WORLD_INSTANCE));
static_assert(
    SCENE_LIST.size() == std::to_underlying(WORLD_SCENE) + 1U,
    "a listed scene without its key constant in game/scenes.hpp"
);
static_assert(SCENE_LIST.size() <= std::numeric_limits<u8>::max() + 1U, "a save stores the scene's key in one byte");

} // namespace

std::span<const Scene* const> Scenes() {
    return SCENE_LIST;
}

const Scene& SceneOf(core::SceneKey key) {
    const std::size_t index = std::to_underlying(key);
    PN_ASSERT(index < SCENE_LIST.size(), "a scene key that is no listed scene's");
    return *Scenes()[index < SCENE_LIST.size() ? index : 0];
}

std::optional<core::SceneKey> SceneNamed(std::string_view name) {
    const std::span<const Scene* const> scenes = Scenes();
    const auto found = std::ranges::find(scenes, name, [](const Scene* scene) { return scene->Traits().name; });
    if (found == scenes.end()) {
        return std::nullopt;
    }
    return core::SceneKey{ static_cast<u16>(found - scenes.begin()) };
}

std::string_view SceneName(core::SceneKey key) {
    return SceneOf(key).Traits().name;
}

core::SceneKey NextScene(core::SceneKey key) {
    return core::SceneKey{ static_cast<u16>((std::to_underlying(key) + 1U) % SCENE_LIST.size()) };
}

core::SceneKey NextWalkableScene(core::SceneKey key) {
    core::SceneKey next = NextScene(key);
    while (next != key && !SceneOf(next).Traits().portal && !SceneOf(next).Traits().portalFromMap) {
        next = NextScene(next);
    }
    return next;
}

std::expected<void, core::Error> CheckScenes(const core::Manifest& manifest, const core::GameData& data) {
    for (const Scene* const entry : Scenes()) {
        const SceneTraits& traits = entry->Traits();
        const std::optional<core::SceneId> scene = manifest.SceneNamed(traits.name);
        if (!scene) {
            return std::unexpected{ core::Error{ std::format("the atlas has no scene {}: run `./dev atlas`", traits.name) } };
        }
        const std::vector<core::atlas::SpriteId>& sprites = manifest.Scene(*scene).sprites;
        if (traits.worldMap && data.world == nullptr) {
            return std::unexpected{ core::Error{ std::format(
                "the scene {} needs the world map (assets/data/world.yaml), which the game data lacks", traits.name
            ) } };
        }
        for (const core::atlas::SpriteId sprite : { data.cultivator.sprite, data.fox.sprite }) {
            if (traits.creatures && !std::ranges::contains(sprites, sprite)) {
                return std::unexpected{ core::Error{ std::format(
                    "the scene {} doesn't load {}, which the creatures data draws: add it to art/scenes.toml and run "
                    "`./dev atlas`",
                    traits.name,
                    core::atlas::SPRITE_NAMES.at(std::to_underlying(sprite))
                ) } };
            }
        }
    }
    return {};
}

} // namespace game
