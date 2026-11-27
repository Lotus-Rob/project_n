#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/path_text.hpp"
#include "crash_self.hpp"

#include <doctest/doctest.h>

namespace {

struct Run {
    i32 status;
    std::string log;
};

/// Runs this binary as a child that logs a line and then dies the given way, in a fresh folder.
Run RunChild(const std::string& kind) {
    static i32 counter = 0;
    const std::filesystem::path folder =
        std::filesystem::current_path() / std::format("project_n_log_crash_{}_{}", kind, counter++);
    std::filesystem::remove_all(folder);
#ifdef _WIN32
    // cmd.exe drops the outer pair of quotes when a command starts and ends with one.
    constexpr std::string_view OUTER = R"(")";
#else
    constexpr std::string_view OUTER;
#endif
    const std::string command = std::format(
        R"({}"{}" --crash {} "{}"{})",
        OUTER,
        core::PathText(std::filesystem::absolute(SelfPath())),
        kind,
        core::PathText(folder),
        OUTER
    );
    // NOLINTNEXTLINE(bugprone-command-processor): the command is this binary's own path, run as the child that dies
    const i32 status = std::system(command.c_str());
    std::ifstream file{ folder / "project_n.log" };
    Run run{ .status = status, .log = std::string{ std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} } };
    file.close();
    std::filesystem::remove_all(folder);
    return run;
}

/// What Quill's crash handler writes once it has caught the death: only it writes this, so a line logged before the crash
/// that merely beat the process to the file doesn't count as the handler's flush.
constexpr std::string_view QUILL_CRASH_RECORD = "Program terminated unexpectedly";

TEST_CASE("an abort leaves the lines logged before it in the file") {
    const Run run = RunChild("abort");
    CHECK(run.status != 0);
    CHECK(run.log.contains("last line before the crash"));
    CHECK(run.log.contains(QUILL_CRASH_RECORD));
}

TEST_CASE("a segmentation fault leaves the lines logged before it in the file") {
    const Run run = RunChild("segv");
    CHECK(run.status != 0);
    CHECK(run.log.contains("last line before the crash"));
    CHECK(run.log.contains(QUILL_CRASH_RECORD));
}

TEST_CASE("an exception that escapes a thread is named in the log") {
    const Run run = RunChild("terminate");
    CHECK(run.status != 0);
    CHECK(run.log.contains("last line before the crash"));
#ifndef _WIN32
    // On Windows Quill's unhandled-exception filter takes an uncaught exception before the terminate handler runs, so the log
    // has the lines and Quill's record only.
    CHECK(run.log.contains("terminating: boom on a thread"));
#endif
}

TEST_CASE("an exception that escapes the main thread is named in the log") {
    const Run run = RunChild("main");
    CHECK(run.status != 0);
    CHECK(run.log.contains(QUILL_CRASH_RECORD));
#ifndef _WIN32
    // Measured under Wine: Quill's unhandled-exception filter takes the uncaught exception before the C++ runtime's terminate
    // handler runs, so on Windows even the starting thread's exception isn't named; Quill's record is all the log holds.
    CHECK(run.log.contains("terminating: boom on the main thread"));
#endif
}

TEST_CASE("a terminate inside the terminate handler still ends the program with the earlier lines in the file") {
    const Run run = RunChild("reenter");
    CHECK(run.status != 0);
    CHECK(run.log.contains("last line before the crash"));
}

TEST_CASE("Folder is the folder Start was given") {
    const std::filesystem::path folder = std::filesystem::current_path() / "project_n_log_crash_folder";
    CHECK(core::log::Folder().empty());
    core::log::Start(folder);
    CHECK(core::log::Folder() == folder);
    // The log file may still be open, which Windows won't delete; a leftover folder is harmless.
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
}

} // namespace
