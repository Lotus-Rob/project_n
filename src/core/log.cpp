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
    quill::Backend::start();

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
        quill::Backend::start();
        return quill::Frontend::create_or_get_logger(
            EARLY_LOGGER_NAME, quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console")
        );
    }();
    return early;
}

} // namespace core::log
