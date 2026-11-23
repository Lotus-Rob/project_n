#pragma once

/// @file
/// @ingroup core
/// The frame contract: the plain data the logic thread hands the main
/// thread through core::Handover, and the input it gets back: timing and pausing, instances, view models and events.

#include "core/abilities.hpp"
#include "core/display_delay.hpp"
#include "core/game_data.hpp"
#include "core/handover.hpp"
#include "core/hit.hpp"
#include "core/manifest.hpp"
#include "core/names.hpp"
#include "core/profile.hpp"
#include "core/rect.hpp"
#include "core/seal.hpp"
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

/// One sprite as the GPU reads it from the instance storage buffer: INSTANCE_BYTES
/// (56) bytes, matching `Instance` in shaders/sprite.vert.hlsl field for field. Position and rotation
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
    /// The glow's inputs (SpriteDraw::glow): the role (core::GlowRole) in bits 0 to 7, the progress result in 8 to 15, the
    /// element (core::HitKind) in 16 to 23, and bit 24 set once a charge is released. 0 for no glow.
    u32 glow = 0;
    /// The glow's value at the previous tick, then at this one: two unorm16.
    u32 glowValue = 0;
    /// The low 32 bits of the glow's start tick.
    u32 glowStart = 0;
    /// SpriteDraw::flags: INSTANCE_FOGGED_LAND, INSTANCE_SIGHT_FADE and INSTANCE_FOG_SHADE. This word also keeps the shader's
    /// stride (56) at a multiple of 8, as DXC's SPIR-V StructuredBuffer needs when the struct holds a float2.
    u32 flags = 0;
};
/// The bytes of one Instance, which the shader's StructuredBuffer reads with the same layout.
inline constexpr std::size_t INSTANCE_BYTES = 56;
static_assert(sizeof(Instance) == INSTANCE_BYTES, "the shader reads instances as 56 bytes");
/// Instance::flags bit: the sprite stands on the land, so the fog's cloud covers it where its tile is unexplored.
inline constexpr u32 INSTANCE_FOGGED_LAND = 1U;
/// Instance::flags bit: the sprite fades out of the player's sight and into the fog's edge.
inline constexpr u32 INSTANCE_SIGHT_FADE = 2U;
/// Instance::flags bit: the sprite stands on the land but stays in colour out of sight, so the fog only shades it where its
/// tile is unexplored, hiding it under the clouds, and never drains or mists it (a place's marker). INSTANCE_FOGGED_LAND
/// wins over it.
inline constexpr u32 INSTANCE_FOG_SHADE = 4U;

/// How a batch of instances blends onto what is drawn under it: one
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

/// The game's own name for a scene, its place in the game's list of scenes: core passes it through without reading it, so
/// scenes that share a sprite set stay distinct.
enum class SceneKey : u16 {
};

/// What the logic and the main thread know of a scene.
struct SceneFacts {
    /// The game's name for it.
    SceneKey key{};
    /// The manifest's scene (sprite set) it draws with.
    SceneId sprites{};
    /// Whether it is a combat scene, where fights happen and casts act.
    bool combat = false;

    /// Whether two scenes' facts are alike, field by field.
    friend constexpr bool operator==(const SceneFacts&, const SceneFacts&) = default;
};

/// Where a scene change stands. Leaving, loading and entering run
/// no tick, count nothing and record nothing, so a change of any length leaves replays exact.
enum class SceneStage : u8 {
    Running,  ///< The scene runs: no change under way.
    Leaving,  ///< The old scene shows, frozen, while the main thread plays the way out, until it answers SceneLeft.
    Loading,  ///< The new scene is built and waits for the main thread to hold its sprites (SceneResident).
    Entering, ///< The new scene shows, frozen, while the main thread plays the way in, until it answers SceneEntered.
};

/// The scene change a snapshot is in, for the main thread to answer (core::SceneReports) and, later, to draw.
struct SceneChange {
    /// Its stage.
    SceneStage stage = SceneStage::Running;
    /// The scene it leaves; while running, the current scene.
    SceneFacts from{};
    /// The scene it enters; while running, the current scene.
    SceneFacts to{};
    /// Which change it is, counting the game's start as 1: the main thread's answers name it.
    u64 change = 0;
};

/// The change that entered the scene a snapshot in `change` shows: the change before while it is left (the old scene
/// still shows), its own otherwise. A view (ViewReport) names it, so a view of the old scene never culls the new one.
[[nodiscard]] constexpr u64 ShownChange(const SceneChange& change) {
    return change.stage == SceneStage::Leaving ? change.change - 1 : change.change;
}

