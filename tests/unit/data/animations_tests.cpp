#include "core/animations.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/manifest.hpp"
#include "data/animations.hpp"
#include "platform/paths.hpp"
#include "support/manifest.hpp"

#include <doctest/doctest.h>

namespace {

using core::atlas::SpriteId;

// A walk of `frames` villager frames, its stride `stride`.
std::string VillagerWalk(std::string_view frames, std::string_view stride) {
    return std::format("villager:\n  walk:\n    frames: {}\n    stride: {}\n", frames, stride);
}

// A list of `count` frames.
std::string FrameList(std::size_t count) {
    std::string list = "[";
    for (std::size_t frame = 0; frame < count; ++frame) {
        list += frame == 0 ? "villager_walk_0" : ", villager_walk_0";
    }
    return list + "]";
}

core::Error ErrorFor(std::string_view text, const core::Manifest& manifest) {
    const std::expected<core::Animations, core::Error> table = data::ParseAnimations(text, manifest);
    REQUIRE_FALSE(table.has_value());
    return table.error();
}

TEST_CASE("the shipped animations read") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const std::expected<core::Animations, core::Error> table =
        platform::AssetsDirectory().and_then([&](const std::filesystem::path& assets) {
            return data::LoadAnimations(assets / "data", *manifest);
        });
    REQUIRE(table.has_value());
    const core::WalkClip* villager = table->WalkOf(SpriteId::Villager);
    const core::WalkClip* cultivator = table->WalkOf(SpriteId::Cultivator);
    REQUIRE(villager != nullptr);
    REQUIRE(cultivator != nullptr);
    CHECK(villager->frames.size() == 8);
    CHECK(cultivator->frames.size() == 8);
    CHECK(villager->frames.front() == SpriteId::VillagerWalk0);
    CHECK(cultivator->frames.back() == SpriteId::CultivatorWalk7);
    CHECK(villager->stride == 12.0F);
    CHECK(cultivator->stride == 14.0F);
}

TEST_CASE("an empty table reads as no walks") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    for (const std::string_view text : { "", "# only a comment\n", "{}\n" }) {
        CAPTURE(text);
        const std::expected<core::Animations, core::Error> table = data::ParseAnimations(text, *manifest);
        REQUIRE(table.has_value());
        CHECK(table->walks.empty());
    }
}

TEST_CASE("the reader refuses a bad table, naming the line") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    // The manifest's scenes hold both sprites, so a frame the manifest has is also in each scene holding the figure.
    core::Manifest narrow = *manifest;
    narrow.scenes = { core::SceneInfo{ .name = "narrow", .sprites = { SpriteId::Villager, SpriteId::VillagerWalk0 } } };
    struct Broken {
        std::string_view why;
        std::string text;
        const core::Manifest* against;
        std::string_view says;
    };
    const std::string one = "[villager_walk_0]";
    const std::string missingStride = "villager:\n  walk:\n    frames: [villager_walk_0]\n";
    const std::array<Broken, 14> cases{ {
        { .why = "an unknown entry key",
          .text = "villager:\n  run: { frames: [villager_walk_0], stride: 1 }\n",
          .against = manifest.get(),
          .says = "animations line 2: villager has no key \"run\"" },
        { .why = "an unknown walk key",
          .text = "villager:\n  walk:\n    frames: [villager_walk_0]\n    stride: 1\n    speed: 2\n",
          .against = manifest.get(),
          .says = "animations line 5: villager's walk has no key \"speed\"" },
        { .why = "a standing sprite the manifest lacks",
          .text = "nobody:\n  walk: { frames: [villager_walk_0], stride: 1 }\n",
          .against = manifest.get(),
          .says = "animations line 1: the table names the sprite \"nobody\", which isn't in the atlas" },
        { .why = "a frame the manifest lacks",
          .text = "villager:\n  walk: { frames: [villager_walk_0, nothing], stride: 1 }\n",
          .against = manifest.get(),
          .says = "animations line 2: villager's walk names the sprite \"nothing\", which isn't in the atlas" },
        { .why = "a frame a scene holding the standing sprite lacks",
          .text = VillagerWalk("[villager_walk_0, villager_walk_3]", "1"),
          .against = &narrow,
          .says = "animations line 3: villager's walk has the frame \"villager_walk_3\", which the scene \"narrow\" "
                  "holding \"villager\" lacks" },
        { .why = "no frames",
          .text = VillagerWalk("[]", "1"),
          .against = manifest.get(),
          .says = "animations line 3: villager's walk needs 1 to 32 frames in a list" },
        { .why = "33 frames",
          .text = VillagerWalk(FrameList(33), "1"),
          .against = manifest.get(),
          .says = "animations line 3: villager's walk needs 1 to 32 frames in a list" },
        { .why = "a stride of 0", .text = VillagerWalk(one, "0"), .against = manifest.get(), .says = "animations line 4:" },
        { .why = "a negative stride",
          .text = VillagerWalk(one, "-1"),
          .against = manifest.get(),
          .says = "animations line 4:" },
        { .why = "a stride that isn't a number",
          .text = VillagerWalk(one, ".nan"),
          .against = manifest.get(),
          .says = "animations line 4:" },
        { .why = "a stride too small for an f32",
          .text = VillagerWalk(one, "1e-50"),
          .against = manifest.get(),
          .says = "animations line 4:" },
        { .why = "a stride above the cap",
          .text = VillagerWalk(one, "1e7"),
          .against = manifest.get(),
          .says = "animations line 4:" },
        { .why = "a standing sprite given twice",
          .text = "villager:\n  walk: { frames: [villager_walk_0], stride: 1 }\nvillager:\n"
                  "  walk: { frames: [villager_walk_0], stride: 2 }\n",
          .against = manifest.get(),
          .says = "animations line 3: villager has two entries" },
        { .why = "no stride",
          .text = missingStride,
          .against = manifest.get(),
          .says = "animations line 3: villager's walk has no stride" },
    } };
    for (const Broken& broken : cases) {
        CAPTURE(broken.why);
        const core::Error error = ErrorFor(broken.text, *broken.against);
        CHECK(error.message.starts_with(broken.says));
    }
    // The cap itself is allowed.
    CHECK(data::ParseAnimations(VillagerWalk(FrameList(32), "1"), *manifest).has_value());
}

} // namespace
