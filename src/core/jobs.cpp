#include "core/jobs.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/math.hpp"
#include "core/profile.hpp"

#include <enkiTS/TaskScheduler.h>

namespace core {

namespace {

constexpr u32 OTHER_THREADS = 2; // the main thread and the logic thread

/// What `error` says in a log line: its `what()` for a std::exception, else a stand-in.
std::string Describe(const std::exception_ptr& error) {
    try {
        std::rethrow_exception(error);
    }
    catch (const std::exception& e) {
        return e.what();
    }
    catch (...) {
        return "an exception of unknown type";
    }
}

/// The first exception thrown by any of a task's chunks, kept for the thread that joins it.
class ChunkFailure {
public:
    /// Whether a chunk has thrown, so chunks not yet begun are skipped.
    [[nodiscard]] bool Failed() const {
        return m_failed.load(std::memory_order_relaxed);
    }

    /// Keeps the exception being handled, unless another chunk's came first.
    void Catch() {
        bool expected = false;
        // The claim comes first, so only one thread ever writes the pointer; the joiner reads it after the join.
        if (m_failed.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
            m_error = std::current_exception();
        }
    }

    /// Hands over the exception kept, once, after the join; null when none was or it was taken.
    [[nodiscard]] std::exception_ptr Take() {
        return std::exchange(m_error, nullptr);
    }

private:
    std::atomic<bool> m_failed = false;
    std::exception_ptr m_error = nullptr;
};

/// Runs `call(chunk)` for each chunk in `range` unless one has failed, keeping the first exception in `failure`.
template<typename Call>
void RunChunks(enki::TaskSetPartition range, ChunkFailure& failure, const Call& call) {
    for (u32 chunk = range.start; chunk < range.end && !failure.Failed(); ++chunk) {
        try {
            call(chunk);
        }
        catch (...) {
            failure.Catch();
        }
    }
}

/// One item per chunk: enkiTS divides a set by its thread count, so chunk boundaries, and with them the
/// results, would otherwise depend on how many workers run.
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

    /// The first chunk's exception, null when none threw; only after the join.
    [[nodiscard]] std::exception_ptr TakeFailure() {
        return m_failure.Take();
    }

    void ExecuteRange(enki::TaskSetPartition range, u32 /*threadnum*/) override {
        PN_PROFILE_ZONE();
        static_cast<void>(m_ran.load(std::memory_order_acquire));
        RunChunks(range, m_failure, [this](u32 chunk) { m_call(m_callable, chunk); });
        // Skipped chunks count too: the join waits for the whole range, run or not.
        m_ran.fetch_add(range.end - range.start, std::memory_order_release);
    }

private:
    void (*m_call)(const void*, u32) = nullptr;
    const void* m_callable = nullptr;
    ChunkFailure m_failure;
    std::atomic<u32> m_ran = 0;
};

} // namespace

/// Low-priority chunks: one item per chunk, as ChunkTask, so a join at high priority never waits on them.
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
    ChunkFailure failure;
    std::atomic<u32> ran = 0; // the fork and join as edges ThreadSanitizer sees (enkiTS isn't instrumented)
};

// Out of line: the class has external linkage, and its vtable needs one translation unit to live in.
void Jobs::Pending::Task::ExecuteRange(enki::TaskSetPartition range, u32 /*threadnum*/) {
    PN_PROFILE_ZONE();
    static_cast<void>(ran.load(std::memory_order_acquire)); // the fork: sees the body Start() moved in
    RunChunks(range, failure, body);
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

        /// Why the job failed, once, after Done(); nothing when it succeeded or the failure was taken.
        [[nodiscard]] std::optional<Error> TakeFailure() {
            return std::exchange(m_failure, std::nullopt);
        }

        void ExecuteRange(enki::TaskSetPartition range, u32 threadnum) override;

    private:
        std::function<void()> m_work;
        std::optional<Error> m_failure;
        std::atomic<bool> m_done = false;
    };

    /// Waits until the background job started last has finished, if there is one.
    void JoinBackground();

    enki::TaskScheduler scheduler{};
    u32 workers = 0;
    u32 externalThreads = 0;
    std::unique_ptr<BackgroundTask> background = nullptr; // the last one started, until the next
};

// Out of line: the class has external linkage, and its vtable needs one translation unit to live in.
void Jobs::Impl::BackgroundTask::ExecuteRange(enki::TaskSetPartition /*range*/, u32 /*threadnum*/) {
    PN_PROFILE_ZONE();
    static_cast<void>(m_done.load(std::memory_order_acquire)); // the fork: sees the work Background() moved in
    try {
        m_work();
    }
    catch (...) {
        // Logged here, where it happened: a later wait may only drop it (the next Background() does).
        m_failure = Error{ .message = Describe(std::current_exception()) };
        PN_LOG_ERROR("a background job failed: {}", m_failure->message);
    }
    m_done.store(true, std::memory_order_release);
}

void Jobs::Impl::JoinBackground() {
    if (background) {
        scheduler.WaitforTask(background.get());
        const bool done = background->Done(); // loaded in every mode: it's the join's edge too
        PN_ASSERT(done, "a background job's wait returned before it finished");
    }
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
        const bool finished = Finished(); // loaded in every mode: it's the join's edge too
        PN_ASSERT(finished, "a pending job's wait returned before its chunks ran");
        // A destructor can't rethrow, and the owner went without asking, so this is the failure's only trace.
        if (const std::exception_ptr error = m_task->failure.Take()) {
            PN_LOG_ERROR("started chunks failed and nobody took the failure: {}", Describe(error));
        }
    }
}

bool Jobs::Pending::Done() const {
    if (!Finished()) {
        return false;
    }
    if (const std::exception_ptr error = m_task ? m_task->failure.Take() : nullptr) {
        std::rethrow_exception(error);
    }
    return true;
}

bool Jobs::Pending::Finished() const {
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
    AssertMayFork();
    // The last job's failure, if any, was logged on its thread and goes with it.
    m_impl->JoinBackground();
    m_impl->background = std::make_unique<Impl::BackgroundTask>(std::move(work));
    m_impl->background->Fork();
    m_impl->scheduler.AddTaskSetToPipe(m_impl->background.get());
}

std::expected<void, Error> Jobs::WaitForBackground() {
    PN_PROFILE_ZONE();
    AssertMayFork();
    m_impl->JoinBackground();
    if (std::optional<Error> failure = m_impl->background ? m_impl->background->TakeFailure() : std::nullopt) {
        return std::unexpected{ *std::move(failure) };
    }
    return {};
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
    if (const std::exception_ptr error = task.TakeFailure()) {
        std::rethrow_exception(error);
    }
}

} // namespace core
