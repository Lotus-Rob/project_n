#include "platform/sdl_log.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"

#include <SDL3/SDL.h>

namespace platform {

namespace {

// A switch over SDL_LogPriority can't satisfy both -Wswitch-default and -Wcovered-switch-default;
// the priorities are ordered, so compare instead.
void SDLCALL LogFromSdl(void* /*userdata*/, i32 /*category*/, SDL_LogPriority priority, const char* message) noexcept {
    if (priority >= SDL_LOG_PRIORITY_CRITICAL) {
        PN_LOG_CRITICAL("SDL: {}", message);
    }
    else if (priority >= SDL_LOG_PRIORITY_ERROR) {
        PN_LOG_ERROR("SDL: {}", message);
    }
    else if (priority >= SDL_LOG_PRIORITY_WARN) {
        PN_LOG_WARNING("SDL: {}", message);
    }
    else if (priority >= SDL_LOG_PRIORITY_INFO) {
        PN_LOG_INFO("SDL: {}", message);
    }
    else {
        PN_LOG_DEBUG("SDL: {}", message);
    }
}

} // namespace

void RouteSdlLogs() {
    SDL_SetLogOutputFunction(LogFromSdl, nullptr);
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_DEBUG);
}

} // namespace platform
