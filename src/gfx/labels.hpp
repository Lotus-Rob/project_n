#pragma once

/// @file
/// @ingroup gfx
/// The label pass (the phase 6 chunk 3 spec, "Labels: placement, stacking and priority"): each frame it places every
/// name label on the screen so they never crowd it. Overlapping labels of one name, chains of them included, stack
/// into one "Name (+N)" over the creature nearest the group's middle, with room kept for that wider text; a label
/// overlapping one of another name gives way to the higher priority (selected > hovered > player > named creatures >
/// the rest); labels too small to read, or whose glyphs aren't all rasterised, fade out, still placed. Every change fades
/// over LABEL_FADE_SECONDS, never pops: a label coming or going fades in or out, a stack's count cross-fades and its place
/// glides; and a label on the edge of overlapping keeps its state (LABEL_HYSTERESIS_PIXELS), so it doesn't flicker. Pure,
/// tested without a device.

#include "core/frame.hpp"
#include "core/stable_id.hpp"
#include "core/strings.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gfx {

/// A label as the pass sees it: what it names, how it ranks and where its box falls on the screen.
struct LabelInput {
    /// The entity it names; labels are told apart frame to frame by it, so each label names a different one.
    core::StableId entity{};
    /// Labels of one key stack where they overlap (core::TextItem::nameKey).
    u32 nameKey = 0;
    /// Which wins where labels of different keys overlap, already raised for the hovered and selected entities.
    core::LabelPriority priority = core::LabelPriority::Rest;
    /// The middle of its box's bottom edge, in screen pixels from the top left (labels hang above their anchor).
    core::Vec2 anchor{};
    /// Its box's width, in screen pixels.
    f32 width = 0.0F;
    /// Its box's height, in screen pixels.
    f32 height = 0.0F;
    /// The width of its name as a stack ("Name (+N)", StackText()) with as many digits as the frame's count can have,
    /// in screen pixels: the room a stack keeps over each of its members.
    f32 stackWidth = 0.0F;
    /// The pixel size it is drawn at (TextPixels()): below LABEL_MIN_PIXELS × the text scale it fades out.
    f32 pixels = 0.0F;
    /// The named entity's height on the screen, in pixels (core::TextItem::height × PixelsPerUnit()). The placement
    /// doesn't read it; it travels with the label for what is decided by the creature's size on the screen.
    f32 entityPixels = 0.0F;
    /// Whether every glyph of its text and its stack text is rasterised (Layout::complete): until they are, it is placed
    /// but fades out, as one too small to read does, so it never draws with gaps; then it fades in.
    bool complete = true;
    /// The text it draws. The pass doesn't read it: it keeps it with the label, so a label whose item has gone draws as
    /// it last did while it fades out.
    core::TextItem item{};
};

/// PlacedLabel::input of a label whose item has gone, fading out.
inline constexpr std::size_t LABEL_GONE = static_cast<std::size_t>(-1);

/// A label the pass draws this frame: `count`'s text at `opacity` × `change` and, while a count change cross-fades,
/// `previous`'s text at `opacity` × (1 − `change`), both at `anchor`.
struct PlacedLabel {
    /// Its index in the labels given to LabelPass::Place(), or LABEL_GONE for a label whose item has gone, fading out.
    std::size_t input = 0;
    /// The label it draws, as the pass keeps it: the one given at `input`, or the last given of a label whose item has
    /// gone. It holds until the next Place().
    const LabelInput* label = nullptr;
    /// Where it is drawn, the middle of its box's bottom edge: over its stack's member nearest the stack's middle, or
    /// its own anchor, and gliding between them over LABEL_FADE_SECONDS when that changes.
    core::Vec2 anchor{};
    /// How many other labels stacked into it: above zero, it reads StackText()'s "Name (+count)", else its name.
    u32 count = 0;
    /// The count it showed before, cross-fading out; `count` when no change is fading.
    u32 previous = 0;
    /// How far the cross-fade from `previous`'s text to `count`'s has come, from 0 to 1 (done).
    f32 change = 1.0F;
    /// How much of it shows, from 0 (none) to 1 (all): it multiplies the label's premultiplied colour.
    f32 opacity = 0.0F;
};

