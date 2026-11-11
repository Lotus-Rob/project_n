#include "core/jobs.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/math.hpp"
#include "core/profile.hpp"

#include <enkiTS/TaskScheduler.h>

namespace core {

namespace {

constexpr u32 OTHER_THREADS = 2; // the main thread and the logic thread

// One item per chunk: enkiTS divides a set by its thread count, so chunk boundaries, and with them the
// results, would otherwise depend on how many workers run.
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

// Low-priority chunks: one item per chunk, as ChunkTask, so a join at high priority never waits on them.
struct Jobs::Pending::Task final : public enki::ITaskSet {
    Task(u32 chunks, std::function<void(u32)> work)
      : enki::ITaskSet(chunks, 1)
      , body(std::move(work)) {
        m_Priority = enki::TASK_PRIORITY_LOW;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&&) = delete;
    Task& operator=(Task&&) = delete;
    ~Task() override = default;

    void ExecuteRange(enki::TaskSetPartition range, u32 threadnum) override;

    std::function<void(u32)> body;
    std::atomic<u32> ran = 0; // the fork and join as edges ThreadSanitizer sees (enkiTS isn't instrumented)
};

// Out of line: the class has external linkage, and its vtable needs one translation unit to live in.
void Jobs::Pending::Task::ExecuteRange(enki::TaskSetPartition range, u32 /*threadnum*/) {
    PN_PROFILE_ZONE();
    static_cast<void>(ran.load(std::memory_order_acquire)); // the fork: sees the body Start() moved in
    for (u32 chunk = range.start; chunk < range.end; ++chunk) {
        body(chunk);
    }
    ran.fetch_add(range.end - range.start, std::memory_order_release);
}

struct Jobs::Impl {
    // A background job: one item, low priority, so a join at high priority never waits on it.
    class BackgroundTask final : public enki::ITaskSet {
    public:
        explicit BackgroundTask(std::function<void()> work)
          : enki::ITaskSet(1, 1)
          , m_work(std::move(work)) {
            m_Priority = enki::TASK_PRIORITY_LOW;
        }
        BackgroundTask(const BackgroundTask&) = delete;
        BackgroundTask& operator=(const BackgroundTask&) = delete;
        BackgroundTask(BackgroundTask&&) = delete;
        BackgroundTask& operator=(BackgroundTask&&) = delete;
        ~BackgroundTask() override = default;

        // The fork and join as edges ThreadSanitizer sees (enkiTS isn't instrumented): Fork() before it's started,
        // Done() once it finished.
        void Fork() {
            m_done.store(false, std::memory_order_release);
        }
        [[nodiscard]] bool Done() const {
            return m_done.load(std::memory_order_acquire);
        }

        void ExecuteRange(enki::TaskSetPartition range, u32 threadnum) override;

    private:
        std::function<void()> m_work;
        std::atomic<bool> m_done = false;
    };

    enki::TaskScheduler scheduler{};
    u32 workers = 0;
    u32 externalThreads = 0;
    std::unique_ptr<BackgroundTask> background = nullptr; // the last one started, until the next
};

// Out of line: the class has external linkage, and its vtable needs one translation unit to live in.
void Jobs::Impl::BackgroundTask::ExecuteRange(enki::TaskSetPartition /*range*/, u32 /*threadnum*/) {
    PN_PROFILE_ZONE();
    static_cast<void>(m_done.load(std::memory_order_acquire)); // the fork: sees the work Background() moved in
    m_work();
    m_done.store(true, std::memory_order_release);
}

Jobs::Pending::Pending()
  : m_task{ nullptr } {
}

Jobs::Pending::Pending(Pending&& other) noexcept
  : m_jobs{ std::exchange(other.m_jobs, nullptr) }
  , m_task{ std::move(other.m_task) } {
}

Jobs::Pending::~Pending() {
    if (m_task) {
        m_jobs->m_impl->scheduler.WaitforTask(m_task.get());
        PN_ASSERT(Done(), "a pending job's wait returned before its chunks ran");
    }
}

bool Jobs::Pending::Done() const {
    // Both: `ran` is the edge ThreadSanitizer sees, but enkiTS marks the task complete only after the last chunk returns,
    // and until then the destructor's wait would run other low-priority work (a scene's decode) on this thread.
    return !m_task || (m_task->ran.load(std::memory_order_acquire) == m_task->m_SetSize && m_task->GetIsComplete());
}

Jobs::ThreadRegistration::ThreadRegistration(Jobs& jobs)
  : m_jobs{ &jobs } {
    const bool registered = m_jobs->m_impl->scheduler.RegisterExternalTaskThread();
    PN_ASSERT(registered, "more threads registered with the job system than it was built for");
}

Jobs::ThreadRegistration::~ThreadRegistration() {
    m_jobs->m_impl->scheduler.DeRegisterExternalTaskThread();
}

Jobs::Jobs(u32 workers, u32 externalThreads)
  : m_impl(std::make_unique<Impl>()) {
    enki::TaskSchedulerConfig config{};
    config.numTaskThreadsToCreate = std::max(workers, 1U);
    config.numExternalTaskThreads = externalThreads;
    m_impl->externalThreads = externalThreads;
    config.profilerCallbacks.threadStart = [](u32 /*thread*/) { math::SetFloatingPointEnvironment(); };
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

void Jobs::AssertMayFork() const {
    // enkiTS numbers the creating thread 0 and registered ones 1 to externalThreads; a worker or an
    // unregistered thread forking would race them.
    const u32 thread = m_impl->scheduler.GetThreadNum();
    PN_ASSERT(thread <= m_impl->externalThreads, "only the creating thread or a registered one forks");
}

Jobs::Pending Jobs::Start(u32 chunks, std::function<void(u32)> body) {
    AssertMayFork();
    Pending pending;
    if (chunks > 0) {
        pending.m_jobs = this;
        pending.m_task = std::make_unique<Pending::Task>(chunks, std::move(body));
        pending.m_task->ran.store(0, std::memory_order_release); // the fork
        m_impl->scheduler.AddTaskSetToPipe(pending.m_task.get());
    }
    return pending;
}

void Jobs::Background(std::function<void()> work) {
    WaitForBackground();
    m_impl->background = std::make_unique<Impl::BackgroundTask>(std::move(work));
    m_impl->background->Fork();
    m_impl->scheduler.AddTaskSetToPipe(m_impl->background.get());
}

void Jobs::WaitForBackground() {
    PN_PROFILE_ZONE();
    AssertMayFork();
    if (m_impl->background) {
        m_impl->scheduler.WaitforTask(m_impl->background.get());
        PN_ASSERT(m_impl->background->Done(), "a background job's wait returned before it finished");
    }
}

void Jobs::Run(u32 chunks, void (*call)(const void*, u32), const void* callable) {
    PN_PROFILE_ZONE();
    AssertMayFork();
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
