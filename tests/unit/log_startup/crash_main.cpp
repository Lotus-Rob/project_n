#define DOCTEST_CONFIG_IMPLEMENT
#include "crash_self.hpp"

#include "core/log.hpp"

#include <doctest/doctest.h>

#ifndef _WIN32
#include <sys/resource.h>
#endif

namespace {

/// An exception whose description itself ends the program, so the terminate handler meets a second terminate.
struct Reentrant : std::exception {
    [[noreturn]] const char* what() const noexcept override {
        std::terminate();
    }
};

[[noreturn]] void Boom() {
    throw std::runtime_error{ "boom on a thread" };
}

[[noreturn]] void Crash(const std::string_view kind, const std::filesystem::path& folder) {
#ifndef _WIN32
    // The crash is deliberate: no core dump for the host's collector to keep.
    constexpr rlimit NO_CORE{ .rlim_cur = 0, .rlim_max = 0 };
    setrlimit(RLIMIT_CORE, &NO_CORE);
#endif
    core::log::Start(folder);
    PN_LOG_INFO("last line before the crash");
    if (kind == "reenter") {
        throw Reentrant{};
    }
    if (kind == "segv") {
        std::raise(SIGSEGV);
    }
    if (kind == "terminate") {
        std::thread{ Boom }.join();
    }
    std::abort();
}

std::atomic<const char*>& Self() {
    static std::atomic<const char*> self{ nullptr }; // argv[0], which outlives main
    return self;
}

} // namespace

std::filesystem::path SelfPath() {
    return Self().load();
}

int main(int argc, char** argv) {
#ifdef __clang__
#pragma clang diagnostic push
// The C runtime hands the arguments over as a pointer and a count: nothing can check them.
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
    const std::span<char*> args{ argv, static_cast<std::size_t>(argc) };
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    if (args.size() == 4 && std::string_view{ args[1] } == "--crash") {
        Crash(args[2], args[3]);
    }
    Self().store(args[0]);
    return doctest::Context{ argc, argv }.run();
}
