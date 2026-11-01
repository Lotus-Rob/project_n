#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "platform/sdl_log.hpp"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

namespace {

// Quill's loggers live for the whole process, so Start() runs once, here, for every test, in the
// working directory (the build's), not the shared temp folder another run could own.
std::filesystem::path LogDirectory() {
    const std::filesystem::path directory = std::filesystem::current_path() / "project_n_sdl_log_tests";
    [[maybe_unused]] static const bool started = [&directory] {
        std::filesystem::remove_all(directory);
        core::log::Start(directory);
        return true;
    }();
    return directory;
}

// The log file's line for `message`, which names its level.
std::string LoggedLine(const std::filesystem::path& directory, std::string_view message) {
    core::log::Root()->flush_log();
    std::ifstream text{ directory / "project_n.log" };
    for (std::string line; std::getline(text, line);) {
        if (line.contains(message)) {
            return line;
        }
    }
    return {};
}

TEST_CASE("SDL warnings are logged as warnings") {
    const std::filesystem::path directory = LogDirectory();
    platform::RouteSdlLogs();
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "an SDL warning");

    const std::string line = LoggedLine(directory, "an SDL warning");
    CHECK(line.find("LOG_WARNING") != std::string::npos);
}

TEST_CASE("SDL errors are logged as errors") {
    const std::filesystem::path directory = LogDirectory();
    platform::RouteSdlLogs();
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "an SDL error");

    const std::string line = LoggedLine(directory, "an SDL error");
    CHECK(line.find("LOG_ERROR") != std::string::npos);
}

TEST_CASE("SDL critical messages are logged as critical") {
    const std::filesystem::path directory = LogDirectory();
    platform::RouteSdlLogs();
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "an SDL critical message");

    const std::string line = LoggedLine(directory, "an SDL critical message");
    CHECK(line.find("LOG_CRITICAL") != std::string::npos);
}

TEST_CASE("SDL info messages are logged as info") {
    const std::filesystem::path directory = LogDirectory();
    platform::RouteSdlLogs();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "an SDL info message");

    const std::string line = LoggedLine(directory, "an SDL info message");
    CHECK(line.find("LOG_INFO") != std::string::npos);
}

TEST_CASE("SDL debug messages are logged as debug") {
    const std::filesystem::path directory = LogDirectory();
    platform::RouteSdlLogs();
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "an SDL debug message");

    const std::string line = LoggedLine(directory, "an SDL debug message");
    CHECK(line.find("LOG_DEBUG") != std::string::npos);
}

TEST_CASE("SDL verbose messages are not logged") {
    const std::filesystem::path directory = LogDirectory();
    platform::RouteSdlLogs();
    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "an SDL verbose message");

    const std::string line = LoggedLine(directory, "an SDL verbose message");
    CHECK(line.empty());
}

} // namespace
