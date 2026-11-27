#include "core/log.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/path_text.hpp"

#ifdef _MSC_VER
#pragma warning(push)
// C4868 (braced-list evaluation order) comes from Quill's templates during link-time code
// generation, where /external:W0 no longer applies.
#pragma warning(disable : 4868)
#endif
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/sinks/ConsoleSink.h>
#include <quill/sinks/RotatingFileSink.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace core::log {

namespace {

constexpr const char* LOGGER_NAME = "root";
constexpr const char* EARLY_LOGGER_NAME = "early";
// Quill's handler arms an alarm that ends the process if its flush hangs.
constexpr u32 CRASH_FLUSH_TIMEOUT_SECONDS = 5;
constexpr u32 KEPT_FILES = 10; // this run's and the 9 before it

/// The logger Start() created; null before it.
std::atomic<quill::Logger*>& Started() {
    static std::atomic<quill::Logger*> logger{ nullptr };
    return logger;
}

#ifdef _WIN32
/// The launching terminal's console output code page, which exit puts back: the console outlives
/// the game, so a code page left at UTF-8 would change how the terminal shows the next program.
u32& OriginalConsoleCodePage() {
    static u32 codePage = 0;
    return codePage;
}

void RestoreConsoleCodePage() noexcept {
    SetConsoleOutputCP(OriginalConsoleCodePage());
}
#endif

/// The folder of the log file; written once by Start() before it publishes the logger.
std::filesystem::path& FolderStorage() {
    // Never destroyed, so a caller late in the program's exit still finds it intact.
    static auto* const folder = new std::filesystem::path; // NOLINT(cppcoreguidelines-owning-memory): see above
    return *folder;
}

/// Logs what ended the program, writes everything still queued and aborts, which Quill's signal
/// handler then turns into a flush of whatever the abort itself left.
[[noreturn]] void LogAndAbort() noexcept {
    // A second terminate inside this handler (a throwing what(), a failed allocation) would
    // otherwise run it again until the stack ends.
    static std::atomic_flag entered;
    if (entered.test_and_set()) {
        std::abort();
    }
    std::string what;
    const std::exception_ptr current = std::current_exception();
    if (current == nullptr) {
        what = "terminating with no active exception";
    }
    else {
        try {
            std::rethrow_exception(current);
        }
        catch (const std::exception& e) {
            what = std::format("terminating: {}", e.what());
        }
        catch (...) {
            what = "terminating: an exception of unknown type";
        }
    }
    PN_LOG_CRITICAL("{}", what);
    // flush_log waits for the backend: it never returns once the backend has stopped, and on the
    // backend's own thread it would throw.
    if (quill::Backend::is_running() && quill::detail::get_thread_id() != quill::Backend::get_thread_id()) {
        Root()->flush_log();
    }
    std::abort();
}

/// Starts Quill's backend with its crash handler. On POSIX, SIGINT and SIGTERM stay out: SDL turns
/// them into its quit event only while their handlers are the defaults. On Windows Quill always
/// installs a console control handler too; Ctrl-C still reaches SDL because SDL's handler, set
/// later, runs first. Quill starts once, so the early logger and Start() share these options.
void StartBackend() {
    quill::SignalHandlerOptions options;
    options.catchable_signals = { SIGABRT, SIGFPE, SIGILL, SIGSEGV };
#ifdef SIGBUS
    options.catchable_signals.push_back(SIGBUS);
#endif
    // Named, so the record lands in the file even when the early console-only logger exists too.
    options.logger_name = LOGGER_NAME;
    options.timeout_seconds = CRASH_FLUSH_TIMEOUT_SECONDS;
    quill::Backend::start<quill::FrontendOptions>(quill::BackendOptions{}, options);
#ifdef _WIN32
    // Windows' C runtime keeps signal handlers per thread, and Backend::start leaves them to the caller.
    quill::init_signal_handler<quill::FrontendOptions>(options.catchable_signals);
#endif
    std::set_terminate(LogAndAbort);
}

/// Quill writes UTF-8; the Windows console shows it as text only in its UTF-8 output code page.
/// Every logger that reaches the console calls this first, and before Quill's backend starts, so
/// the restore at exit runs after the backend's final flush (exit handlers run in reverse).
void UseUtf8Console() {
#ifdef _WIN32
    static const bool registered = [] {
        // Zero: no console, so nothing to change now or to restore.
        OriginalConsoleCodePage() = GetConsoleOutputCP();
        return OriginalConsoleCodePage() != 0 && std::atexit(RestoreConsoleCodePage) == 0;
    }();
    static_cast<void>(registered);
    SetConsoleOutputCP(CP_UTF8);
#endif
}

} // namespace

void Start(const std::filesystem::path& directory) {
    if (Started().load(std::memory_order_acquire) != nullptr) {
        return;
    }
    UseUtf8Console();
    StartBackend();

    auto console = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console");
    console->set_log_level_filter(quill::LogLevel::Info);
    std::vector<std::shared_ptr<quill::Sink>> sinks{ console };

    std::string file; // also the sink's name
    std::string problem;
    if (!directory.empty()) {
        try {
            std::filesystem::create_directories(directory);
            // Quill takes the file name as a std::string and reads it in the ANSI code page,
            // which the executables' manifest makes UTF-8: the UTF-8 text opens the same file
            // however the folder is named.
            file = core::PathText(directory / "project_n.log");
            quill::RotatingFileSinkConfig config;
            // Append mode: only then do files rotated by earlier runs count towards the limit;
            // in write mode Quill deletes them at startup.
            config.set_open_mode('a');
            config.set_rotation_on_creation(true);
            config.set_max_backup_files(KEPT_FILES - 1);
            sinks.push_back(quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(file, config));
            FolderStorage() = directory;
        }
        catch (const std::exception& e) {
            problem = e.what();
        }
    }

    quill::Logger* logger = quill::Frontend::create_or_get_logger(LOGGER_NAME, std::move(sinks));
    logger->set_log_level(quill::LogLevel::Debug);
    Started().store(logger, std::memory_order_release);
    if (!problem.empty()) {
        PN_LOG_WARNING("logging to the console only: {}", problem);
    }
    else if (!file.empty()) {
        PN_LOG_INFO("logging to {}", file);
    }
}

quill::Logger* Root() {
    if (quill::Logger* logger = Started().load(std::memory_order_acquire); logger != nullptr) {
        return logger;
    }
    // Before Start(): the console only, so an early message is still written, not lost.
    static quill::Logger* const early = [] {
        UseUtf8Console();
        StartBackend();
        return quill::Frontend::create_or_get_logger(
            EARLY_LOGGER_NAME, quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console")
        );
    }();
    return early;
}

const std::filesystem::path& Folder() {
    return FolderStorage();
}

} // namespace core::log
