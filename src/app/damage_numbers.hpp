#pragma once

/// @file
/// @ingroup app
/// Damage numbers (the phase 6 chunk 3 spec, "Damage numbers"): every hit the simulation reports plays as a number over
/// its target, coloured by its kind, that bounces or rises (the motion setting) and fades over NUMBER_SECONDS of the
/// render clock; with the combined setting, one use's hits on one target of one kind add into one number, which plays
/// once that use ends. Numbers hitting one target in one tick each start apart and later (ScatterOf()), the more so
/// the more hits it took, so they rise as a fountain rather than print over each other; combined numbers starting on
/// one target together start apart likewise, but at once. From a hash of the hit, so every run and replay draws them the
/// same. None is ever dropped or cut short: the pool has no cap. Pure, tested
/// without a device.

#include "core/frame.hpp"
#include "core/settings.hpp"
#include "core/stable_id.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

#include <array>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace app {

/// How long a number shows, from its start to gone, in seconds of the render clock.
inline constexpr f32 NUMBER_SECONDS = 1.0F;
/// When a number starts fading out, in seconds after its start: it fades linearly to nothing at NUMBER_SECONDS.
inline constexpr f32 NUMBER_FADE_START = 0.6F;
/// How far a rising number has risen at NUMBER_SECONDS, in ems of its size.
inline constexpr f32 RISE_EMS = 1.5F;
/// How high a bouncing number hops, in ems of its size, which it reaches BOUNCE_TOP_SECONDS after its start.
inline constexpr f32 BOUNCE_TOP_EMS = 1.0F;
/// When a bouncing number is at the top of its hop, in seconds after its start.
inline constexpr f32 BOUNCE_TOP_SECONDS = 0.15F;
/// Where a bouncing number settles after its hop, in ems of its size, which it reaches BOUNCE_REST_SECONDS after its
/// start and holds while it fades.
inline constexpr f32 BOUNCE_REST_EMS = 0.6F;
/// When a bouncing number has settled, in seconds after its start.
inline constexpr f32 BOUNCE_REST_SECONDS = 0.4F;
/// How far a number's start may be moved left or right of its anchor, in ems of its size, while its target took at most
/// NUMBER_SCATTER_GROWTH_HITS hits in its tick: numbers hitting one creature at once spread so their digits don't print
/// over each other. More hits widen it by ScatterGrowth().
inline constexpr f32 NUMBER_SCATTER_X_EMS = 0.75F;
/// How far a number's start may be moved up or down from its anchor, in ems of its size, likewise.
inline constexpr f32 NUMBER_SCATTER_Y_EMS = 0.35F;
/// The hits on one target in one tick up to which the scatter keeps its narrowest bounds.
inline constexpr u32 NUMBER_SCATTER_GROWTH_HITS = 4;
/// The most the scatter's bounds widen by, reached at 16 × NUMBER_SCATTER_GROWTH_HITS hits.
inline constexpr f32 NUMBER_SCATTER_GROWTH_MAX = 4.0F;
/// How much the window a number's start is delayed within grows per hit its target took in that tick, in seconds.
inline constexpr f32 NUMBER_STAGGER_PER_HIT_SECONDS = 0.02F;
/// The narrowest that window is, in seconds.
inline constexpr f32 NUMBER_STAGGER_MIN_SECONDS = 0.06F;
/// The widest that window is, in seconds.
inline constexpr f32 NUMBER_STAGGER_MAX_SECONDS = 1.5F;
/// How many live numbers the pool holds room for before it grows (a debug burst is 4,000): it grows past it, never
/// dropping one.
inline constexpr std::size_t NUMBER_RESERVE = 8'192;

/// Where a number is in its motion: how far it has risen and how much of it shows.
struct NumberPose {
    /// How far above its anchor it has risen, in ems of its size (core::TextItem::lift).
    f32 lift = 0.0F;
    /// How much of it shows, from 0 (none) to 1 (all).
    f32 opacity = 1.0F;
};

/// The pose of a number moving by `motion`, `seconds` after its start (clamped to 0 to NUMBER_SECONDS). Rise: it
/// rises from 0 to RISE_EMS, fast at first and easing out. Bounce: it hops to BOUNCE_TOP_EMS by BOUNCE_TOP_SECONDS,
/// easing out, then settles to BOUNCE_REST_EMS by BOUNCE_REST_SECONDS, easing in and out, and stays there. Either way
/// it shows fully until NUMBER_FADE_START, then fades linearly to nothing at NUMBER_SECONDS. Pure: reduce motion
/// (chunk 11) adds a still motion here.
[[nodiscard]] NumberPose NumberMotionAt(core::NumberMotion motion, f32 seconds);

