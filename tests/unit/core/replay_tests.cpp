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
    replay.Record({ .actions = pause });
    replay.Record({});
    replay.Record({ .actions = resumeAndSave });
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
    CHECK(Kinds(replay.Next().actions) == std::vector{ core::ActionKind::Pause });
    CHECK(replay.Next().actions.empty());
    CHECK(Kinds(replay.Next().actions) == std::vector{ core::ActionKind::Resume, core::ActionKind::Save });
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
    recording.Record({ .actions = many });
    core::Replay player;
    const auto started = player.StartPlaying(recording.FinishRecording());
    REQUIRE(started.has_value());
    CHECK(player.Next().actions.size() == KEPT);
}

constexpr core::HeldInput HELD_RIGHT{ .move = { .x = 1.0F, .y = 0.0F } };
constexpr core::HeldInput HELD_UP{ .move = { .x = 0.0F, .y = -0.5F } };

// A recording of held input over four ticks (right, right, up, nothing) and a select in the second.
std::vector<std::byte> RecordedHeld() {
    constexpr core::StableId TARGET{ 7 };
    core::Replay replay;
    replay.StartRecording(Start());
    const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = TARGET } } };
    replay.Record({ .actions = {}, .held = HELD_RIGHT });
    replay.Record({ .actions = select, .held = HELD_RIGHT });
    replay.Record({ .actions = {}, .held = HELD_UP });
    replay.Record({});
    return replay.FinishRecording();
}

TEST_CASE("a replay plays back each tick's held input and a select's target") {
    constexpr core::StableId TARGET{ 7 };
    core::Replay replay;
    const bool started = replay.StartPlaying(RecordedHeld()).has_value();
    REQUIRE(started);
    CHECK(replay.Next().held == HELD_RIGHT);
    const core::TickInput second = replay.Next();
    CHECK(second.held == HELD_RIGHT);
    REQUIRE(second.actions.size() == 1);
    CHECK(second.actions[0].target == TARGET);
    CHECK(replay.Next().held == HELD_UP);
    CHECK(replay.Next().held == core::HeldInput{});
}

TEST_CASE("a replay writes held input only on the ticks it changes") {
    constexpr std::size_t CHANGES = 3; // right, up, nothing
    constexpr std::size_t HELD_BYTES = sizeof(core::HeldInput);
    core::Replay unchanged;
    unchanged.StartRecording(Start());
    for (u32 tick = 0; tick < 4; ++tick) {
        unchanged.Record({});
    }
    constexpr std::size_t SELECT_BYTES = sizeof(u8) + sizeof(core::StableId);
    CHECK(RecordedHeld().size() == unchanged.FinishRecording().size() + (CHANGES * HELD_BYTES) + SELECT_BYTES);
}

// Recorded() with its first action's kind changed to `kind`.
std::vector<std::byte> RecordedWithFirstKind(core::ActionKind kind) {
    std::vector<std::byte> replay = Recorded();
    core::Reader reader{ replay };
    u32 format = 0;
    u8 versionLength = 0;
    reader(format, versionLength);
    // The header, the data hash, the start state's size and bytes, the tick count, the first tick's action count.
    const std::size_t firstKindAt =
        sizeof(u32) + sizeof(u8) + versionLength + (2 * sizeof(u64)) + Start().size() + sizeof(u64) + sizeof(u8);
    REQUIRE(replay.at(firstKindAt) == std::byte{ std::to_underlying(core::ActionKind::Pause) });
    replay.at(firstKindAt) = std::byte{ std::to_underlying(kind) };
    return replay;
}

TEST_CASE("a replay holding a load, new game data or a resident scene, which are never recorded, is refused") {
    for (const core::ActionKind kind :
         { core::ActionKind::Load, core::ActionKind::ReloadData, core::ActionKind::SceneResident }) {
        core::Replay player;
        const auto started = player.StartPlaying(RecordedWithFirstKind(kind));
        CHECK_FALSE(started.has_value());
    }
}

TEST_CASE("a replay plays only under the game data it was recorded with") {
    constexpr u64 DATA_HASH = 0x1234;
    core::Replay recording;
    recording.StartRecording(Start(), DATA_HASH);
    recording.Record({});
    const std::vector<std::byte> replay = recording.FinishRecording();
    core::Replay other;
    const auto underOtherData = other.StartPlaying(replay, DATA_HASH + 1);
    REQUIRE_FALSE(underOtherData.has_value());
    CHECK(underOtherData.error().message.contains("other game data"));
    core::Replay same;
    const auto underSameData = same.StartPlaying(replay, DATA_HASH);
    CHECK(underSameData.has_value());
}

// Where Recorded()'s fields lie in its bytes.
struct Layout {
    std::size_t startSize = 0; // the start state's byte count
    std::size_t tickCount = 0;
    std::size_t hashCount = 0;
    std::size_t secondHashTick = 0;
};

