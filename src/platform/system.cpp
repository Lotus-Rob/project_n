#include "platform/system.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

#include <SDL3/SDL.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/utsname.h>
#endif

namespace platform {

namespace {

#ifdef _WIN32
// GetVersionEx lies about the version to programs without a compatibility manifest; ntdll's answer is true.
std::string OsText() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return SDL_GetPlatform();
    }
    const auto rtlGetVersion = std::bit_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (rtlGetVersion == nullptr) {
        return SDL_GetPlatform();
    }
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (rtlGetVersion(&info) != 0) {
        return SDL_GetPlatform();
    }
    return std::format("Windows {}.{}.{}", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
}
#else
#ifdef SDL_PLATFORM_LINUX
std::string PrettyName() {
    // os-release(5): /etc's file, else the one the distribution ships under /usr/lib.
    std::ifstream file("/etc/os-release");
    if (!file) {
        file.open("/usr/lib/os-release");
    }
    constexpr std::string_view KEY = "PRETTY_NAME=";
    std::string line;
    while (std::getline(file, line)) {
        if (!line.starts_with(KEY)) {
            continue;
        }
        std::string value = line.substr(KEY.size());
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
            value = value.substr(1, value.size() - 2);
        }
        return value;
    }
    return {};
}
#endif

std::string OsText() {
    utsname names{};
    if (uname(&names) != 0) {
        return SDL_GetPlatform();
    }
#ifdef SDL_PLATFORM_LINUX
    const std::string pretty = PrettyName();
    if (!pretty.empty()) {
        return std::format("{}, kernel {}", pretty, names.release);
    }
    return std::format("Linux {}", names.release);
#else
    return std::format("{} {}", names.sysname, names.release);
#endif
}
#endif

} // namespace

std::string SystemSummary() {
    return std::format("{}, {} logical CPUs, {} MiB RAM", OsText(), SDL_GetNumLogicalCPUCores(), SDL_GetSystemRAM());
}

} // namespace platform
