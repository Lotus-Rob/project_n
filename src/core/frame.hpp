#pragma once

/// @file
/// @ingroup core
/// The frame contract (design doc 0002 §4, §5): the plain data the logic thread hands the main
/// thread through core::Handover, and the input it gets back. It grows with each phase of §16
/// (instances, view models and events in phase 3 onwards); phase 1 carries timing and pausing.

#include "core/abilities.hpp"
#include "core/game_data.hpp"
#include "core/handover.hpp"
#include "core/hit.hpp"
#include "core/manifest.hpp"
#include "core/profile.hpp"
#include "core/stable_id.hpp"
#include "core/strings.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "core/volley.hpp"

#include <array>
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
    /// The texture-array layer (the atlas page), then how far it has dissolved (SpriteDraw::dissolve) as unorm16: 0
    /// draws it whole, 65535 not at all.
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

/// Which way a text's characters run.
enum class TextDirection : u8 {
    LeftToRight, ///< In lines across.
    TopToBottom, ///< In a column down, as a vertical caption.
};

/// What a text is, which sets its size in world units and how it behaves.
enum class TextKind : u8 {
    Label,   ///< A name over a creature's head.
    Caption, ///< A caption on the scenery.
    Number,  ///< A damage or healing number.
};

/// Which of two overlapping labels is shown: the higher.
enum class LabelPriority : u8 {
    Rest,     ///< Any other creature.
    Named,    ///< A creature with its own name.
    Player,   ///< The player's.
    Hovered,  ///< The hovered entity's.
    Selected, ///< The selected entity's.
};

/// Whether a number shows its sign.
enum class NumberSign : u8 {
    Plain, ///< No sign: damage.
    Plus,  ///< A "+" before it: healing.
};

/// Where a text's anchor sits on its box.
enum class TextAlign : u8 {
    BottomCentre, ///< The text stands on its anchor: a label over a head.
    TopCentre,    ///< The text hangs from its anchor: a vertical caption.
};

/// Opaque white, premultiplied and packed as TextItem::colour: a text's colour unless it names another.
inline constexpr u32 TEXT_WHITE = 0xFFFF'FFFFU;

/// A text the main thread draws over the world (the phase 6 chunk 3 spec, "Labels: placement, stacking and
/// priority"): its size in world units scales with the zoom, and its anchor interpolates by the render clock's alpha,
/// as instances do.
struct TextItem {
    /// What it is.
    TextKind kind = TextKind::Label;
    /// What it says (Label, Caption), in the language the renderer shows.
    StringId string = StringId::CultivatorName;
    /// Of a Number: its value, its digits all shown however many.
    i64 value = 0;
    /// Of a Number: whether it shows a "+".
    NumberSign sign = NumberSign::Plain;
    /// The anchor at the previous tick, in world units.
    Vec2 previous{};
    /// The anchor at this tick.
    Vec2 current{};
    /// Moves it from the anchor, in world units.
    Vec2 offset{};
    /// Of a Number: how far its start is moved from its anchor, in ems of its size (x right, y down), so numbers on one
    /// creature at once don't print over each other.
    Vec2 scatter{};
    /// Of a Number: how far above its anchor (moved by `scatter`) it has risen, in ems of its size.
    f32 lift = 0.0F;
    /// How much of it shows, from 0 (none) to 1 (all).
    f32 opacity = 1.0F;
    /// The entity it names or belongs to; zero for none (a caption).
    StableId entity{};
    /// Of a Label: the named entity's height, in world units; 0 when unknown, which never counts as too small to see.
    f32 height = 0.0F;
    /// Which labels may stack: those with the same key.
    u32 nameKey = 0;
    /// Of a Label: which wins where labels overlap.
    LabelPriority priority = LabelPriority::Rest;
    /// Of a Number: its premultiplied linear colour (the scene is drawn in linear space): red in the low byte, then green,
    /// blue and alpha. A Label and a Caption leave it alone: the renderer draws them in core::UiColours' colours.
    u32 colour = TEXT_WHITE;
    /// Where the anchor sits on it.
    TextAlign align = TextAlign::BottomCentre;
    /// Which way its characters run.
    TextDirection direction = TextDirection::LeftToRight;
};

/// A creature's health as the simulation reports it, for the main thread to draw as a bar over its head in
/// core::UiColours' colours: the simulation never colours it.
struct HealthBar {
    /// The creature.
    StableId entity{};
    /// Its head at the previous tick, in world units (as its label's anchor).
    Vec2 previous{};
    /// Its head at this tick.
    Vec2 current{};
    /// Its health left as a fraction of its most, from 0 to 1.
    f32 fraction = 1.0F;
};

/// The player's casting state as the simulation reports it, for the HUD: the main thread draws the bars and the cooldowns
/// in core::UiColours' colours, as the simulation colours nothing.
struct CasterView {
    /// The Qi the player holds.
    i64 qi = 0;
    /// The most Qi it holds.
    i64 qiMax = 0;
    /// The player's health.
    i64 health = 0;
    /// The player's most health.
    i64 healthMax = 0;
    /// The simulation tick each slot can next be cast at: a slot is cooling down while this is past the snapshot's tick.
    std::array<u64, SLOTS> readyTick{};
    /// The ability in each slot.
    std::array<AbilityKind, SLOTS> loadout{};
};