/// Where and when one hit's number starts, apart from the others on its target in its tick.
struct NumberScatter {
    /// How far its start is moved from its anchor, in ems of its size (core::TextItem::scatter): within
    /// ±NUMBER_SCATTER_X_EMS across and ±NUMBER_SCATTER_Y_EMS up and down, both × ScatterGrowth().
    core::Vec2 offset{};
    /// How long after its tick its motion starts, in seconds: within StaggerWindow().
    f32 delay = 0.0F;
};

/// How much the scatter's bounds widen for `hits` hits on one target in one tick: √(`hits` /
/// NUMBER_SCATTER_GROWTH_HITS), kept within 1 and NUMBER_SCATTER_GROWTH_MAX, so the numbers' density stays about even.
[[nodiscard]] f32 ScatterGrowth(u32 hits);

/// The window, in seconds after their tick, that the numbers of `hits` hits on one target in one tick start within:
/// `hits` × NUMBER_STAGGER_PER_HIT_SECONDS, kept within NUMBER_STAGGER_MIN_SECONDS and NUMBER_STAGGER_MAX_SECONDS.
[[nodiscard]] f32 StaggerWindow(u32 hits);

/// The scatter of the number of `hit`, the `ordinal`th (from 0) of the `hits` hits on its target in its tick: a hash
/// of its tick, target, use and `ordinal` alone (not its epoch, which a load or replay changes), so the same hit
/// scatters the same way on every run and replay.
[[nodiscard]] NumberScatter ScatterOf(const core::Event& hit, u32 ordinal, u32 hits);

/// The colour of each kind's numbers, in core::HitKind's order: premultiplied linear (the scene is drawn in linear
/// space), packed as core::TextItem::colour. Read over the renderer's dark outline and shadow, in sRGB: damage white
/// `#FFFFFF`, healing green `#7CDB6A`, poison violet `#B07CE8`, burn orange `#FF9A3C`. Healing also carries a "+", so
/// colour is never its only sign; chunk 11's palettes replace this table.
inline constexpr std::array<u32, core::HIT_KINDS> KIND_COLOURS{ {
    0xFFFF'FFFFU, // damage: linear (1, 1, 1)
    0xFF25'B533U, // healing: linear (0.2016, 0.7084, 0.1441)
    0xFFCE'336FU, // poison: linear (0.4342, 0.2016, 0.8070)
    0xFF0C'52FFU, // burn: linear (1, 0.3231, 0.0452)
} };

/// The colour of each kind's numbers with the outline and the shadow both off (core::Settings::textOutline and
/// textShadow), in core::HitKind's order and KIND_COLOURS' packing: without a dark edge, light numbers would read at
/// about 1.3:1 on the pale parchment, so each kind turns to a dark variant of its colour, each 4.6:1 or more against the
/// arena's bare ground (relative luminance 0.53 to 0.73) and still apart from the others. In sRGB: damage ink `#20242A`
/// (gfx::TEXT_INK, the labels' ink), healing green `#145A22`, poison violet `#5E2A9E`, burn orange `#823200`. Chunk 11's
/// palettes replace both colour tables, this one and KIND_COLOURS.
inline constexpr std::array<u32, core::HIT_KINDS> DARK_KIND_COLOURS{ {
    0xFF06'0404U, // damage: linear (0.0144, 0.0176, 0.0232)
    0xFF04'1A02U, // healing: linear (0.0070, 0.1022, 0.0160)
    0xFF57'061DU, // poison: linear (0.1119, 0.0232, 0.3419)
    0xFF00'0839U, // burn: linear (0.2232, 0.0319, 0)
} };

/// The main thread's live damage numbers: what the played events started, as texts at the render clock. Main thread
/// only.
class DamageNumbers {
public:
    /// Plays numbers as `settings` say: each hit apart or combined per use (combinedDamageNumbers), how they move
    /// (damageNumberMotion), and in KIND_COLOURS or, with textOutline and textShadow both off, DARK_KIND_COLOURS, as the
    /// renderer's text options are read from the same settings. The settings are read here, once.
    explicit DamageNumbers(const core::Settings& settings);

