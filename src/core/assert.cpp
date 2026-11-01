#include "core/assert.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"

namespace core::detail {

void AssertFailed(std::string_view condition, std::string_view message, std::source_location where) {
    PN_LOG_CRITICAL(
        "assertion failed: {} ({}) at {}:{} in {}", message, condition, where.file_name(), where.line(), where.function_name()
    );
    log::Root()->flush_log();
    std::abort();
}

} // namespace core::detail