Layout Locate(std::span<const std::byte> replay) {
    constexpr std::size_t TICK_BYTES = 9; // Recorded()'s ticks: 1 + 1 action + 1, 1 + 0 + 1, 1 + 2 + 1
    constexpr std::size_t HASH_BYTES = 2 * sizeof(u64);
    const auto versionLength = std::to_integer<std::size_t>(replay[sizeof(u32)]);
    Layout layout;
    layout.startSize = sizeof(u32) + sizeof(u8) + versionLength + sizeof(u64); // after the data hash
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

TEST_CASE("a replay holding held input out of range is refused") {
    std::vector<std::byte> replay = RecordedHeld();
    const auto versionLength = std::to_integer<std::size_t>(replay[sizeof(u32)]);
    // After the data hash and the start state, the first tick: no actions, the change flag, then x.
    const std::size_t firstX =
        sizeof(u32) + sizeof(u8) + versionLength + (2 * sizeof(u64)) + Start().size() + sizeof(u64) + sizeof(u8) + sizeof(u8);
    const auto raw = std::bit_cast<std::array<std::byte, sizeof(f32)>>(std::numeric_limits<f32>::quiet_NaN());
    f32 recordedX = 0.0F;
    core::Reader reader{ std::span{ replay }.subspan(firstX) };
    reader(recordedX);
    REQUIRE(recordedX == HELD_RIGHT.move.x);
    std::ranges::copy(raw, replay.begin() + static_cast<std::ptrdiff_t>(firstX));
    const bool refused = Refused(std::move(replay));
    CHECK(refused);
}

TEST_CASE("a replay holding a move longer than 1 is refused") {
    constexpr core::HeldInput DIAGONAL{ .move = { .x = 1.0F, .y = 1.0F } }; // each component in range, 1.41 long
    core::Replay recorder;
    recorder.StartRecording(Start());
    recorder.Record({ .actions = {}, .held = DIAGONAL });
    const bool refused = Refused(recorder.FinishRecording());
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

// A recording of one tick holding a cast of `slot` at `point`.
std::vector<std::byte> RecordedCast(core::Slot slot, core::Vec2 point) {
    core::Replay replay;
    replay.StartRecording(Start());
    const std::array<core::Action, 1> cast{ { { .kind = core::ActionKind::Cast, .slot = slot, .point = point } } };
    replay.Record({ .actions = cast });
    return replay.FinishRecording();
}

TEST_CASE("a cast's slot and point play back bit for bit") {
    constexpr core::Vec2 POINT{ .x = -123.456F, .y = 0.1F };
    core::Replay player;
    const auto started = player.StartPlaying(RecordedCast(core::Slot::Special3, POINT));
    REQUIRE(started.has_value());
    const core::TickInput tick = player.Next();
    REQUIRE(tick.actions.size() == 1);
    CHECK(tick.actions[0].kind == core::ActionKind::Cast);
    CHECK(tick.actions[0].slot == core::Slot::Special3);
    CHECK(std::bit_cast<u64>(tick.actions[0].point) == std::bit_cast<u64>(POINT));
}

TEST_CASE("a replay with a cast in a slot past the last, or at a non-finite point, is refused") {
    constexpr core::Vec2 POINT{ .x = 1.0F, .y = 2.0F };
    // The slot byte follows the cast's kind, which is the first tick's first action.
    std::vector<std::byte> past = RecordedCast(core::Slot::Generator, POINT);
    core::Reader reader{ past };
    u32 format = 0;
    u8 versionLength = 0;
    reader(format, versionLength);
    const std::size_t startSizeAt = sizeof(u32) + sizeof(u8) + versionLength;
    // After the version: the data hash, the start state's size and bytes, the tick count and the first count.
    const std::size_t kindAt = startSizeAt + (2 * sizeof(u64)) + Start().size() + sizeof(u64) + sizeof(u8);
    REQUIRE(past.at(kindAt) == std::byte{ std::to_underlying(core::ActionKind::Cast) });
    past.at(kindAt + 1) = std::byte{ core::SLOTS };
    core::Replay pastPlayer;
    const bool pastPlays = pastPlayer.StartPlaying(std::move(past)).has_value(); // named: MSVC's C4866
    CHECK_FALSE(pastPlays);
    core::Replay nanPlayer;
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    const bool nanPlays = nanPlayer.StartPlaying(RecordedCast(core::Slot::Generator, { .x = nan, .y = 0.0F })).has_value();
    CHECK_FALSE(nanPlays);
    core::Replay infinitePlayer;
    const f32 infinity = std::numeric_limits<f32>::infinity();
    const bool infinitePlays =
        infinitePlayer.StartPlaying(RecordedCast(core::Slot::Generator, { .x = 0.0F, .y = infinity })).has_value();
    CHECK_FALSE(infinitePlays);
    core::Replay lastPlayer;
    const bool lastPlays = lastPlayer.StartPlaying(RecordedCast(core::Slot::Ultimate, POINT)).has_value();
    CHECK(lastPlays);
}

} // namespace