/// What the main thread's window shows, as it last told the logic:
/// news like SceneResident, which changes what is published and never the state, so it is never recorded, hashed or saved.
struct ViewReport {
    /// The world the window shows, with a margin for the camera moving before a snapshot culled to it is drawn.
    Rect area{};
    /// The change that entered the scene it shows (ShownChange()): only a publish of that scene culls to it.
    u64 change = 0;
    /// Whether the player has turned on seeing the whole map through the fog: a debugging aid, heard whatever scene the
    /// view is of.
    bool debugSight = false;
};

/// How the camera moves over a scene.
enum class CameraKind : u8 {
    Follow, ///< It centres on what the snapshot follows (Snapshot::follow).
    Pan,    ///< It stays where it is until the player pans it.
    /// It follows what the snapshot follows until the player pans it, then stays where the player leaves it until
    /// snap back.
    Roam,
};

/// The entity the camera follows: its foot at the previous and this tick.
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
    Place,   ///< The name of a place on the world map, in the label pass: it reads at the widest zoom.
};

/// Which of two overlapping labels is shown: the higher.
enum class LabelPriority : u8 {
    Rest,     ///< Any other creature.
    Named,    ///< A creature with its own name.
    Place,    ///< A place's name.
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

/// How the fog hides a text.
enum class TextFog : u8 {
    None,     ///< Always shown.
    Sight,    ///< Shown only where its anchor is in the player's sight and not on land the clouds hide.
    Explored, ///< Shown only where its anchor's tile has been explored.
};

/// A text the main thread draws over the world: its size in world units scales with the
/// zoom, and its anchor interpolates by the render clock's alpha, as instances do.
struct TextItem {
    /// What it is.
    TextKind kind = TextKind::Label;
    /// What it says (Label, Caption), in the language the renderer shows; ignored when `npcName` is set.
    StringId string = StringId::CultivatorName;
    /// Of a Label: an NPC's name, which it says instead of `string`, composed from the renderer's names table in the
    /// language shown (core::NameText()). Its stacking key is core::NameKey() of it with the top bit set (gfx::LabelKey()),
    /// whatever `nameKey` holds.
    std::optional<NpcName> npcName = std::nullopt;
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
    /// The entity it names or belongs to; zero for none (a caption). A Place needs its own non-zero entity that no
    /// creature uses: the label pass tells labels apart by it.
    StableId entity{};
    /// Of a Label or a Place: the named entity's height, in world units; 0 when unknown, which never counts as too small to
    /// see.
    f32 height = 0.0F;
    /// Which labels may stack: those with the same key. A Place needs a key of its own per name, or places stack.
    u32 nameKey = 0;
    /// Of a Label or a Place: which wins where labels overlap.
    LabelPriority priority = LabelPriority::Rest;
    /// How the fog hides it.
    TextFog fog = TextFog::None;
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

/// A burst at a point as the simulation reports it (the poison lance's strike), for the main thread to draw at the render
/// time in its element's colour (core::UiColours): the simulation never colours it.
struct Flash {
    /// Where it bursts, in world units.
    Vec2 point{};
    /// The simulation tick it starts at.
    u64 startTick = 0;
    /// How many ticks it lasts (CombatRules::flashTicks as it was cast), at least 1: it shows from its start tick to the
    /// tick before this many after it.
    u64 ticks = 1;
    /// The kind of the hits it came with: it picks the colour, never the ability.
    HitKind element = HitKind::Damage;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.point, self.startTick, self.ticks, self.element);
    }
};

/// The world's calendar as the simulation reports it, for the date line: derived from the world clock.
struct CalendarView {
    /// How many months have passed, from 0.
    u64 month = 0;
    /// The day of the month, from 0.
    u32 day = 0;
    /// Whether the player has paused the world.
    bool paused = false;

    /// Whether two views are alike, field by field.
    friend constexpr bool operator==(const CalendarView&, const CalendarView&) = default;
};

