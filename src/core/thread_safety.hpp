#pragma once

/// @file
/// @ingroup core
/// Clang's thread-safety annotations, so the compiler checks which members a mutex guards and
/// which functions take it, and core::Mutex, a std::mutex they work with. The annotations are
/// empty on other compilers.

#include <mutex>

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

} // namespace core
