#include "core/jobs.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"

#include <enkiTS/TaskScheduler.h>

namespace core {

namespace {

constexpr u32 OTHER_THREADS = 2; // the main thread and the logic thread

// A parallel-for's chunks as an enkiTS task set: one item per chunk, so enkiTS splits the set by its
// thread count but never splits a chunk.
class ChunkTask final : public enki::ITaskSet {
public:
    ChunkTask(u32 chunks, void (*call)(const void*, u32), const void* callable)
      : enki::ITaskSet(chunks, 1)
      , m_call(call)
      , m_callable(callable) {
        m_Priority = enki::TASK_PRIORITY_HIGH;
    }
    ChunkTask(const ChunkTask&) = delete;
    ChunkTask& operator=(const ChunkTask&) = delete;
    ChunkTask(ChunkTask&&) = delete;
    ChunkTask& operator=(ChunkTask&&) = delete;
    ~ChunkTask() override = default;

    // The fork and join as edges ThreadSanitizer sees: enkiTS synchronises in a library it doesn't instrument.
    void Fork() {
        m_ran.store(0, std::memory_order_release);
    }
    [[nodiscard]] u32 Joined() const {
        return m_ran.load(std::memory_order_acquire);
    }

    void ExecuteRange(enki::TaskSetPartition range, u32 /*threadnum*/) override {
        PN_PROFILE_ZONE();
        static_cast<void>(m_ran.load(std::memory_order_acquire));
        for (u32 chunk = range.start; chunk < range.end; ++chunk) {
            m_call(m_callable, chunk);
        }
        m_ran.fetch_add(range.end - range.start, std::memory_order_release);
    }

private:
    void (*m_call)(const void*, u32) = nullptr;
    const void* m_callable = nullptr;
    std::atomic<u32> m_ran = 0;
};

} // namespace

struct Jobs::Impl {
    enki::TaskScheduler scheduler{};
    u32 workers = 0;
};

Jobs::Jobs(u32 workers)
  : m_impl(std::make_unique<Impl>()) {
    enki::TaskSchedulerConfig config{};
    config.numTaskThreadsToCreate = std::max(workers, 1U);
    m_impl->workers = config.numTaskThreadsToCreate;
    m_impl->scheduler.Initialize(config);
}

Jobs::~Jobs() {
    m_impl->scheduler.WaitforAllAndShutdown();
}

u32 Jobs::DefaultWorkers() {
    const u32 hardware = std::thread::hardware_concurrency(); // 0 when unknown
    return hardware > OTHER_THREADS + 1 ? hardware - OTHER_THREADS : 1;
}

u32 Jobs::Workers() const {
    return m_impl->workers;
}

void Jobs::Run(u32 chunks, void (*call)(const void*, u32), const void* callable) {
    PN_PROFILE_ZONE();
    // enkiTS numbers the thread that initialised it 0; any other thread forking would race it.
    PN_ASSERT(m_impl->scheduler.GetThreadNum() == 0, "only the thread that created the job system forks");
    if (chunks == 0) {
        return;
    }
    ChunkTask task{ chunks, call, callable };
    task.Fork();
    m_impl->scheduler.AddTaskSetToPipe(&task);
    m_impl->scheduler.WaitforTask(&task, enki::TASK_PRIORITY_HIGH);
    const u32 ran = task.Joined(); // loaded in every mode: it's the join's edge too
    PN_ASSERT(ran == chunks, "a parallel-for returned before all its chunks ran");
}

} // namespace core
