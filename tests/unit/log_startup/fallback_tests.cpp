#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"

#include <doctest/doctest.h>

namespace {

// Its own executable: Quill's loggers live for the whole process, and this Start() must be the first.
TEST_CASE("Start with a directory it can't create still starts logging, to the console") {
    const std::filesystem::path file = std::filesystem::current_path() / "project_n_log_fallback";
    std::ofstream{ file } << "a file, so no directory can be created under it";
    core::log::Start(file / "logs");

    CHECK(core::log::Root()->get_logger_name() == "root");
}

} // namespace
