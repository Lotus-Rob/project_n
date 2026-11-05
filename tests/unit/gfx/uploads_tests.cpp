#include "core/cxx23.hpp" // IWYU pragma: keep
#include "gfx/uploads.hpp"

#include <doctest/doctest.h>

namespace {

constexpr gfx::SnapshotId SNAPSHOT{ .epoch = 1, .wallTick = 7 };

TEST_CASE("an upload whose commands weren't submitted is uploaded again") {
    gfx::UploadedSnapshot uploaded;
    uploaded.Recorded(SNAPSHOT);
    uploaded.Submitted(false);
    CHECK_FALSE(uploaded.Holds(SNAPSHOT));
}

TEST_CASE("an upload counts once its commands are submitted") {
    gfx::UploadedSnapshot uploaded;
    uploaded.Recorded(SNAPSHOT);
    uploaded.Submitted(true);
    CHECK(uploaded.Holds(SNAPSHOT));
}

TEST_CASE("a capture whose upload failed is cancelled and fails with the upload's error") {
    bool cancelled = false;
    bool drawn = false;
    const std::expected<i32, core::Error> captured = gfx::AfterUpload(
        std::unexpected{ core::Error{ "no upload" } },
        [&cancelled] { cancelled = true; },
        [&drawn]() -> std::expected<i32, core::Error> {
            drawn = true;
            return 0;
        }
    );
    REQUIRE_FALSE(captured);
    CHECK(captured.error().message == "no upload");
    CHECK(cancelled);
    CHECK_FALSE(drawn);
}

} // namespace
