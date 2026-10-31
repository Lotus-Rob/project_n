#include "core/log.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

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

namespace core::log {

namespace {

constexpr const char* LOGGER_NAME = "root";
constexpr const char* EARLY_LOGGER_NAME = "early";
constexpr u32 KEPT_FILES = 10; // this run's and the 9 before it

// The logger Start() created; null before it.
std::atomic<quill::Logger*>& Started() {
    static std::atomic<quill::Logger*> logger{ nullptr };
    return logger;
}

} // namespace

void Start(const std::filesystem::path& directory) {
    if (Started().load(std::memory_order_acquire) != nullptr) {
        return;
    }
    quill::Backend::start();

    auto console = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console");
    console->set_log_level_filter(quill::LogLevel::Info);
    std::vector<std::shared_ptr<quill::Sink>> sinks{ console };

    std::string file; // also the sink's name
    std::string problem;
    if (!directory.empty()) {
        try {
            std::filesystem::create_directories(directory);
            // shortcut: Quill takes the file name as a std::string, so on Windows a path outside the
            // ANSI code page throws here and logging stays on the console; a UTF-8 code page
            // manifest would lift it.
            file = (directory / "project_n.log").string();
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
        quill::Backend::start();
        return quill::Frontend::create_or_get_logger(
            EARLY_LOGGER_NAME, quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console")
        );
    }();
    return early;
}

} // namespace core::log
