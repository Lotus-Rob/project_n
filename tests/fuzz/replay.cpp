// The replay reader (core::Replay::StartPlaying): a replay file is untrusted, so any bytes are refused or
// played, never a crash. Seeded with a short recording holding every kind of field.
#include "core/replay.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

namespace {

std::vector<std::byte> Bytes(const u8* data, std::size_t size) {
#pragma clang diagnostic push
// libFuzzer hands its input over as a pointer and a size: nothing can check them.
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
    const std::span<const u8> input{ data, size };
#pragma clang diagnostic pop
    std::vector<std::byte> bytes(size);
    std::ranges::transform(input, bytes.begin(), [](u8 byte) { return std::byte{ byte }; });
    return bytes;
}

} // namespace

// Declared first: libFuzzer ships no header for it, and -Wmissing-prototypes requires one.
extern "C" i32 LLVMFuzzerTestOneInput(const u8* data, std::size_t size);

extern "C" i32 LLVMFuzzerTestOneInput(const u8* data, std::size_t size) {
    std::vector<std::byte> bytes = Bytes(data, size);
    core::Replay replay;
    if (replay.StartPlaying(std::move(bytes)).has_value()) {
        while (!replay.AtEnd()) {
            static_cast<void>(replay.Next());
        }
    }
    return 0;
}
