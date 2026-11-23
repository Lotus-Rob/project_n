#pragma once

/// @file
/// @ingroup core
/// Clang's thread-safety annotations, so the compiler checks which members a mutex guards and
/// which functions take it, and core::Mutex and core::SharedMutex, a std::mutex and a
/// std::shared_mutex they work with. The annotations are empty on other compilers.

#include <mutex>
#include <shared_mutex>

// NOLINTBEGIN(cppcoreguidelines-macro-usage): attributes only Clang knows, empty elsewhere.

#ifdef __clang__
/// Applies the thread-safety attribute `x`; use the named macros below instead.
#define PN_THREAD_ANNOTATION(x) __attribute__((x))
#else
/// Nothing: only Clang checks thread safety.
#define PN_THREAD_ANNOTATION(x)
#endif

/// Declares a member that may be used only with the mutex `x` held.
#define PN_GUARDED_BY(x) PN_THREAD_ANNOTATION(guarded_by(x))
/// Declares a function that must be called with the mutexes listed held, or, written `!x`, without
/// `x` held (a function that takes `x` itself).
#define PN_REQUIRES(...) PN_THREAD_ANNOTATION(requires_capability(__VA_ARGS__))
/// Declares a function that must be called with the mutexes listed held, at least shared.
#define PN_REQUIRES_SHARED(...) PN_THREAD_ANNOTATION(requires_shared_capability(__VA_ARGS__))

// NOLINTEND(cppcoreguidelines-macro-usage)

namespace core {

/// A std::mutex the thread-safety annotations understand, including negative requirements
/// (`PN_REQUIRES(!mutex)`). Lock it with std::scoped_lock.
class PN_THREAD_ANNOTATION(capability("mutex")) Mutex : std::mutex {
public:
    Mutex() = default;
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex(Mutex&&) = delete;
    Mutex& operator=(Mutex&&) = delete;
    ~Mutex() = default;

    /// Waits for and takes the mutex.
    // NOLINTNEXTLINE(readability-identifier-naming): std::scoped_lock calls lock() and unlock()
    void lock() PN_THREAD_ANNOTATION(acquire_capability()) {
        std::mutex::lock();
    }
    /// Releases the mutex.
    // NOLINTNEXTLINE(readability-identifier-naming): std::scoped_lock calls lock() and unlock()
    void unlock() PN_THREAD_ANNOTATION(release_capability()) {
        std::mutex::unlock();
    }
    /// For `PN_REQUIRES(!mutex)`; the annotations read it, nothing calls it.
    const Mutex& operator!() const {
        return *this;
    }
};

/// A std::shared_mutex the thread-safety annotations understand: many threads may hold it shared
/// (core::SharedLock) while none holds it alone (std::scoped_lock).
class PN_THREAD_ANNOTATION(capability("mutex")) SharedMutex : std::shared_mutex {
public:
    SharedMutex() = default;
    SharedMutex(const SharedMutex&) = delete;
    SharedMutex& operator=(const SharedMutex&) = delete;
    SharedMutex(SharedMutex&&) = delete;
    SharedMutex& operator=(SharedMutex&&) = delete;
    ~SharedMutex() = default;

    /// Waits for and takes the mutex alone.
    // NOLINTNEXTLINE(readability-identifier-naming): std::scoped_lock calls lock() and unlock()
    void lock() PN_THREAD_ANNOTATION(acquire_capability()) {
        std::shared_mutex::lock();
    }
    /// Releases the mutex held alone.
    // NOLINTNEXTLINE(readability-identifier-naming): std::scoped_lock calls lock() and unlock()
    void unlock() PN_THREAD_ANNOTATION(release_capability()) {
        std::shared_mutex::unlock();
    }
    /// Waits for and takes the mutex shared.
    void LockShared() PN_THREAD_ANNOTATION(acquire_shared_capability()) {
        std::shared_mutex::lock_shared();
    }
    /// Releases the mutex held shared.
    void UnlockShared() PN_THREAD_ANNOTATION(release_shared_capability()) {
        std::shared_mutex::unlock_shared();
    }
    /// For `PN_REQUIRES(!mutex)`; the annotations read it, nothing calls it.
    const SharedMutex& operator!() const {
        return *this;
    }
};

/// Holds a core::SharedMutex shared for its own life, as std::shared_lock would, in a way the
/// thread-safety annotations follow.
class PN_THREAD_ANNOTATION(scoped_lockable) SharedLock {
public:
    /// Waits for and takes `mutex` shared.
    explicit SharedLock(SharedMutex& mutex) PN_THREAD_ANNOTATION(acquire_shared_capability(mutex))
      : m_mutex{ &mutex } {
        m_mutex->LockShared();
    }
    SharedLock(const SharedLock&) = delete;
    SharedLock& operator=(const SharedLock&) = delete;
    SharedLock(SharedLock&&) = delete;
    SharedLock& operator=(SharedLock&&) = delete;
    /// Releases the mutex.
    ~SharedLock() PN_THREAD_ANNOTATION(release_capability()) {
        m_mutex->UnlockShared();
    }

private:
    SharedMutex* m_mutex;
};

} // namespace core
