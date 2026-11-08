// The save reader (game::Arena::Load, behind the save file's checksum): a save is untrusted, so any bytes are
// refused or loaded into a state the game can tick, publish and save again, never a crash. Seeded with the
// painted scene's save.
#include "../support/manifest.hpp" // relative: lint has no compile flags for the fuzz targets, which only build with ASan
#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/jobs.hpp"
#include "game/arena.hpp"

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
    game::Arena arena(test::TestManifest(), game::Scene::Painted);
    core::Reader reader{ bytes };
    if (arena.Load(reader)) {
        arena.Tick({ .wall = 1, .simulation = 1, .state = 1 }, {});
        core::Jobs jobs{ 1 }; // here, not static: only inputs that load pay for its thread
        core::Snapshot snapshot;
        arena.Publish(snapshot, jobs);
        std::vector<std::byte> saved;
        core::Writer writer{ saved };
        arena.Save(writer);
    }
    return 0;
}
