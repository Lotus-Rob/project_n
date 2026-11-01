#include "platform/clock.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"

#include <SDL3/SDL.h>

namespace platform {

namespace {

// Sleeping can overshoot by about a scheduler slice; the rest is waited precisely.
constexpr u64 PRECISE_WAIT_NS = 1'000'000;

} // namespace

u64 NowNs() {
    return SDL_GetTicksNS();
}

void WaitUntilNs(u64 deadlineNs) {
    PN_PROFILE_ZONE();
    u64 now = NowNs();
    if (deadlineNs > now + PRECISE_WAIT_NS) {
        SDL_DelayNS(deadlineNs - now - PRECISE_WAIT_NS);
        now = NowNs();
    }
    if (deadlineNs > now) {
        SDL_DelayPrecise(deadlineNs - now);
    }
}

} // namespace platform
