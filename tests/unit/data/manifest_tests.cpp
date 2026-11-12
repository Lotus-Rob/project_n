#include "core/cxx23.hpp" // IWYU pragma: keep
#include "data/manifest.hpp"
#include "support/manifest.hpp"

#include <doctest/doctest.h>

namespace {

using core::atlas::SPRITE_NAMES;

// A valid manifest of every sprite the game has, each a 10x20 image at (32, 32) of one 128-texel page, and two scenes:
// the header on lines 1 to 4, a sprite per line from line 6, the scenes after them.
std::string Valid() {
    std::string text = "page_size: 128\npages: 1\nmip_levels: 5\ngutter: 32\nsprites:\n";
    for (const std::string_view name : SPRITE_NAMES) {
        text +=
            std::format("  - {{ name: {0}, file: sprites/{0}.png, page: 0, region: [32, 32, 10, 20], foot: [5, 20] }}\n", name);
    }
    text +=
        std::format("scenes:\n  - {{ name: first, sprites: [{}] }}\n  - {{ name: second, sprites: [] }}\n", SPRITE_NAMES[0]);
    return text;
}

// Valid() with its first `from` replaced by `replacement`.
std::string With(std::string_view from, std::string_view replacement) {
    std::string text = Valid();
    const std::size_t where = text.find(from);
    REQUIRE(where != std::string::npos);
    return text.replace(where, from.size(), replacement);
}

std::string ErrorFor(const std::string& text) {
    const std::expected<core::Manifest, core::Error> manifest = data::ParseManifest(text);
    REQUIRE_FALSE(manifest.has_value());
    return manifest.error().message;
}

TEST_CASE("the shipped manifest names every sprite the game has, and its scenes") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    CHECK(manifest->sprites.size() == SPRITE_NAMES.size());
    CHECK(manifest->Sprite(core::atlas::SpriteId::Ground).file == "sprites/ground.png");
    const std::optional<core::SceneId> arena = manifest->SceneNamed("arena");
    const std::optional<core::SceneId> nowhere = manifest->SceneNamed("nowhere");
    CHECK(arena.has_value());
    CHECK_FALSE(nowhere.has_value());
}

TEST_CASE("a manifest parses into its sprites and scenes") {
    const std::expected<core::Manifest, core::Error> manifest = data::ParseManifest(Valid());
    REQUIRE(manifest.has_value());
    CHECK(manifest->pageSize == 128);
    CHECK(manifest->gutter == 32);
    CHECK(manifest->Edge() == 16);
    CHECK(manifest->mipLevels == 5);
    const core::SpriteInfo& sprite = manifest->sprites.back();
    CHECK(sprite.x == 32);
    CHECK(sprite.width == 10);
    CHECK(sprite.height == 20);
    CHECK(sprite.footX == 5);
    REQUIRE(manifest->scenes.size() == 2);
    CHECK(manifest->scenes[0].name == "first");
    CHECK(manifest->scenes[0].sprites == std::vector{ core::atlas::SpriteId{} });
    const std::optional<core::SceneId> second = manifest->SceneNamed("second");
    CHECK(second == core::SceneId{ 1 });
}

TEST_CASE("a manifest whose sprites differ from the game's is stale") {
    const std::string renamed = ErrorFor(With(std::format("name: {}", SPRITE_NAMES[1]), "name: other"));
    CHECK(
        renamed == std::format(
                       "manifest line 7: sprite 1 is other, but the game expects {}: the atlas is stale: run "
                       "`./dev atlas`",
                       SPRITE_NAMES[1]
                   )
    );
    std::string missing = Valid();
    const std::size_t lastSprite = missing.rfind("  - { name: " + std::string{ SPRITE_NAMES.back() });
    missing.erase(lastSprite, missing.find('\n', lastSprite) + 1 - lastSprite);
    CHECK(ErrorFor(missing).find("sprites, but the game has") != std::string::npos);
}

