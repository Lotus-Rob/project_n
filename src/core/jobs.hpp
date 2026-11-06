#pragma once

/// @file
/// @ingroup core
/// The fork-join job system (design doc 0002 §7.5): a parallel-for over fixed-size chunks, run by
/// worker threads and the thread that forks, which joins before it goes on. enkiTS behind our names.

#include "core/types.hpp"

#include <cstddef>
#include <functional>
#include <memory>

namespace core {

/// How many fixed-size chunks of `chunkSize` items cover `items`; the last may be short.
[[nodiscard]] constexpr u32 ChunkCount(std::size_t items, u32 chunkSize) {
    return static_cast<u32>((items + chunkSize - 1) / chunkSize);
}

/// A pool of worker threads that runs parallel-fors at high priority. Only the thread that creates
/// it may fork (the logic thread, which works on chunks too while it waits); its joins wait only on
/// high-priority work, never on a low-priority job such as an image decode. A thread has one at a
/// time: enkiTS keeps a thread's number in a thread_local that every scheduler shares.
class Jobs {
public:
    /// Starts `workers` worker threads (at least one).
    explicit Jobs(u32 workers = DefaultWorkers());
    Jobs(const Jobs&) = delete;
    Jobs& operator=(const Jobs&) = delete;
    Jobs(Jobs&&) = delete;
    Jobs& operator=(Jobs&&) = delete;
    /// Stops the workers once they're idle.
    ~Jobs();

    /// The hardware threads minus two (the main and logic threads), at least one.
    [[nodiscard]] static u32 DefaultWorkers();

    /// How many worker threads it runs, besides the forking thread.
    [[nodiscard]] u32 Workers() const;

    /// Calls `body(chunk)` once for every chunk in [0, `chunks`), on the workers and this thread,
    /// and returns when every call has. A chunk is the unit of determinism: its work and results
    /// must not depend on which thread runs it or when, so the outcome is the same with 1 worker or 32.
    template<typename Body>
    void ParallelFor(u32 chunks, const Body& body) {
        Run(chunks, [](const void* callable, u32 chunk) { (*static_cast<const Body*>(callable))(chunk); }, &body);
    }

    /// Starts `work` on a worker at low priority and returns at once: for work that takes long and
    /// must never hold up a parallel-for's join (writing a save). One runs at a time: starting
    /// another waits for the last first. The destructor waits for one still running. Only the
    /// thread that created the job system starts or waits for one.
    void Background(std::function<void()> work);

    /// Waits until the background job started last has finished; returns at once when none runs.
    void WaitForBackground();

private:
    void Run(u32 chunks, void (*call)(const void* callable, u32 chunk), const void* callable);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace core
