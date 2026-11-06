#include "core/replay.hpp"

#include "core/archive.hpp"
#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"

namespace core {

namespace {

constexpr u32 REPLAY_FORMAT = 1;
constexpr std::string_view VERSION = PN_GAME_VERSION; // from xmake.lua's set_version
constexpr std::size_t MAX_ACTIONS_PER_TICK = 255;     // a tick's count is a byte

// Actions that end or replace a recording's timeline, so none is ever recorded.
bool NeverRecorded(ActionKind kind) {
    return kind == ActionKind::Load || kind == ActionKind::ToggleRecording || kind == ActionKind::PlayReplay;
}

// Reads the recorded ticks into `actions`, one after another, and each tick's first action's index into
// `tickStarts`; fails `archive` on a tick that doesn't fit or an action that can't be in a replay.
void ReadTicks(Reader& archive, std::vector<Action>& actions, std::vector<std::size_t>& tickStarts) {
    u64 ticks = 0;
    archive(ticks);
    if (ticks > archive.Remaining()) {
        archive.Fail();
    }
    tickStarts.reserve(archive.Failed() ? 0 : static_cast<std::size_t>(ticks));
    for (u64 tick = 0; tick < ticks && !archive.Failed(); ++tick) {
        tickStarts.push_back(actions.size());
        u8 count = 0;
        archive(count);
        for (u8 action = 0; action < count && !archive.Failed(); ++action) {
            u8 kind = 0;
            archive(kind);
            if (kind >= ACTION_KINDS || NeverRecorded(static_cast<ActionKind>(kind))) {
                archive.Fail();
            }
            actions.push_back({ .kind = static_cast<ActionKind>(kind) });
        }
    }
}

} // namespace

void Replay::StartRecording(std::vector<std::byte> start) {
    m_mode = Mode::Recording;
    m_bytes = std::move(start);
    m_actions.clear();
    m_tickStarts.clear();
    m_hashes.clear();
}

void Replay::Record(std::span<const Action> actions) {
    PN_PROFILE_ZONE();
    PN_ASSERT(Recording(), "a tick recorded with no recording under way");
    PN_ASSERT(
        std::ranges::none_of(actions, [](const Action& action) { return NeverRecorded(action.kind); }),
        "a replay control or a load recorded"
    );
    m_tickStarts.push_back(m_actions.size());
    const std::size_t count = std::min(actions.size(), MAX_ACTIONS_PER_TICK);
    if (count < actions.size()) {
        // Playback will diverge from here; the log says why.
        PN_LOG_WARNING(
            "a replay records at most {} actions a tick; {} were dropped", MAX_ACTIONS_PER_TICK, actions.size() - count
        );
    }
    m_actions.insert(m_actions.end(), actions.begin(), actions.begin() + static_cast<std::ptrdiff_t>(count));
}

void Replay::RecordHash(u64 wallTick, u64 hash) {
    PN_ASSERT(Recording(), "a hash recorded with no recording under way");
    PN_ASSERT(m_hashes.empty() || m_hashes.back().first < wallTick, "replay hashes recorded out of order");
    m_hashes.emplace_back(wallTick, hash);
}

std::vector<std::byte> Replay::FinishRecording() {
    PN_ASSERT(Recording(), "a recording finished with none under way");
    std::vector<std::byte> replay;
    Writer archive{ replay };
    archive(REPLAY_FORMAT, static_cast<u8>(VERSION.size()));
    for (const char letter : VERSION) {
        archive(letter);
    }
    archive(u64{ m_bytes.size() });
    replay.insert(replay.end(), m_bytes.begin(), m_bytes.end());
    archive(u64{ m_tickStarts.size() });
    for (std::size_t tick = 0; tick < m_tickStarts.size(); ++tick) {
        const std::size_t end = tick + 1 < m_tickStarts.size() ? m_tickStarts[tick + 1] : m_actions.size();
        archive(static_cast<u8>(end - m_tickStarts[tick]));
        for (std::size_t action = m_tickStarts[tick]; action < end; ++action) {
            archive(m_actions[action].kind);
        }
    }
    archive(u64{ m_hashes.size() });
    for (const auto& [wallTick, hash] : m_hashes) {
        archive(wallTick, hash);
    }
    m_mode = Mode::Off;
    return replay;
}

std::expected<std::span<const std::byte>, Error> Replay::StartPlaying(std::vector<std::byte> replay) {
    Reader archive{ replay };
    u32 format = 0;
    u8 versionLength = 0;
    archive(format, versionLength);
    if (archive.Failed() || format != REPLAY_FORMAT) {
        return std::unexpected{ Error{ .message = std::format("not a replay of format {}", REPLAY_FORMAT) } };
    }
    std::string version(versionLength, ' ');
    for (char& letter : version) {
        archive(letter);
    }
    if (!archive.Failed() && version != VERSION) {
        return std::unexpected{ Error{ .message =
                                           std::format("recorded by version {} of the game, not {}", version, VERSION) } };
    }
    u64 startBytes = 0;
    archive(startBytes);
    if (archive.Failed() || startBytes > archive.Remaining()) {
        return std::unexpected{ Error{ .message = "a broken replay: its start state is cut short" } };
    }
    const std::size_t startOffset = replay.size() - archive.Remaining();
    archive.Skip(static_cast<std::size_t>(startBytes));

    std::vector<Action> actions;
    std::vector<std::size_t> tickStarts;
    ReadTicks(archive, actions, tickStarts);
    std::vector<std::pair<u64, u64>> hashes;
    u64 hashCount = 0;
    archive(hashCount);
    if (hashCount > archive.Remaining() / (2 * sizeof(u64))) {
        archive.Fail();
    }
    for (u64 i = 0; i < hashCount && !archive.Failed(); ++i) {
        u64 wallTick = 0;
        u64 hash = 0;
        archive(wallTick, hash);
        if (!hashes.empty() && hashes.back().first >= wallTick) {
            archive.Fail();
        }
        hashes.emplace_back(wallTick, hash);
    }
    if (archive.Failed() || archive.Remaining() != 0) {
        return std::unexpected{ Error{ .message = "a broken replay: its ticks or hashes don't add up" } };
    }

    m_mode = Mode::Playing;
    m_bytes = std::move(replay);
    m_start = std::span<const std::byte>{ m_bytes }.subspan(startOffset, static_cast<std::size_t>(startBytes));
    m_actions = std::move(actions);
    m_tickStarts = std::move(tickStarts);
    m_hashes = std::move(hashes);
    m_next = 0;
    m_nextHash = 0;
    m_matched = 0;
    return m_start;
}

std::span<const Action> Replay::Next() {
    PN_PROFILE_ZONE();
    PN_ASSERT(Playing() && !AtEnd(), "a replay tick fed with none left");
    const std::size_t begin = m_tickStarts[m_next];
    const std::size_t end = m_next + 1 < m_tickStarts.size() ? m_tickStarts[m_next + 1] : m_actions.size();
    ++m_next;
    return std::span<const Action>{ m_actions }.subspan(begin, end - begin);
}

HashCheck Replay::CheckHash(u64 wallTick, u64 hash) {
    PN_PROFILE_ZONE();
    while (m_nextHash < m_hashes.size() && m_hashes[m_nextHash].first < wallTick) {
        ++m_nextHash; // a hash recorded at a tick playback didn't check; can't happen with the same timeline
    }
    if (m_nextHash == m_hashes.size() || m_hashes[m_nextHash].first != wallTick) {
        return HashCheck::None;
    }
    const bool matches = m_hashes[m_nextHash++].second == hash;
    m_matched += matches ? 1 : 0;
    return matches ? HashCheck::Match : HashCheck::Diverged;
}

void Replay::StopPlaying() {
    m_mode = Mode::Off;
    m_start = {};
}

} // namespace core
