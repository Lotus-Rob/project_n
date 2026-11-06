#include "core/saves.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"
#include "core/save_file.hpp"

namespace core {

void Saves::Write(std::vector<std::byte> state) {
    PN_PROFILE_ZONE();
    if (m_slot.empty()) {
        PN_LOG_WARNING("not saved: there is no folder for saves");
        return;
    }
    m_jobs->Background([slot = m_slot, bytes = std::move(state)] {
        const std::expected<void, Error> written = WriteSaveFile(slot, bytes);
        if (written) {
            PN_LOG_INFO("saved the game to {}", slot.string());
        }
        else {
            PN_LOG_ERROR("{}", written.error().message);
        }
    });
}

std::expected<std::vector<std::byte>, Error> Saves::Read() {
    PN_PROFILE_ZONE();
    if (m_slot.empty()) {
        return std::unexpected{ Error{ .message = "there is no folder for saves" } };
    }
    m_jobs->WaitForBackground(); // a save still being written is the newest
    return ReadSaveFile(m_slot);
}

} // namespace core