/// The fog of war on the world map, as the main thread draws it.
struct FogView {
    /// Whether the scene has fog: false elsewhere, where the rest is stale.
    bool on = false;
    /// Changes whenever `explored` does, so the renderer uploads it only then.
    u64 version = 0;
    /// The explored tiles, one bit each, row by row (core/fog.hpp); refilled in place, its capacity kept.
    // NOLINTNEXTLINE(readability-redundant-member-init): designated initializers omit it; Clang warns without one
    std::vector<u64> explored{};
    /// The tile the player stands on, the centre of its sight; none without a player.
    std::optional<u32> playerTile = std::nullopt;
    /// How far the player sees, in tiles.
    u32 sightRadius = 0;
    /// Whether the debug aid is on: every tile counts as in sight, and the land not yet explored stays under cloud.
    bool debugSight = false;
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
    /// Whether a calendar step runs next: the main thread shows the transition
    /// and caps its frame rate, leaving the cores to the step, until a snapshot without it arrives.
    bool transition = false;
    /// The scene its state needs (Simulation::Scene()): drawn only once the main thread holds it, and the ticks
    /// wait until it says so.
    SceneId scene{};
    /// The scene change it is in, set by core::Logic on every publish: its stage and the scenes on either side.
    SceneChange sceneChange{};
    /// The sprites to draw, in drawing order. The buffer keeps its capacity from publish to
    /// publish: the publisher clears and refills it, never assigns a new one.
    // NOLINTNEXTLINE(readability-redundant-member-init): designated initializers omit it; Clang warns without one
    std::vector<Instance> instances{};
    /// The instances' batches, in drawing order, covering them all; refilled in place likewise.
    // NOLINTNEXTLINE(readability-redundant-member-init): designated initializers omit it; Clang warns without one
    std::vector<Batch> batches{};
    /// How many of `batches`, from the first, draw only the ground and its decals (core::Layer::Ground and Decals): the
    /// renderer draws the ground seals after them, under the world.
    u32 groundBatches = 0;
    /// Whether the scene shows the world map (game::SceneTraits::worldMap): the renderer draws it, from the map and the look
    /// it was given, under the ground and everything else, so the snapshot holds none of its tiles.
    bool worldMap = false;
    /// The entity each instance draws, parallel to `instances` (zero for one that is no entity's:
    /// the ground, a shadow), for picking; refilled in place likewise.
    // NOLINTNEXTLINE(readability-redundant-member-init): designated initializers omit it; Clang warns without one
    std::vector<StableId> ids{};
    /// The texts drawn over the world, in drawing order; refilled in place likewise.
    // NOLINTNEXTLINE(readability-redundant-member-init): designated initializers omit it; Clang warns without one
    std::vector<TextItem> texts{};
    /// The health of each living enemy in a combat scene; refilled in place likewise.
    // NOLINTNEXTLINE(readability-redundant-member-init): designated initializers omit it; Clang warns without one
    std::vector<HealthBar> healthBars{};
    /// The volleys in flight, the bolts among them, until their last sword has faded; refilled in place likewise.
    // NOLINTNEXTLINE(readability-redundant-member-init): designated initializers omit it; Clang warns without one
    std::vector<VolleyView> volleys{};
    /// Every volley's targets, each volley's together (VolleyView::firstTarget); refilled in place likewise.
    // NOLINTNEXTLINE(readability-redundant-member-init): designated initializers omit it; Clang warns without one
    std::vector<VolleyTarget> volleyTargets{};
    /// The bursts showing, each for its `ticks` from its start; refilled in place likewise.
    // NOLINTNEXTLINE(readability-redundant-member-init): designated initializers omit it; Clang warns without one
    std::vector<Flash> flashes{};
    /// The seals in play, each from its start tick to the tick before its end; refilled in place likewise.
    // NOLINTNEXTLINE(readability-redundant-member-init): designated initializers omit it; Clang warns without one
    std::vector<Seal> seals{};
    /// Whether the scene is a combat scene, where casts act.
    bool combat = false;
    /// The player's Qi, health and cooldowns in a combat scene with a player; none otherwise.
    std::optional<CasterView> caster = std::nullopt;
    /// The world's calendar in a scene that shows the world map (`worldMap`); none elsewhere.
    std::optional<CalendarView> calendar = std::nullopt;
    /// The fog of war on the world map; `on` is false until the simulation turns it on, each publish.
    FogView fog{};
    /// The player's remaining route on the world map: its foot where it is drawn (on the road it walks), then the tiles
    /// still to walk as they will be drawn (on a road its curve, else the centre; the clicked end its centre); refilled in
    /// place each publish and empty for none.
    // NOLINTNEXTLINE(readability-redundant-member-init): designated initializers omit it; Clang warns without one
    std::vector<Vec2> route{};
    /// What the camera follows, if anything.
    std::optional<Follow> follow = std::nullopt;
    /// How the camera moves over the scene: Pan unless the simulation says otherwise.
    CameraKind camera = CameraKind::Pan;
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
    CycleScene,      ///< Switch to the next scene (a debug key): the simulation asks for it, and the logic has it built.
    SceneResident,   ///< The main thread holds the scene `scene` and can draw it: never in a replay.
    DebugBurst,      ///< Hit every fox thousands of times (a debug key), for the damage numbers' stress.
    Cast,            ///< Use the ability in the slot `slot` at the world point `point`: one per press, in a combat scene.
    SceneLeft,       ///< The main thread has played the way out of scene change `change`: never in a replay.
    SceneEntered,    ///< The main thread has played the way into scene change `change`: never in a replay.
    /// Pause the world's clock, or resume it, in a scene that shows the world map: the simulation ignores it elsewhere.
    ToggleWorldPause,
    /// Walk the player's cultivator to the tile under the world point `point` by the cheapest way on foot, in a scene that
    /// shows the world map: the simulation ignores it elsewhere.
    WalkTo,
};
/// How many kinds ActionKind names (WalkTo is the last): a replay read from disk holds only kinds below this.
inline constexpr u8 ACTION_KINDS = static_cast<u8>(std::to_underlying(ActionKind::WalkTo) + 1U);

