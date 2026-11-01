#pragma once

/// @file
/// @ingroup core
/// The frame contract (design doc 0002 §4, §5): the plain data the logic thread hands the main
/// thread through core::Handover, and the input it gets back. It grows with each phase of §16
/// (instances, view models and events in phase 3 onwards); phase 1 carries timing and pausing.

#include "core/handover.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

#include <cstddef>
#include <vector>

namespace core {

/// One sprite as the GPU reads it from the instance storage buffer (design doc 0002 §8.5): 40
/// bytes, matching `Instance` in shaders/sprite.vert.hlsl field for field. Position and rotation
/// interpolate from previous to current by the render clock's alpha; size, region and colour snap
/// at the tick. The 16-bit fields are packed in pairs into u32s (core/instances.hpp), low half
/// first, as the shader unpacks them.
struct Instance {
    /// The quad's centre at the previous tick, in world units.
    Vec2 previous{};
    /// The quad's centre at this tick.
    Vec2 current{};
    /// Width, then height, in world units: two f16.
    u32 size = 0;
    /// Clockwise rotation about the centre in radians, at the previous tick, then at this one: two f16.
    u32 rotation = 0;
    /// The region's top-left corner on its page, as fractions of the page: u, then v, unorm16.
    u32 regionMin = 0;
    /// The region's bottom-right corner, likewise.
    u32 regionMax = 0;
    /// The texture-array layer (the atlas page), then flags (none defined yet): two u16.
    u32 layerFlags = 0;
    /// Premultiplied colour the texture is multiplied by: red in the low byte, then green, blue and alpha.
    u32 colour = 0;
};
/// The bytes of one Instance, which the shader's StructuredBuffer reads with the same layout.
inline constexpr std::size_t INSTANCE_BYTES = 40;
static_assert(sizeof(Instance) == INSTANCE_BYTES, "the shader reads instances as 40 bytes");

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
    /// The sprites to draw, in drawing order. The buffer keeps its capacity from publish to
    /// publish: the publisher clears and refills it, never assigns a new one.
    std::vector<Instance> instances{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
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
