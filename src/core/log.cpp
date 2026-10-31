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
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace core::log {

void Start() {
    quill::Backend::start();
}

quill::Logger* Root() {
    // Looked up once: Quill's own lookup by name takes a lock, and this runs on every log call.
    static quill::Logger* const logger =
        quill::Frontend::create_or_get_logger("root", quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console"));
    return logger;
}

} // namespace core::log