/// A volley in flight as the simulation reports it (core/volley.hpp), for the main thread to draw at the render time:
/// its targets are `targets` entries of Snapshot::volleyTargets from `firstTarget`, which SwordTarget() and SwordAim()
/// take as they are.
struct VolleyView {
    /// The volley as it was cast.
    Volley volley{};
    /// The ability that cast it: it picks only the sprite.
    AbilityKind ability = AbilityKind::None;
    /// The kind of its hits: it picks the effects' colour (core::UiColours), never the ability.
    HitKind element = HitKind::Damage;
    /// Its first target in Snapshot::volleyTargets.
    u32 firstTarget = 0;
    /// How many targets it has there, at least one.
    u32 targets = 0;
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
    /// The scene its state needs (Simulation::Scene()): drawn only once the main thread holds it, and the ticks
    /// wait until it says so (design doc 0002.3.1 §4).
    SceneId scene{};
    /// The sprites to draw, in drawing order. The buffer keeps its capacity from publish to
    /// publish: the publisher clears and refills it, never assigns a new one.
    std::vector<Instance> instances{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// The instances' batches, in drawing order, covering them all; refilled in place likewise.
    std::vector<Batch> batches{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// The entity each instance draws, parallel to `instances` (zero for one that is no entity's:
    /// the ground, a shadow), for picking; refilled in place likewise.
    std::vector<StableId> ids{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// The texts drawn over the world, in drawing order; refilled in place likewise.
    std::vector<TextItem> texts{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// The health of each living enemy in a combat scene; refilled in place likewise.
    std::vector<HealthBar> healthBars{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// The volleys in flight, the bolts among them, until their last sword has faded; refilled in place likewise.
    std::vector<VolleyView> volleys{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// Every volley's targets, each volley's together (VolleyView::firstTarget); refilled in place likewise.
    std::vector<VolleyTarget>
        volleyTargets{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// Whether the scene is a combat scene, where casts act.
    bool combat = false;
    /// The player's Qi, health and cooldowns in a combat scene with a player; none otherwise.
    std::optional<CasterView> caster = std::nullopt;
    /// What the camera follows, if anything.
    std::optional<Follow> follow = std::nullopt;
    /// The index in `instances` of the selected entity's instance, which the renderer outlines; none
    /// when nothing is selected or it isn't drawn.
    std::optional<u32> selected = std::nullopt;
};

/// What an Event says happened.
enum class EventKind : u8 {
    Hit,         ///< A hit on `target`: its `hit` kind and `value`, at `position`, from the ability use `use`.
    NotEnoughQi, ///< A cast was refused for want of Qi: `value` is the slot (core::Slot) that was cast.
    UseEnded,    ///< The ability use `use` has emitted all its hits. Every use emits exactly one, after its last hit, even
                 ///< one cut short (its user's death, a scene change): combined damage numbers wait for it.
};

/// The ID of one use of an ability: `tick` is the simulation tick it happens at and `source` says which ability it is
/// (below 256), so two uses on one tick differ and a use's hits and its end share the ID.
[[nodiscard]] constexpr u64 AbilityUse(u64 tick, u32 source) {
    constexpr u32 SOURCE_BITS = 8;
    return (tick << SOURCE_BITS) | source;
}

/// Something a tick started that the main thread plays at the render clock (sounds, particle
/// bursts, damage numbers). So far: hits, and the end of the use that dealt them. Neither is cosmetic: none is dropped.
struct Event {
    /// The simulation tick it happens at; the logic stamps it.
    u64 tick = 0;
    /// The timeline it belongs to; the logic stamps it.
    u64 epoch = 0;
    /// Whether it may be dropped when it falls far behind (a sound may; a residency hint may not).
    bool cosmetic = false;
    /// What happened.
    EventKind kind = EventKind::Hit;
    /// Of a Hit: its kind.
    HitKind hit = HitKind::Damage;
    /// The ability use it belongs to (AbilityUse()).
    u64 use = 0;
    /// Of a Hit: the entity hit.
    StableId target{};
    /// Of a Hit: where it lands, in world units.
    Vec2 position{};
    /// Of a Hit: the amount (game::HitAmount()). Of a NotEnoughQi: the slot cast.
    i64 value = 0;
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
    CycleScene,      ///< Switch to the next scene (a debug key): the simulation rebuilds itself as it.
    SceneResident,   ///< The main thread holds the scene `scene` and can draw it: never in a replay.
    DebugBurst,      ///< Hit every fox thousands of times (a debug key), for the damage numbers' stress.
    Cast,            ///< Use the ability in the slot `slot` at the world point `point`: one per press, in a combat scene.
};
/// How many kinds ActionKind names (Cast is the last): a replay read from disk holds only
/// kinds below this.
inline constexpr u8 ACTION_KINDS = static_cast<u8>(std::to_underlying(ActionKind::Cast) + 1U);

/// A player's or the main thread's request to the simulation, sent as input: a press, taken once.
struct Action {
    /// What it asks for.
    ActionKind kind = ActionKind::Pause;
    /// The entity it is about (Select); zero otherwise.
    StableId target{};
    /// The game data to swap in (ReloadData); empty otherwise.
    std::shared_ptr<const GameData> data{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// The scene now resident (SceneResident); zero otherwise.
    SceneId scene{};
    /// The slot whose ability is cast (Cast); the first slot otherwise.
    Slot slot = Slot::Generator;
    /// Where it is cast, in world units (Cast); the origin otherwise.
    Vec2 point{};
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
