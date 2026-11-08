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
/// it, or one registered with it (ThreadRegistration), may fork, and works on chunks too while it
/// waits; its joins wait only on high-priority work, never on a low-priority job such as an image
/// decode. A thread has one at a time: enkiTS keeps a thread's number in a thread_local that every
/// scheduler shares.
class Jobs {
public:
    /// Low-priority chunks started by Start(), owned until they have all run.
    class Pending {
    public:
        /// An empty Pending, owning no chunks: Done() is true.
        Pending();
        Pending(const Pending&) = delete;
        Pending& operator=(const Pending&) = delete;
        /// Takes over `other`'s chunks; `other` is then done.
        Pending(Pending&& other) noexcept;
        Pending& operator=(Pending&&) = delete;
        /// Waits for every chunk, working on chunks meanwhile.
        ~Pending();

        /// Whether every chunk has run; what they wrote is then visible to the caller.
        [[nodiscard]] bool Done() const;

    private:
        friend class Jobs;
        struct Task;
        Jobs* m_jobs = nullptr;
        std::unique_ptr<Task> m_task;
    };

    /// Registers the calling thread to fork and start work while it lives (one of the external
    /// threads the job system was built for), and deregisters it when it goes.
    class ThreadRegistration {
    public:
        /// Registers the calling thread with `jobs`, which must outlive it.
        explicit ThreadRegistration(Jobs& jobs);
        ThreadRegistration(const ThreadRegistration&) = delete;
        ThreadRegistration& operator=(const ThreadRegistration&) = delete;
        ThreadRegistration(ThreadRegistration&&) = delete;
        ThreadRegistration& operator=(ThreadRegistration&&) = delete;
        /// Deregisters the thread.
        ~ThreadRegistration();

    private:
        Jobs* m_jobs;
    };

    /// Starts `workers` worker threads (at least one), with room for `externalThreads` other
    /// threads to register besides the creating one.
    explicit Jobs(u32 workers = DefaultWorkers(), u32 externalThreads = 0);
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

    /// Calls `body(chunk)` once for every chunk in [0, `chunks`) at low priority, on the workers,
    /// and returns at once: for work a parallel-for's join must never wait on (decoding images).
    [[nodiscard]] Pending Start(u32 chunks, std::function<void(u32)> body);

    /// Starts `work` on a worker at low priority and returns at once: for work that takes long and
    /// must never hold up a parallel-for's join (writing a save). One runs at a time: starting
    /// another waits for the last first. The destructor waits for one still running. Only a
    /// thread that may fork starts or waits for one, and the same thread each time.
    void Background(std::function<void()> work);

    /// Waits until the background job started last has finished; returns at once when none runs.
    void WaitForBackground();

private:
    void AssertMayFork() const;
    void Run(u32 chunks, void (*call)(const void* callable, u32 chunk), const void* callable);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace core
