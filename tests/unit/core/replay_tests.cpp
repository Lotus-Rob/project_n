#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/replay.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 FIRST_HASH = 0xabc;
constexpr u64 SECOND_HASH = 0xdef;
constexpr u64 SECOND_HASH_TICK = 2 * core::REPLAY_HASH_TICKS;

std::vector<std::byte> Start() {
    constexpr u32 STATE = 42;
    std::vector<std::byte> start;
    core::Writer writer{ start };
    writer(STATE);
    return start;
}

// A recording of three ticks (a pause, nothing, a resume and a save) with hashes at two ticks.
std::vector<std::byte> Recorded() {
    core::Replay replay;
    replay.StartRecording(Start());
    const std::array<core::Action, 1> pause{ { { .kind = core::ActionKind::Pause } } };
    const std::array<core::Action, 2> resumeAndSave{ { { .kind = core::ActionKind::Resume },
                                                       { .kind = core::ActionKind::Save } } };
    replay.Record(pause);
    replay.Record({});
    replay.Record(resumeAndSave);
    replay.RecordHash(core::REPLAY_HASH_TICKS, FIRST_HASH);
    replay.RecordHash(SECOND_HASH_TICK, SECOND_HASH);
    return replay.FinishRecording();
}

std::vector<core::ActionKind> Kinds(std::span<const core::Action> actions) {
    std::vector<core::ActionKind> kinds;
    for (const core::Action& action : actions) {
        kinds.push_back(action.kind);
    }
    return kinds;
}

TEST_CASE("a replay plays back its start state and each tick's actions in order") {
    core::Replay replay;
    const auto start = replay.StartPlaying(Recorded());
    REQUIRE(start.has_value());
    CHECK(replay.Playing());
    const std::vector<std::byte> expected = Start();
    const bool sameStart = std::ranges::equal(*start, expected);
    CHECK(sameStart);
    CHECK(Kinds(replay.Next()) == std::vector{ core::ActionKind::Pause });
    CHECK(replay.Next().empty());
    CHECK(Kinds(replay.Next()) == std::vector{ core::ActionKind::Resume, core::ActionKind::Save });
    CHECK(replay.AtEnd());
}

TEST_CASE("a replay's hashes match, mismatch, or aren't there") {
    core::Replay replay;
    const auto started = replay.StartPlaying(Recorded());
    REQUIRE(started.has_value());
    CHECK(replay.HashCount() == 2);
    CHECK(replay.CheckHash(1, FIRST_HASH) == core::HashCheck::None);
    CHECK(replay.CheckHash(core::REPLAY_HASH_TICKS, FIRST_HASH) == core::HashCheck::Match);
    CHECK(replay.CheckHash(SECOND_HASH_TICK, FIRST_HASH) == core::HashCheck::Diverged);
    CHECK(replay.Matched() == 1);
}

TEST_CASE("a truncated replay is refused, and nothing plays") {
    constexpr std::size_t CUT = 3;
    std::vector<std::byte> replay = Recorded();
    replay.resize(replay.size() - CUT);
    core::Replay player;
    const auto started = player.StartPlaying(std::move(replay));
    CHECK_FALSE(started.has_value());
    CHECK_FALSE(player.Playing());
}

TEST_CASE("a replay from another version of the game is refused") {
    constexpr std::size_t VERSION_START = sizeof(u32) + sizeof(u8); // after the format and the version's length
    std::vector<std::byte> replay = Recorded();
    replay[VERSION_START] = std::byte{ '9' };
    core::Replay player;
    const auto started = player.StartPlaying(std::move(replay));
    REQUIRE_FALSE(started.has_value());
    CHECK(started.error().message.find("version") != std::string::npos);
}

TEST_CASE("a replay naming an action that doesn't exist is refused") {
    std::vector<std::byte> replay = Recorded();
    // The first tick's one action: just after the start state and the tick count.
    core::Reader reader{ replay };
    u32 format = 0;
    u8 versionLength = 0;
    reader(format, versionLength);
    const std::size_t startSizeAt = sizeof(u32) + sizeof(u8) + versionLength;
    const std::size_t firstKindAt = startSizeAt + sizeof(u64) + Start().size() + sizeof(u64) + sizeof(u8);
    REQUIRE(replay.at(firstKindAt) == std::byte{ std::to_underlying(core::ActionKind::Pause) });
    replay.at(firstKindAt) = std::byte{ core::ACTION_KINDS };
    core::Replay player;
    const auto started = player.StartPlaying(std::move(replay));
    CHECK_FALSE(started.has_value());
}