    /// Plays `event`, at its tick. A Hit starts a number of its value over its target at its position, in its kind's
    /// colour and signed "+" if it heals, moved and delayed by its scatter (ScatterOf()) among its
    /// target's hits in that tick; a tick's hits are played together, before the next Forget() or Texts(). With the combined
    /// setting it adds into the number of its use, target and kind instead, which waits at the start of its motion,
    /// unscattered, until the use ends: every use emits one UseEnded (core::EventKind::UseEnded), or its numbers wait until
    /// Clear(). A UseEnded starts the motion of its use's waiting numbers at its tick, at once: scattered among the
    /// combined numbers starting on their target in that tick as hits are among theirs, but never delayed.
    void Play(const core::Event& event);

    /// Forgets every number, waiting or not: a new timeline began.
    void Clear();

    /// Forgets the numbers NUMBER_SECONDS old or older at render tick `renderTick`, as Texts() does, drawing nothing:
    /// for frames not drawn.
    void Forget(f64 renderTick);

    /// Appends to `out` one Number text per live number that has started, at its pose at render tick `renderTick` (its
    /// age is the seconds since its tick, core::TICKS_PER_SECOND to a second, less its stagger delay; a waiting one's
    /// is 0), in the order played; forgets those NUMBER_SECONDS old or older (Forget()). One whose delay hasn't passed isn't
    /// drawn yet; its whole motion plays once it has.
    void Texts(f64 renderTick, std::vector<core::TextItem>& out);

    /// How many numbers are live: playing or waiting for their use to end.
    [[nodiscard]] std::size_t Live() const;

private:
    // A number: what it shows, where, and when its motion started.
    struct Number {
        u64 use = 0;
        core::StableId target{};
        core::HitKind kind = core::HitKind::Damage;
        core::Vec2 position{};
        i32 value = 0;
        u64 start = 0;         // the tick it was hit or, combined, its use ended
        u32 ordinal = 0;       // among its target's hits in its tick, or combined numbers starting then
        core::Vec2 scatter{};  // ScatterOf()'s, from Settle(); none while combined and waiting
        f32 delay = 0.0F;      // likewise: seconds after `start` its motion starts; none when combined
        bool combined = false; // added up from its use's hits, started when the use ended
    };
    // What a waiting number adds the hits of.
    struct Key {
        u64 use = 0;
        core::StableId target{};
        core::HitKind kind = core::HitKind::Damage;

        [[nodiscard]] friend bool operator==(const Key& lhs, const Key& rhs) = default;
    };
    struct KeyHash {
        [[nodiscard]] std::size_t operator()(const Key& key) const;
    };
    // A target's hits in one tick, which share a stagger window.
    struct Moment {
        core::StableId target{};
        u64 tick = 0;

        [[nodiscard]] friend bool operator==(const Moment& lhs, const Moment& rhs) = default;
    };
    struct MomentHash {
        [[nodiscard]] std::size_t operator()(const Moment& moment) const;
    };

    // Gives each number played since the last call its scatter and delay, now that every hit in its tick has played (a
    // tick's events are played together), from how many hits its target took in it (or combined numbers started on it).
    void Settle();

    // How old `number` is at render tick `renderTick`, in seconds from the start of its motion.
    [[nodiscard]] static f32 AgeOf(const Number& number, f64 renderTick);
    // `number` as a text at `pose`.
    [[nodiscard]] core::TextItem TextOf(const Number& number, NumberPose pose) const;

    bool m_combined = false;
    core::NumberMotion m_motion = core::NumberMotion::Bounce;
    bool m_dark = false;         // read KIND_COLOURS or, when true, DARK_KIND_COLOURS
    std::size_t m_unsettled = 0; // the first of m_playing that Settle() hasn't given its scatter
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<Number> m_playing{};                             // in the order they were played
    std::vector<Number> m_waiting{};                             // combined, until their use ends
    std::unordered_map<Key, std::size_t, KeyHash> m_waitingOf{}; // each waiting number's index in m_waiting
    std::unordered_map<Moment, u32, MomentHash> m_hitsIn{};      // the numbers started since Settle(), per target and tick
    // NOLINTEND(readability-redundant-member-init)
};

} // namespace app
