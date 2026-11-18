#pragma once

/// @file
/// Exactness checks for host saves: a game that saved, one that never did and one loaded from the save, run side by side
/// and compared hash by hash and draw by draw.

#include "core/jobs.hpp"
#include "core/types.hpp"
#include "game/host.hpp"
#include "support/host.hpp"

#include <vector>

namespace test {

/// The three games an exactness test runs side by side: one that saved, one that never did and one loaded from the save.
struct Games {
    /// The game that saved.
    game::Host* saved;
    /// The same game, never saved.
    game::Host* neverSaved;
    /// The game loaded from the save.
    game::Host* loaded;
};

/// Ticks `games` from tick `from` for `ticks` ticks, then runs a month step on `jobs`, counting the checks where the
/// loaded or the never-saved game hashes unlike the saved one: one at the start, one every `every` ticks and one after the
/// step; then one more where either draws unlike it.
inline u32 Unlike(const Games& games, core::Jobs& jobs, u64 from, u64 ticks, u64 every) {
    u32 unlike = 0;
    const auto compare = [&games, &unlike] {
        const u64 hash = games.saved->StateHash();
        unlike += games.loaded->StateHash() == hash && games.neverSaved->StateHash() == hash ? 0U : 1U;
    };
    compare();
    for (u64 tick = from; tick < from + ticks; ++tick) {
        for (game::Host* host : { games.saved, games.neverSaved, games.loaded }) {
            Tick(*host, At(tick), {});
        }
        if ((tick - from + 1) % every == 0) {
            compare();
        }
    }
    for (game::Host* host : { games.saved, games.neverSaved, games.loaded }) {
        host->AdvanceCalendar(At(from + ticks), jobs, {});
    }
    compare();
    const std::vector<core::Instance> drawn = Published(*games.saved, jobs);
    const bool same = SameBytes(Published(*games.loaded, jobs), drawn) && SameBytes(Published(*games.neverSaved, jobs), drawn);
    return unlike + (same ? 0U : 1U);
}

/// Unlike() on 1 worker, then on 4: only the month step and the publish fork, so each worker count gets its own month
/// step and draw after the ticks, which run alike on either.
inline u32 UnlikeOnBothWorkerCounts(const Games& games, u64 from, u64 ticks, u64 every) {
    core::Jobs one{ 1 };
    core::Jobs four{ 4 };
    const u32 unlike = Unlike(games, one, from, ticks, every);
    return unlike + Unlike(games, four, from + ticks + 1, 0, 1);
}

} // namespace test
