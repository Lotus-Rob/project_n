#pragma once

/// @file
/// @ingroup core
/// Replays (design doc 0002.2, 0002 §7.4): a recording is a start state and then, for every wall
/// tick, the actions that tick took, with a hash of the state every REPLAY_HASH_TICKS wall ticks;
/// playing it back feeds those actions in again and checks the hashes, so a divergence shows
/// within a second of where it starts. Bytes in memory only: reading and writing the file is
/// core::Saves's job, and deciding when is core::Logic's.

#include "core/error.hpp"
#include "core/frame.hpp"
#include "core/types.hpp"

#include <cstddef>
#include <expected>
#include <span>
#include <utility>
#include <vector>

namespace core {

/// A hash of the state is recorded and checked on every wall tick divisible by this: once a second.
inline constexpr u64 REPLAY_HASH_TICKS = 60;

/// What a hash check during playback found.
enum class HashCheck : u8 {
    None,     ///< Nothing was recorded at this tick.
    Match,    ///< The state hashes as it did when recorded.
    Diverged, ///< It doesn't: the game has left the recorded timeline.
};

/// One replay, being recorded, being played, or neither.
class Replay {
public:
    /// Whether a recording is under way.
    [[nodiscard]] bool Recording() const {
        return m_mode == Mode::Recording;
    }

    /// Whether a replay is being played.
    [[nodiscard]] bool Playing() const {
        return m_mode == Mode::Playing;
    }

    /// Starts a recording from `start`, the state as its owner serialised it at this tick (core::Logic
    /// writes its own state, the tick counters and the simulation's); the next Record() is the
    /// following wall tick, under game data hashing to `dataHash` (GameData::Hash). Drops a
    /// recording or playback under way.
    void StartRecording(std::vector<std::byte> start, u64 dataHash = 0);

    /// Records the input of the next wall tick: its actions, in order, at most 255 (any more are
    /// dropped with a warning, and playback diverges from there), and its held input. Never a Load,
    /// ToggleRecording, PlayReplay or ReloadData, which end or replace the recorded timeline, nor a
    /// SceneResident, which is the main thread's and no tick's.
    void Record(const TickInput& input);

    /// Records `hash`, the state's hash after wall tick `wallTick`, which must be after the last one recorded.
    void RecordHash(u64 wallTick, u64 hash);

    /// Ends the recording and returns it as bytes for the replay file, stamped with the game's version.
    [[nodiscard]] std::vector<std::byte> FinishRecording();

    /// Starts playing `replay` (bytes FinishRecording() made) and returns its start state, valid
    /// until playback stops. Fails, changing nothing, on bytes that are malformed (an action that is
    /// never recorded, or held input out of range, included), of another format, from another
    /// version of the game, or recorded under game data that doesn't hash to `dataHash`.
    [[nodiscard]] std::expected<std::span<const std::byte>, Error> StartPlaying(
        std::vector<std::byte> replay,
        u64 dataHash = 0
    );

    /// Whether playback has fed every recorded tick.
    [[nodiscard]] bool AtEnd() const {
        return m_next >= m_tickStarts.size();
    }

    /// The next recorded tick's input; only while playing and not AtEnd(). Its actions stay valid
    /// until the next call.
    [[nodiscard]] TickInput Next();

    /// Compares `hash`, the state's hash after wall tick `wallTick`, with the one recorded there,
    /// if any. Wall ticks checked must increase.
    [[nodiscard]] HashCheck CheckHash(u64 wallTick, u64 hash);

    /// How many hashes the replay being played holds, or the last recording held.
    [[nodiscard]] std::size_t HashCount() const {
        return m_hashes.size();
    }

    /// How many hashes have matched since playback started.
    [[nodiscard]] u64 Matched() const {
        return m_matched;
    }

    /// Stops playback.
    void StopPlaying();

private:
    enum class Mode : u8 {
        Off,
        Recording,
        Playing,
    };

    Mode m_mode = Mode::Off;
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<std::byte> m_bytes{};            // playing: the whole replay; recording: the start state
    std::vector<Action> m_actions{};             // every tick's actions, one after another
    std::vector<std::size_t> m_tickStarts{};     // each tick's first action in m_actions
    std::vector<HeldInput> m_held{};             // each tick's held input
    std::vector<std::pair<u64, u64>> m_hashes{}; // (wall tick, hash), by wall tick
    std::span<const std::byte> m_start{};        // playing: the start state, inside m_bytes
    // NOLINTEND(readability-redundant-member-init)
    u64 m_dataHash = 0;         // recording: the game data's hash
    std::size_t m_next = 0;     // playing: the next tick to feed
    std::size_t m_nextHash = 0; // playing: the next hash to check
    u64 m_matched = 0;
};

} // namespace core