TEST_CASE("each refusal names its line") {
    struct Case {
        std::string_view from;
        std::string_view to;
        std::string_view error;
    };
    constexpr std::array<Case, 11> CASES{ {
        Case{ .from = "mip_levels: 5", .to = "mip_levels: 0", .error = "manifest line 3: 0 mip levels, not 2 to 15" },
        Case{ .from = "mip_levels: 5", .to = "mip_levels: 16", .error = "manifest line 3: 16 mip levels, not 2 to 15" },
        Case{ .from = "gutter: 32", .to = "gutter: 30", .error = "manifest line 4: a 30-texel gutter, under 32" },
        Case{ .from = "gutter: 32", .to = "gutter: 33", .error = "manifest line 4: a 33-texel gutter, which is odd" },
        Case{ .from = "pages: 1\n", .to = "", .error = "manifest line 1: no pages" },
        Case{ .from = "page: 0", .to = "page: 1", .error = "is on page 1, past the last" },
        Case{ .from = "region: [32, 32, 10, 20]",
              .to = "region: [120, 32, 10, 20]",
              .error = "region and its 16-texel edge are outside its 128-texel page" },
        Case{ .from = "region: [32, 32, 10, 20]", .to = "region: [16, 16, 10]", .error = "region must be 4 numbers" },
        Case{ .from = "foot: [5, 20]", .to = "foot: [11, 20]", .error = "foot is outside its image" },
        Case{ .from = "sprites: [", .to = "sprites: [nothing, ", .error = "scene first draws nothing, which isn't a sprite" },
        Case{ .from = "page: 0", .to = "page: x", .error = "manifest line 6: bad conversion" },
    } };
    for (const Case& test : CASES) {
        CAPTURE(test.to);
        const std::string error = ErrorFor(With(test.from, test.to));
        CHECK(error.find(test.error) != std::string::npos);
        CHECK(error.starts_with("manifest line "));
    }
}

TEST_CASE("a manifest's gutter is read, and one under two fields' reach or the mips' gutter is refused") {
    const std::expected<core::Manifest, core::Error> odd = data::ParseManifest(With("gutter: 32", "gutter: 31"));
    CHECK_FALSE(odd.has_value());
    // 7 mip levels need 2^6 = 64 texels apart, which a 32-texel gutter can't give.
    const std::expected<core::Manifest, core::Error> deepMips = data::ParseManifest(With("mip_levels: 5", "mip_levels: 7"));
    REQUIRE_FALSE(deepMips.has_value());
    CHECK(deepMips.error().message.find("manifest line 4") != std::string::npos);
    const std::string noGutter = ErrorFor(With("gutter: 32\n", ""));
    CHECK(noGutter.find("no gutter") != std::string::npos);
}

TEST_CASE("a manifest whose sprites leave no room for their gutter is refused") {
    // Each sprite needs its 16-texel edge on every side, inside its 128-texel page.
    const std::expected<core::Manifest, core::Error> oneMip = data::ParseManifest(With("mip_levels: 5", "mip_levels: 1"));
    const std::expected<core::Manifest, core::Error> atLeftEdge =
        data::ParseManifest(With("region: [32, 32, 10, 20]", "region: [8, 32, 10, 20]"));
    const std::expected<core::Manifest, core::Error> pastRightEdge =
        data::ParseManifest(With("region: [32, 32, 10, 20]", "region: [110, 32, 10, 20]"));
    CHECK_FALSE(oneMip.has_value());
    CHECK_FALSE(atLeftEdge.has_value());
    CHECK_FALSE(pastRightEdge.has_value());
}

TEST_CASE("text that isn't YAML is refused") {
    CHECK(ErrorFor("sprites: [").starts_with("manifest line "));
}

TEST_CASE("a scene listing a sprite twice is refused with its line") {
    const std::string twice = std::format("sprites: [{0}, {0}]", SPRITE_NAMES[0]);
    const std::expected<core::Manifest, core::Error> manifest =
        data::ParseManifest(With(std::format("sprites: [{}]", SPRITE_NAMES[0]), twice));
    REQUIRE_FALSE(manifest.has_value());
    CHECK(manifest.error_or(core::Error{}).message.starts_with("manifest line "));
}

} // namespace