TEST_CASE("a replay records at most 255 actions a tick") {
    constexpr std::size_t SENT = 300;
    constexpr std::size_t KEPT = 255;
    core::Replay recording;
    recording.StartRecording(Start());
    const std::vector<core::Action> many(SENT, core::Action{ .kind = core::ActionKind::Pause });
    recording.Record(many);
    core::Replay player;
    const auto started = player.StartPlaying(recording.FinishRecording());
    REQUIRE(started.has_value());
    CHECK(player.Next().size() == KEPT);
}

TEST_CASE("a replay holding a load, which is never recorded, is refused") {
    std::vector<std::byte> replay = Recorded();
    core::Reader reader{ replay };
    u32 format = 0;
    u8 versionLength = 0;
    reader(format, versionLength);
    const std::size_t firstKindAt =
        sizeof(u32) + sizeof(u8) + versionLength + sizeof(u64) + Start().size() + sizeof(u64) + sizeof(u8);
    REQUIRE(replay.at(firstKindAt) == std::byte{ std::to_underlying(core::ActionKind::Pause) });
    replay.at(firstKindAt) = std::byte{ std::to_underlying(core::ActionKind::Load) };
    core::Replay player;
    const auto started = player.StartPlaying(std::move(replay));
    CHECK_FALSE(started.has_value());
}

// Where Recorded()'s fields lie in its bytes.
struct Layout {
    std::size_t startSize = 0; // the start state's byte count
    std::size_t tickCount = 0;
    std::size_t hashCount = 0;
    std::size_t secondHashTick = 0;
};

Layout Locate(std::span<const std::byte> replay) {
    constexpr std::size_t TICK_BYTES = 6; // Recorded()'s ticks: 1 + 1 action, 1 + 0, 1 + 2
    constexpr std::size_t HASH_BYTES = 2 * sizeof(u64);
    const auto versionLength = std::to_integer<std::size_t>(replay[sizeof(u32)]);
    Layout layout;
    layout.startSize = sizeof(u32) + sizeof(u8) + versionLength;
    layout.tickCount = layout.startSize + sizeof(u64) + Start().size();
    layout.hashCount = layout.tickCount + sizeof(u64) + TICK_BYTES;
    layout.secondHashTick = layout.hashCount + sizeof(u64) + HASH_BYTES;
    return layout;
}

void Put(std::vector<std::byte>& bytes, std::size_t offset, u64 value) {
    const auto raw = std::bit_cast<std::array<std::byte, sizeof(u64)>>(value);
    std::ranges::copy(raw, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

// Whether a replay player refuses `replay` and stays stopped.
bool Refused(std::vector<std::byte> replay) {
    core::Replay player;
    const auto started = player.StartPlaying(std::move(replay));
    return !started.has_value() && !player.Playing();
}

constexpr u64 HUGE_COUNT = 1'000'000'000;

TEST_CASE("a replay whose start state runs past its end is refused") {
    std::vector<std::byte> replay = Recorded();
    Put(replay, Locate(replay).startSize, HUGE_COUNT);
    const bool refused = Refused(std::move(replay));
    CHECK(refused);
}

TEST_CASE("a replay claiming more ticks than it holds is refused") {
    std::vector<std::byte> replay = Recorded();
    Put(replay, Locate(replay).tickCount, HUGE_COUNT);
    const bool refused = Refused(std::move(replay));
    CHECK(refused);
}

TEST_CASE("a replay claiming more hashes than it holds is refused") {
    std::vector<std::byte> replay = Recorded();
    Put(replay, Locate(replay).hashCount, HUGE_COUNT);
    const bool refused = Refused(std::move(replay));
    CHECK(refused);
}

TEST_CASE("a replay whose hashes don't go forward in wall ticks is refused") {
    std::vector<std::byte> replay = Recorded();
    Put(replay, Locate(replay).secondHashTick, core::REPLAY_HASH_TICKS);
    const bool refused = Refused(std::move(replay));
    CHECK(refused);
}

TEST_CASE("a replay with bytes after its hashes is refused") {
    constexpr std::byte EXTRA{ 1 };
    std::vector<std::byte> replay = Recorded();
    replay.push_back(EXTRA);
    const bool refused = Refused(std::move(replay));
    CHECK(refused);
}

TEST_CASE("the layout these tests edit is Recorded()'s") {
    const std::vector<std::byte> replay = Recorded();
    const Layout layout = Locate(replay);
    core::Reader reader{ std::span{ replay }.subspan(layout.secondHashTick) };
    u64 secondTick = 0;
    reader(secondTick);
    CHECK(secondTick == SECOND_HASH_TICK);
}

} // namespace