/// How long a label takes to fade fully in or out, a stack's count to cross-fade and its place to glide, in seconds of
/// the frame clock (View::frameClock).
inline constexpr f32 LABEL_FADE_SECONDS = 0.15F;
/// How far a label's box shrinks on every side while it was placed last frame (no more than a quarter of its smaller
/// side), and grows while it wasn't, in screen pixels: a label on the edge of overlapping keeps the state it had.
/// Likewise a stack stays over the creature it was over until another is this much nearer the stack's middle.
inline constexpr f32 LABEL_HYSTERESIS_PIXELS = 4.0F;
/// The side of the screen-space grid's cells the pass finds overlaps through, in screen pixels.
inline constexpr f32 LABEL_GRID_PIXELS = 64.0F;

/// Places the frame's labels and keeps each one's opacity, count and place from frame to frame. Main thread only.
class LabelPass {
public:
    /// Places `labels` on a screen `width` by `height` pixels, at frame-clock time `clock` in seconds, with text
    /// scaled by `scale` (the text size setting, which scales the readable size too).
    ///
    /// In priority order, highest first, then by entity, each label is placed, joins a stack or is hidden. A stack
    /// covers its members' boxes and, once it has two, a box `stackWidth` wide over each member, where its text may be
    /// drawn. A label overlapping nothing is placed, heading a stack of its own. One overlapping only stacks of its
    /// name key, where neither it nor their heads are selected or hovered, joins the highest-ranked of them, merging
    /// any others it bridges into it, unless the room the stack would need over it (or over a lone head it joins)
    /// overlaps anything else. Any other is hidden. A label wholly off the screen is left out of all this.
    ///
    /// A stack is drawn over its member nearest the mean of its members' anchors, or over the one it was drawn over
    /// last frame while that is within LABEL_HYSTERESIS_PIXELS as near. A placed label large enough to read whose text is
    /// complete shows; every other fades out. Each label's opacity moves toward its target by the clock's step over
    /// LABEL_FADE_SECONDS, its count cross-fades and its place glides over the same time, and one not drawn moves at
    /// once. A label new to the pass fades in from nothing, except on the pass's first Place(), which has no frame
    /// before it to fade from (a capture): its labels start at their targets. A label whose item has gone fades out
    /// where it was last drawn, showing what it showed, and is forgotten once faded out; back before that, it fades in
    /// from where it got to.
    ///
    /// Gives every placed label on the screen, faded out or not, and every other still fading out, in drawing order:
    /// those whose items have gone first, then the lowest priority, so the highest draws on top. The span holds until the
    /// next Place().
    std::span<const PlacedLabel> Place(std::span<const LabelInput> labels, u32 width, u32 height, f64 clock, f32 scale);

    /// Where `entity`'s label was put by the last Place(), as the middle of its box's bottom edge: where it or the
    /// stack it joined is drawn, even when faded out, or its own anchor when it was hidden or off the screen; none when
    /// it wasn't given, fading out or not.
    [[nodiscard]] std::optional<core::Vec2> Anchor(core::StableId entity) const;

private:
    // What the pass remembers of an entity's label between frames.
    struct Seen {
        u64 frame = 0;          // the last Place() that was given it
        std::size_t index = 0;  // its index in that Place()'s labels
        bool fresh = true;      // not given the Place() before
        bool drawnOver = false; // a stack was drawn over it
        core::StableId stack{}; // the head of the stack it was in (itself, if it headed one); none if hidden
        f32 opacity = 0.0F;
        core::StableId over{};   // whose anchor its label is drawn at
        core::Vec2 overAnchor{}; // that anchor
        core::Vec2 offset{};     // from that anchor to where it is drawn, while it glides
        f32 glide = 0.0F;        // how much of the glide is left, from 1 to 0
        u32 count = 0;
        u32 previous = 0;
        f32 change = 1.0F;
        std::optional<core::Vec2> anchor = std::nullopt; // Anchor()
        core::Vec2 drawn{};                              // where it was drawn last, where it fades out once its item has gone
        LabelInput last{};                               // as last given
    };
    // A box on the screen, in pixels.
    struct Box {
        f32 left = 0.0F;
        f32 top = 0.0F;
        f32 right = 0.0F;
        f32 bottom = 0.0F;
    };
    // A stack this frame: its head (the label placed first, which it is drawn as) and its members, the head included.
    struct Stack {
        std::size_t head = 0; // the head's index in the labels
        std::size_t into = 0; // the stack it merged into; itself while it hasn't
        u32 members = 0;
        core::Vec2 sum{};        // of the members' anchors
        std::size_t over = 0;    // the member it is drawn over
        f32 nearest = 0.0F;      // the squared distance from the members' mean of the member nearest it
        std::size_t held = 0;    // the nearest member a stack was drawn over last frame; NONE if none is in it
        f32 heldDistance = 0.0F; // that member's squared distance from the mean
        core::Vec2 drawn{};      // where it is drawn
    };
    // A box a stack covers.
    struct Footprint {
        Box box{};
        std::size_t stack = 0;
        std::size_t member = 0; // the label whose box or room it is
    };
    static constexpr std::size_t NONE = static_cast<std::size_t>(-1);
    static constexpr std::size_t OFF_SCREEN = NONE - 1; // a label's role when it is wholly off the screen

