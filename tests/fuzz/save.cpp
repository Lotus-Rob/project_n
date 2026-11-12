// The save reader (game::Arena::Load, behind the save file's checksum): a save is untrusted, so any bytes are
// refused or loaded into a state the game can tick, publish and save again, never a crash. Seeded with the
// painted scene's save and the arena's in the middle of a fight (a volley in flight, a lance's flash, burn and poison,
// a fox dying, one easing in, a respawn waiting, the player dashing), its 5,000 world NPCs and their relationships
// left out so each run is quick.
#include "../support/manifest.hpp" // relative: lint has no compile flags for the fuzz targets, which only build with ASan
#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/jobs.hpp"
#include "game/arena.hpp"

namespace {

// The tick every input is loaded as saved at: the largest that leaves a tick after it, so a flash dated at any tick a
// save can name before it is loaded and drawn, not refused as one from the future.
constexpr u64 SAVED_AT = std::numeric_limits<u64>::max() - 1;

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
    if (arena.Load(reader, SAVED_AT)) {
        constexpr u64 NEXT = SAVED_AT + 1;
        std::vector<core::Event> events;
        arena.Tick({ .wall = NEXT, .simulation = NEXT, .state = NEXT }, {}, events);
        core::Jobs jobs{ 1 }; // here, not static: only inputs that load pay for its thread
        core::Snapshot snapshot;
        snapshot.simulationTick = NEXT;
        arena.Publish(snapshot, jobs);
        std::vector<std::byte> saved;
        core::Writer writer{ saved };
        arena.Save(writer);
    }
    return 0;
}
