#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/sdl_log.hpp"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

namespace {

constexpr i32 EARLIER_RUNS = 10; // one more than Start keeps

std::string Read(const std::filesystem::path& file) {
    std::ifstream stream{ file };
    return { std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
}

// Quill's loggers live for the whole process, so Start() runs once, here, for every test that
// needs it: in a directory seeded with logs from EARLIER_RUNS runs, project_n.log the newest.
std::filesystem::path StartedDirectory() {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "project_n_log_tests";
    [[maybe_unused]] static const bool started = [&directory] {
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        std::ofstream{ directory / "project_n.log" } << "run 0";
        for (i32 run = 1; run < EARLIER_RUNS; ++run) {
            std::ofstream{ directory / std::format("project_n.{}.log", run) } << std::format("run {}", run);
        }
        core::log::Start(directory);
        return true;
    }();
    return directory;
}

TEST_CASE("Start keeps the previous run's log as project_n.1.log") {
    CHECK(Read(StartedDirectory() / "project_n.1.log") == "run 0");
}

TEST_CASE("Start keeps only the previous 9 runs' logs") {
    CHECK(Read(StartedDirectory() / "project_n.9.log") == "run 8");
    CHECK_FALSE(std::filesystem::exists(StartedDirectory() / "project_n.10.log"));
}

// The log file's line for `message`, which names its level.
std::string LoggedLine(const std::filesystem::path& directory, std::string_view message) {
    core::log::Root()->flush_log();
    std::istringstream text{ Read(directory / "project_n.log") };
    for (std::string line; std::getline(text, line);) {
        if (line.contains(message)) {
            return line;
        }
    }
    return {};
}

TEST_CASE("SDL warnings are logged as warnings") {
    const std::filesystem::path directory = StartedDirectory();
    core::log::RouteSdlLogs();
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "an SDL warning");

    const std::string line = LoggedLine(directory, "an SDL warning");
    CHECK(line.find("LOG_WARNING") != std::string::npos);
}

TEST_CASE("SDL errors are logged as errors") {
    const std::filesystem::path directory = StartedDirectory();
    core::log::RouteSdlLogs();
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "an SDL error");

    const std::string line = LoggedLine(directory, "an SDL error");
    CHECK(line.find("LOG_ERROR") != std::string::npos);
}

TEST_CASE("debug messages reach the log file") {
    const std::filesystem::path directory = StartedDirectory();
    PN_LOG_DEBUG("a debug message");
    core::log::Root()->flush_log();

    CHECK(Read(directory / "project_n.log").find("a debug message") != std::string::npos);
}

} // namespace
