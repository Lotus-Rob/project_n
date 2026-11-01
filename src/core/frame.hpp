#pragma once

/// @file
/// @ingroup core
/// The frame contract (design doc 0002 §4, §5): the plain data the logic thread hands the main
/// thread through core::Handover, and the input it gets back. It grows with each phase of §16
/// (instances, view models and events in phase 3 onwards); phase 1 carries timing and pausing.

#include "core/handover.hpp"
#include "core/types.hpp"

namespace core {

/// One published snapshot of the game state: what the main thread draws until the next is due.
struct Snapshot {
    /// The wall tick of the last tick in the burst that published it.
    u64 wallTick = 0;
    /// The simulation tick it shows; the render clock runs from one before it.
    u64 simulationTick = 0;
    /// The timeline it belongs to; it changes when a save is loaded, a new game or a replay starts.
    u64 epoch = 0;
    /// Its tick's scheduled time on the shared clock (TickBurst::scheduledNs).
    u64 scheduledNs = 0;
    /// When the logic thread published it, on the same clock: after its due time, it was late.
    u64 publishedNs = 0;
    /// Whether the simulation ran in its tick: false on frozen ticks (paused, loading), whose
    /// snapshots show current transforms only.
    bool advanced = false;
};

/// Something a tick started that the main thread plays at the render clock (sounds, particle
/// bursts, damage numbers). The kinds of event arrive with the phases that play them.
struct Event {
    /// The simulation tick it happens at.
    u64 tick = 0;
    /// The timeline it belongs to.
    u64 epoch = 0;
    /// Whether it may be dropped when it falls far behind (a sound may; a residency hint may not).
    bool cosmetic = false;
};

/// What an action asks the simulation to do.
enum class ActionKind : u8 {
    Pause,  ///< Freeze the simulation tick.
    Resume, ///< Run the simulation tick again.
};

/// A player's or the main thread's request to the simulation, sent as input.
struct Action {
    /// What it asks for.
    ActionKind kind = ActionKind::Pause;
};

/// The handover the game's two threads share.
using GameHandover = Handover<Snapshot, Event, Action>;
/// The main thread's side of GameHandover's state channel.
using GameDisplay = Display<Snapshot, Event, Action>;

} // namespace core
