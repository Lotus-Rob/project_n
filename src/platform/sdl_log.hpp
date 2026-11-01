#pragma once

/// @file
/// @ingroup platform
/// SDL's own log messages, routed into ours.

namespace platform {

/// Sends SDL's log messages, from Debug up, to the `PN_LOG_*` logger at the matching level. Needs
/// no SDL_Init; call it before SDL starts so its start-up messages are logged too.
void RouteSdlLogs();

} // namespace platform