/// A player's or the main thread's request to the simulation, sent as input: a press, taken once.
struct Action {
    /// What it asks for.
    ActionKind kind = ActionKind::Pause;
    /// The entity it is about (Select); zero otherwise.
    StableId target{};
    /// The game data to swap in (ReloadData); empty otherwise.
    std::shared_ptr<const GameData> data = nullptr;
    /// The scene now resident (SceneResident); zero otherwise.
    SceneId scene{};
    /// The slot whose ability is cast (Cast); the first slot otherwise.
    Slot slot = Slot::Generator;
    /// Where it is cast (Cast) or where to walk to (WalkTo), in world units; the origin otherwise.
    Vec2 point{};
    /// The scene change it answers (SceneLeft, SceneEntered); zero otherwise. A replay never holds it.
    u64 change = 0;
};

/// What the player holds down, as the main thread last sent it: every tick of
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
using GameHandover = Handover<Snapshot, Event, Action, HeldInput, ViewReport>;
/// The main thread's side of GameHandover's state channel.
using GameDisplay = Display<Snapshot, Event, Action, HeldInput, ViewReport>;

/// The main loop's frame. While `visible` and a snapshot is drawn, first `acquire()`s the swapchain texture, which may
/// block. Then reads `now()` (ns) once, for all of the frame: D from `delay` (DisplayDelay::Advance()), the snapshot to
/// draw (`display`'s Display::Update() from `handover`; each snapshot it takes tells `delay` its lateness), alpha and the
/// render clock. After the takes it moves the events published so far to the end of `pending`, every frame, drawing or
/// not. With nothing drawn yet, returns true having acquired nothing. Else calls `prepare(drawn)` and returns
/// false if it fails; acquires now if it didn't before (the first snapshot), plays the `pending` events due by the
/// render clock (PlayDue()) and, while `visible`, calls `draw(drawn, alpha)`; returns true. Hidden, D and the snapshot
/// still advance and events still play. `prepare` is `bool(const Snapshot&)`, `draw` `void(const Snapshot&, f64)`.
/// A false return can leave an acquire without its draw (a visible frame that already had a snapshot acquired before
/// `prepare` failed), so the caller treats it as fatal and never presents again.
template<typename Acquire, typename Now, typename Prepare, typename Play, typename Draw>
bool PresentFrame(
    GameDisplay& display,
    GameHandover& handover,
    DisplayDelay& delay,
    std::vector<Event>& pending,
    bool visible,
    Acquire acquire,
    Now now,
    Prepare prepare,
    Play play,
    Draw draw
) {
    PN_PROFILE_ZONE();
    // The acquire can wait most of a frame: the snapshot is chosen after it, so one that falls due meanwhile is drawn
    // now, not a frame later. Before the first snapshot there may be nothing to draw, and an acquire must be drawn.
    const bool acquired = visible && display.Drawn() != nullptr;
    if (acquired) {
        acquire();
    }
    const u64 nowNs = now();
    const u64 delayNs = delay.Advance(nowNs);
    static_cast<void>(display.Update(handover, nowNs, delayNs));
    while (const std::optional<u64> lateness = display.TakenLateness()) {
        delay.Observe(*lateness);
    }
    // After the takes, so every snapshot the display holds has its events in `pending` before they play, even one
    // published during the acquire.
    handover.AppendEvents(pending);
    const Snapshot* drawn = display.Drawn();
    if (drawn == nullptr) {
        return true;
    }
    if (!prepare(*drawn)) {
        return false;
    }
    if (visible && !acquired) {
        acquire();
    }
    const f64 alpha = Alpha(drawn->scheduledNs, nowNs, delayNs, drawn->advanced);
    PlayDue(pending, drawn->epoch, RenderTick(drawn->simulationTick, alpha), play);
    if (visible) {
        draw(*drawn, alpha);
    }
    return true;
}

} // namespace core