    // A box `width` by `height` hanging above `anchor`, centred on it, grown by `grow` on every side.
    [[nodiscard]] static Box BoxAt(core::Vec2 anchor, f32 width, f32 height, f32 grow);
    // How much a box `width` by `height` of the label whose state is `seen` grows (or, negative, shrinks) when tested
    // against labels it may not stack with: it shrinks if it was shown or counted last frame and grows if it was hidden,
    // so a label on the edge of overlapping keeps the state it had.
    [[nodiscard]] static f32 Grow(const Seen& seen, f32 width, f32 height);
    // How much a box `width` by `height` of the label whose state is `seen` grows (or, negative, shrinks) when tested
    // against a box of a stack of its name, the box of the label whose state is `other`: it grows if the two were in one
    // stack last frame or if it was hidden, and shrinks if not, so labels on the edge of stacking keep the state they
    // had; a label new this frame takes neither side, nor does a shown or counted one against a label new this frame.
    [[nodiscard]] static f32 Band(const Seen& seen, const Seen& other, f32 width, f32 height);
    // How much a box `width` by `height` of the label whose state is `seen` grows against `other`: Band() if it may join
    // `other`'s stack (`joins`), else Grow().
    [[nodiscard]] f32 Against(const Seen& seen, const Footprint& other, bool joins, f32 width, f32 height) const;
    // The stack `stack` has merged into, by now.
    [[nodiscard]] std::size_t Root(std::size_t stack);
    // The range of grid cells `box` touches, clamped to the grid: a box past its edge is in the edge cells.
    void Cells(const Box& box, std::size_t& left, std::size_t& top, std::size_t& right, std::size_t& bottom) const;
    // Whether a stack headed by `head` may take `label` in: one name key, neither selected nor hovered.
    [[nodiscard]] static bool Joins(const LabelInput& head, const LabelInput& label);
    // Whether `first` and `second` overlap; boxes that only touch don't.
    [[nodiscard]] static bool Overlap(const Box& first, const Box& second);
    // The stacks the label at `index` may join that its box overlaps, in m_joined; whether its box overlaps any other.
    [[nodiscard]] bool Collect(std::size_t index, std::span<const LabelInput> labels);
    // Adds the stacks the label at `index` may join that the room over `member` overlaps to m_joined, and the heads of
    // those alone to m_rooms; whether it overlaps any other.
    [[nodiscard]] bool Reaches(std::size_t member, std::size_t index, std::span<const LabelInput> labels);
    // Covers `box`, the box or room of the label at `member`, with `stack`.
    void Cover(const Box& box, std::size_t stack, std::size_t member);
    // Gives each label its state's entry (m_seenOf) and the grid its size for a screen `width` by `height`.
    void Begin(std::span<const LabelInput> labels, u32 width, u32 height);
    // Steps 1 to 3: m_order, then each label's stack in m_role, merged where a label bridges two.
    void Resolve(std::span<const LabelInput> labels, u32 width, u32 height);
    // Joins the label at `index` to the stacks in m_joined, and any of its name their text's room reaches, if that room
    // reaches no other; else it is hidden.
    void Join(std::size_t index, std::span<const LabelInput> labels);
    // Step 4: what each stack is drawn over, at its member nearest the members' mean, unless the nearest member it was
    // drawn over last frame is within LABEL_HYSTERESIS_PIXELS as near.
    void Centre(std::span<const LabelInput> labels);
    // Step 5: each label's opacity, count and place, `step` of the way toward their targets, and m_placed; on the pass's
    // `first` frame, labels new to it start at their targets.
    void Fade(std::span<const LabelInput> labels, f32 step, f32 scale, bool first);
    // Fades out, by `step`, the labels whose items have gone, into m_placed.
    void FadeGone(f32 step);
    // Moves the cross-fade of the label whose state is `seen` toward showing `count` by `step`; `drawn` says whether it
    // was drawn last frame (if not, it shows `count` at once).
    static void Count(Seen& seen, u32 count, f32 step, bool drawn);
    // Where the label whose state is `seen` is drawn when over `over`'s label (at `overAnchor`), its glide moved on by
    // `step`; `drawn` says whether it was drawn last frame (if not, it moves at once).
    [[nodiscard]] core::Vec2 Glide(Seen& seen, std::span<const LabelInput> labels, std::size_t over, f32 step, bool drawn);

    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::unordered_map<core::StableId, Seen> m_seen{};
    std::vector<Seen*> m_seenOf{};                   // per label: its entry in m_seen (whose elements stay put while it grows)
    std::vector<std::vector<std::size_t>> m_cells{}; // per grid cell, row by row: the footprints touching it
    std::vector<std::size_t> m_order{};
    std::vector<std::size_t> m_role{}; // per label: the stack it is in, NONE when hidden, or OFF_SCREEN
    std::vector<Stack> m_stacks{};
    std::vector<Footprint> m_footprints{};
    std::vector<std::size_t> m_joined{};
    std::vector<std::size_t> m_rooms{}; // the members whose room a joining label still has to check
    std::vector<PlacedLabel> m_placed{};
    std::vector<Seen*> m_gone{}; // the labels whose items have gone, still fading out
    // NOLINTEND(readability-redundant-member-init)
    std::size_t m_columns = 0;
    std::size_t m_rows = 0;
    std::optional<f64> m_clock = std::nullopt;
    u64 m_frame = 0;
};

