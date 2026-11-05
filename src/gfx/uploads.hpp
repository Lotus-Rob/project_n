#pragma once

/// @file
/// @ingroup gfx
/// Which snapshot's instances the GPU holds, so a snapshot drawn again isn't uploaded again.

#include "core/error.hpp"
#include "core/types.hpp"

#include <expected>
#include <optional>
#include <utility>

namespace gfx {

/// A snapshot as the renderer tells snapshots apart: wall ticks never repeat within an epoch.
struct SnapshotId {
    /// Its timeline.
    u64 epoch = 0;
    /// Its wall tick.
    u64 wallTick = 0;

    /// The same snapshot.
    friend bool operator==(const SnapshotId&, const SnapshotId&) = default;
};

/// Which snapshot's instances are in the storage buffer (design doc 0002 §8.3). A recorded copy counts only once its
/// command buffer is submitted, so a frame that fails to submit is uploaded again.
class UploadedSnapshot {
public:
    /// Whether `snapshot`'s instances are in the storage buffer, so drawing it needs no upload.
    [[nodiscard]] bool Holds(SnapshotId snapshot) const {
        return m_held == snapshot;
    }

    /// A copy of `snapshot`'s instances was recorded into a command buffer not yet submitted.
    void Recorded(SnapshotId snapshot) {
        m_recorded = snapshot;
    }

    /// The command buffer of the last Recorded() copy was `submitted`, or failed to be.
    void Submitted(bool submitted) {
        if (submitted && m_recorded) {
            m_held = m_recorded;
        }
        m_recorded.reset();
    }

private:
    std::optional<SnapshotId> m_held = std::nullopt;
    std::optional<SnapshotId> m_recorded = std::nullopt;
};

/// A capture's steps after recording its upload: `rest()` only if the upload was `copied`; otherwise `cancel()`s the
/// capture's command buffer and returns the upload's error, not a picture of the backdrop alone.
template<typename Cancel, typename Rest>
auto AfterUpload(std::expected<void, core::Error> copied, Cancel cancel, Rest rest) -> decltype(rest()) {
    if (!copied) {
        cancel();
        return std::unexpected{ std::move(copied.error()) };
    }
    return rest();
}

} // namespace gfx
