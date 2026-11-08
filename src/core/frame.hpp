#pragma once

/// @file
/// @ingroup core
/// The frame contract (design doc 0002 §4, §5): the plain data the logic thread hands the main
/// thread through core::Handover, and the input it gets back. It grows with each phase of §16
/// (instances, view models and events in phase 3 onwards); phase 1 carries timing and pausing.

#include "core/game_data.hpp"
#include "core/handover.hpp"
#include "core/profile.hpp"
#include "core/stable_id.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

#include <bit>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <utility>
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

/// How a batch of instances blends onto what is drawn under it (design doc 0002 §8.8): one
/// pipeline each.
enum class Pipeline : u8 {
    Normal,   ///< Premultiplied alpha over: ONE, ONE_MINUS_SRC_ALPHA.
    Additive, ///< Light added to what's under it: ONE, ONE.
};
/// How many pipelines Pipeline names.
inline constexpr std::size_t PIPELINES = 2;

/// A run of consecutive instances drawn with one pipeline: one draw call.
struct Batch {
    /// Its first instance in Snapshot::instances.
    u32 first = 0;
    /// How many instances it draws.
    u32 count = 0;
    /// The pipeline it draws with.
    Pipeline pipeline = Pipeline::Normal;
};

/// The entity the camera follows (design doc 0002 §8.10): its foot at the previous and this tick.
struct Follow {
    /// The foot point at the previous tick, in world units.
    Vec2 previous{};
    /// The foot point at this tick.
    Vec2 current{};
};

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
    /// Whether a calendar step runs next (design doc 0002 §7.3): the main thread shows the transition
    /// and caps its frame rate, leaving the cores to the step, until a snapshot without it arrives.
    bool transition = false;
    /// The sprites to draw, in drawing order. The buffer keeps its capacity from publish to
    /// publish: the publisher clears and refills it, never assigns a new one.
    std::vector<Instance> instances{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// The instances' batches, in drawing order, covering them all; refilled in place likewise.
    std::vector<Batch> batches{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// The entity each instance draws, parallel to `instances` (zero for one that is no entity's:
    /// the ground, a shadow), for picking; refilled in place likewise.
    std::vector<StableId> ids{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// What the camera follows, if anything.
    std::optional<Follow> follow = std::nullopt;
    /// The index in `instances` of the selected entity's instance, which the renderer outlines; none
    /// when nothing is selected or it isn't drawn.
    std::optional<u32> selected = std::nullopt;
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
    Pause,           ///< Freeze the simulation tick.
    Resume,          ///< Run the simulation tick again.
    AdvanceCalendar, ///< Let a month of world time pass: a calendar step behind a transition.
    Save,            ///< Save the game to the save file.
    Load,            ///< Load the game from the save file: a new timeline.
    ToggleRecording, ///< Start recording a replay, or finish and write the one being recorded.
    PlayReplay,      ///< Play the replay file back from its start (a new timeline), or stop playing it.
    Select,          ///< Select the entity `target` names (a stale or zero ID selects nothing).
    ReloadData,      ///< Swap in the game data `data` (debug builds' hot reload): never in a replay.
};
/// How many kinds ActionKind names (ReloadData is the last): a replay read from disk holds only
/// kinds below this.
inline constexpr u8 ACTION_KINDS = static_cast<u8>(std::to_underlying(ActionKind::ReloadData) + 1U);

/// A player's or the main thread's request to the simulation, sent as input: a press, taken once.
struct Action {
    /// What it asks for.
    ActionKind kind = ActionKind::Pause;
    /// The entity it is about (Select); zero otherwise.
    StableId target{};
    /// The game data to swap in (ReloadData); empty otherwise.
    std::shared_ptr<const GameData> data{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
};

/// What the player holds down, as the main thread last sent it (design doc 0002 §10): every tick of
/// a burst gets the newest, so movement goes on through a catch-up burst.
struct HeldInput {
    /// Where the movement controls point: each component in [-1, 1], at most 1 long; zero at rest.
    Vec2 move{};

    /// Whether two are bit for bit the same, as a replay must play them back.
    [[nodiscard]] friend constexpr bool operator==(const HeldInput& lhs, const HeldInput& rhs) {
        return std::bit_cast<u64>(lhs.move) == std::bit_cast<u64>(rhs.move);
    }
};

/// One tick's input: the presses it takes and what is held down.
struct TickInput {
    /// The actions, in the order sent.
    std::span<const Action> actions;
    /// The held input.
    HeldInput held{};
};

/// The handover the game's two threads share.
using GameHandover = Handover<Snapshot, Event, Action, HeldInput>;
/// The main thread's side of GameHandover's state channel.
using GameDisplay = Display<Snapshot, Event, Action, HeldInput>;

/// The main loop's frame once `drawn` is chosen (design doc 0002 §6): while `visible`, `acquire()`s the swapchain
/// texture, which may block; then reads `now()` (ns) once for alpha and the render clock, plays the `pending` events due
/// by it (PlayDue()) and, while `visible`, calls `draw(alpha)`. Hidden, the clock and events still advance.
template<typename Acquire, typename Now, typename Play, typename Draw>
void PresentFrame(
    const Snapshot& drawn,
    std::vector<Event>& pending,
    bool visible,
    Acquire acquire,
    Now now,
    Play play,
    Draw draw
) {
    PN_PROFILE_ZONE();
    if (visible) {
        acquire();
    }
    const f64 alpha = Alpha(drawn.scheduledNs, now(), DEFAULT_DISPLAY_DELAY_NS, drawn.advanced);
    PlayDue(pending, drawn.epoch, RenderTick(drawn.simulationTick, alpha), play);
    if (visible) {
        draw(alpha);
    }
}

} // namespace core
