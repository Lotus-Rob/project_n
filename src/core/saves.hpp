#pragma once

/// @file
/// @ingroup core
/// The save system (design doc 0002 §7.6): one save slot on disk, written by a background job so
/// the logic thread never waits on the disk, and read back validated. What goes into a save is
/// the caller's business (core::Logic); this only stores and fetches its bytes.

#include "core/error.hpp"
#include "core/jobs.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <vector>

namespace core {

/// One save slot, written and read through a job system's background job.
class Saves {
public:
    /// Saves to and loads from `slot`, writing on `jobs`, which must outlive it; with an empty
    /// path, writes and reads are refused with a warning (no folder for saves was found).
    Saves(std::filesystem::path slot, Jobs& jobs)
      : m_slot{ std::move(slot) }
      , m_jobs{ &jobs } {
    }
    Saves(const Saves&) = delete;
    Saves& operator=(const Saves&) = delete;
    Saves(Saves&&) = delete;
    Saves& operator=(Saves&&) = delete;
    ~Saves() = default;

    /// Writes `state` to the slot on the background job and returns at once; the outcome is
    /// logged. A write still in progress finishes first. On the thread that created the job system.
    void Write(std::vector<std::byte> state);

    /// Waits for a write in progress, then reads the slot (core::ReadSaveFile). On the thread that
    /// created the job system.
    [[nodiscard]] std::expected<std::vector<std::byte>, Error> Read();

    /// The slot's path; empty when there is none.
    [[nodiscard]] const std::filesystem::path& Slot() const {
        return m_slot;
    }

private:
    std::filesystem::path m_slot;
    Jobs* m_jobs;
};

} // namespace core