/// `stack` (core::StringId::LabelStack's text) with its first `{name}` replaced by `name` and its first `{count}` by
/// `count` in decimal digits, written into `into` (cleared first, its capacity kept); a placeholder it lacks is left
/// out, so a bad strings table can't throw.
void StackText(std::string& into, std::string_view stack, std::string_view name, u32 count);

/// StackText() into a new string.
[[nodiscard]] std::string StackText(std::string_view stack, std::string_view name, u32 count);

/// Holds each caption back while its glyphs aren't all rasterised, then fades it in over LABEL_FADE_SECONDS, so a caption
/// never draws with gaps. Captions are told apart by what they say. Main thread only.
class CaptionFades {
public:
    /// Starts a frame at frame-clock time `clock` in seconds (View::frameClock).
    void Begin(f64 clock);

    /// How much of a caption saying `string` shows this frame, from 0 to 1: none while its layout is incomplete
    /// (`complete` false, Layout::complete), then rising to all over LABEL_FADE_SECONDS; all at once for one whose
    /// glyphs were there from the first.
    [[nodiscard]] f32 Opacity(core::StringId string, bool complete);

private:
    struct Fade {
        f32 opacity = 1.0F;
        u64 frame = 0; // the last Begin() it was stepped in
    };
    std::array<Fade, core::STRINGS> m_fades{};
    std::optional<f64> m_clock = std::nullopt;
    f32 m_step = 0.0F;
    u64 m_frame = 0;
};

/// How much of a caption shows when drawn at `pixels` (TextPixels()) with text scaled by `scale`: all of it from
/// LABEL_MIN_PIXELS × scale up, none below three quarters of that, and a smooth ramp between, so a caption too small to
/// read fades out with the zoom rather than blurring into a block. No time state: the zoom itself changes smoothly.
[[nodiscard]] f32 CaptionOpacity(f32 pixels, f32 scale);

} // namespace gfx
