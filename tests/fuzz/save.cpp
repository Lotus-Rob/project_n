// The save reader (game::Host::Load, behind the save file's checksum): a save is untrusted, so any bytes are
// refused or loaded into a state the game can enter, tick, publish and save again, never a crash. One game serves every
// input, in the arena on the small test map with travellers: a save's game world is a difference from a world made in a
// moment, and a save of the arena shares the world the game's own arena was built as instead of building its 5,000 NPCs
// again.
// Seeded with the arena in the middle of a fight (a volley in flight, a lance's flash, burn and poison, a fox dying, one
// easing in, a respawn waiting, the player dashing), the empty world scene, the world scene a month on (journeys of the
// base ended, new ones by their plans) and the arena entered from that world mid-volley (both worlds' sections). Each seed
// comes from a skipped-by-default case in unit/game/save_exactness_tests.cpp, written by
// `pixi run project_n test --target game_tests -- --test-case='write the <name> save seed' --no-skip` (`fight`, `world`,
// `delta`, `scratch`); regenerate all four whenever the save layout changes.
#include "../support/manifest.hpp" // relative: lint has no compile flags for the fuzz targets, which only build with ASan
#include "../support/world.hpp"
#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/jobs.hpp"
#include "game/host.hpp"

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

// The game every input is loaded into. Each input starts from the same state, so a run repeats exactly: the game's own
// first save, loaded and entered, and loaded and entered again after any input that loaded.
class Game {
public:
    Game()
      : m_host(test::TestManifest(), game::ARENA_SCENE, test::TravellingSmallData()) {
        core::Writer writer{ m_start };
        m_host.Save(writer);
        Restart();
    }

    game::Host& Host() {
        return m_host;
    }

    // Loads and enters the first save again.
    void Restart() {
        core::Reader reader{ m_start };
        if (!m_host.Load(reader, SAVED_AT)) {
            std::abort(); // every input after it would start from another state
        }
        m_host.EnterScene();
    }

private:
    game::Host m_host;
    std::vector<std::byte> m_start;
};

Game& TheGame() {
    // Never destroyed: it lives as long as the process, and the fuzzer may end that from any thread.
    [[clang::no_destroy]] static Game game;
    return game;
}

} // namespace

// Declared first: libFuzzer ships no header for it, and -Wmissing-prototypes requires one.
extern "C" i32 LLVMFuzzerTestOneInput(const u8* data, std::size_t size);

extern "C" i32 LLVMFuzzerTestOneInput(const u8* data, std::size_t size) {
    std::vector<std::byte> bytes = Bytes(data, size);
    Game& fuzzGame = TheGame();
    game::Host& host = fuzzGame.Host();
    core::Reader reader{ bytes };
    if (host.Load(reader, SAVED_AT)) {
        host.EnterScene(); // as the logic does at the swap
        constexpr u64 NEXT = SAVED_AT + 1;
        std::vector<core::Event> events;
        host.Tick({ .wall = NEXT, .simulation = NEXT, .state = NEXT }, {}, events);
        core::Jobs jobs{ 1 }; // here, not static: only inputs that load pay for its thread
        core::Snapshot snapshot;
        snapshot.simulationTick = NEXT;
        host.Publish(snapshot, jobs, std::nullopt);
        std::vector<std::byte> saved;
        core::Writer writer{ saved };
        host.Save(writer);
        fuzzGame.Restart();
    }
    return 0;
}
