#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/stable_id.hpp"
#include "gfx/labels.hpp"
#include "gfx/text_place.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 SCREEN_WIDTH = 1280;
constexpr u32 SCREEN_HEIGHT = 720;
constexpr f32 BOX_WIDTH = 40.0F;
constexpr f32 BOX_HEIGHT = 20.0F;
constexpr f32 STACK_WIDTH = 60.0F; // "Fox (+N)" is wider than "Fox"
constexpr f32 READABLE = 13.0F;    // pixels, over LABEL_MIN_PIXELS
constexpr f32 TOO_SMALL = 5.0F;
constexpr f32 SCALE = 1.0F;
constexpr f32 ROW = 300.0F; // every box's anchor y, unless a test says otherwise
constexpr u32 FOX_KEY = 7;
constexpr u32 PLAYER_KEY = 3;
constexpr f64 FRAME_SECONDS = 1.0 / 60.0;
constexpr f64 HALF_FADE = 0.075;
constexpr f64 WHOLE_FADE = 0.15;
constexpr f32 HALF = 0.5F;
constexpr f64 QUARTER_FADE = 0.0375;
constexpr f64 THREE_QUARTER_FADE = 0.1125;
constexpr f32 THREE_QUARTERS = 0.75F;
constexpr f32 FRAME_STEP = 1.0F / 9.0F; // a 60th of a second over the fade's 0.15

// Places along the row. A box is 40 px wide: anchors less than 40 apart overlap.
constexpr f32 AT_100 = 100.0F;
constexpr f32 AT_110 = 110.0F;
constexpr f32 AT_120 = 120.0F;
constexpr f32 AT_130 = 130.0F;
constexpr f32 AT_135 = 135.0F;
constexpr f32 AT_160 = 160.0F;
constexpr f32 AT_165 = 165.0F;
constexpr f32 AT_170 = 170.0F;
constexpr f32 AT_200 = 200.0F;
constexpr f32 ELSEWHERE = 600.0F; // apart from every other box

constexpr gfx::LabelInput Label(u64 entity, u32 key, core::LabelPriority priority, f32 across) {
    return {
        .entity = core::StableId{ entity },
        .nameKey = key,
        .priority = priority,
        .anchor = { .x = across, .y = ROW },
        .width = BOX_WIDTH,
        .height = BOX_HEIGHT,
        .stackWidth = STACK_WIDTH,
        .pixels = READABLE,
    };
}

constexpr gfx::LabelInput Fox(u64 entity, f32 across) {
    return Label(entity, FOX_KEY, core::LabelPriority::Rest, across);
}

constexpr gfx::LabelInput Player(u64 entity, f32 across) {
    return Label(entity, PLAYER_KEY, core::LabelPriority::Player, across);
}

std::span<const gfx::PlacedLabel> Place(gfx::LabelPass& pass, std::span<const gfx::LabelInput> labels, f64 clock) {
    return pass.Place(labels, SCREEN_WIDTH, SCREEN_HEIGHT, clock, SCALE);
}

// The placed label of the label at `input`, which must be drawn.
gfx::PlacedLabel Of(std::span<const gfx::PlacedLabel> placed, std::size_t input) {
    const auto found = std::ranges::find(placed, input, &gfx::PlacedLabel::input);
    REQUIRE(found != placed.end());
    return *found;
}

TEST_CASE("labels apart are each shown") {
    const std::array<gfx::LabelInput, 2> labels{ { Fox(1, AT_100), Fox(2, AT_200) } };
    gfx::LabelPass pass;
    const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, 0.0);
    REQUIRE(placed.size() == 2);
    CHECK(placed[0].count == 0);
    CHECK(placed[1].count == 0);
    CHECK(placed[0].opacity == 1.0F);
    CHECK(placed[1].opacity == 1.0F);
}

TEST_CASE("overlapping labels of one name stack over the creature nearest the middle") {
    const std::array<gfx::LabelInput, 3> labels{ { Fox(1, AT_100), Fox(2, AT_110), Fox(3, AT_120) } };
    gfx::LabelPass pass;
    const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, 0.0);
    REQUIRE(placed.size() == 1);
    CHECK(placed[0].input == 0);
    CHECK(placed[0].count == 2);
    CHECK(placed[0].anchor.x == AT_110);
    CHECK(pass.Anchor(core::StableId{ 3 }).value_or(core::Vec2{}).x == AT_110);
}

TEST_CASE("labels of one name overlapping in a chain stack into one") {
    // 100 overlaps 130 and 130 overlaps 160, but 100 and 160 are apart: one stack, over the middle one.
    const std::array<gfx::LabelInput, 3> labels{ { Fox(1, AT_100), Fox(2, AT_130), Fox(3, AT_160) } };
    gfx::LabelPass pass;
    const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, 0.0);
    REQUIRE(placed.size() == 1);
    CHECK(placed[0].count == 2);
    CHECK(placed[0].anchor.x == AT_130);
}

TEST_CASE("a label bridging two stacks of its name merges them") {
    // 100 and 170 are apart, each a stack; 135, ranked last, overlaps both.
    const std::array<gfx::LabelInput, 3> labels{ { Fox(1, AT_100), Fox(2, AT_170), Fox(3, AT_135) } };
    gfx::LabelPass pass;
    const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, 0.0);
    REQUIRE(placed.size() == 1);
    CHECK(placed[0].input == 0);
    CHECK(placed[0].count == 2);
    CHECK(placed[0].anchor.x == AT_135);
}

TEST_CASE("a stack keeps room for its wider text, so it never draws over another name") {
    // The player's box spans 80 to 120; the foxes' at 145 and 160 clear it, but "Fox (+1)", 60 px wide, over the one
    // at 145 would reach 115. So the second fox doesn't stack, whichever of the two heads the stack: it is hidden, and
    // nothing drawn overlaps.
    constexpr f32 CLEAR = 145.0F;
    const std::array<std::array<gfx::LabelInput, 3>, 2> arrangements{ {
        { { Player(1, AT_100), Fox(2, CLEAR), Fox(3, AT_160) } }, // the nearer fox heads the stack
        { { Player(1, AT_100), Fox(2, AT_160), Fox(3, CLEAR) } }, // the nearer fox joins it
    } };
    for (const std::array<gfx::LabelInput, 3>& labels : arrangements) {
        gfx::LabelPass pass;
        const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, 0.0);
        REQUIRE(placed.size() == 2);
        const auto half = [&placed](std::size_t index) { return (placed[index].count > 0 ? STACK_WIDTH : BOX_WIDTH) * HALF; };
        CHECK(placed[0].count == 0);
        CHECK(placed[1].count == 0);
        CHECK(std::abs(placed[0].anchor.x - placed[1].anchor.x) >= half(0) + half(1));
    }
}

TEST_CASE("a label of another name overlapping any of a stack's labels gives way") {
    // The fox at 100 heads a stack the fox at 130 joins; the other name at 165 overlaps only the one at 130.
    const std::array<gfx::LabelInput, 3> labels{
        { Fox(1, AT_100), Fox(2, AT_130), Label(3, PLAYER_KEY, core::LabelPriority::Rest, AT_165) }
    };
    gfx::LabelPass pass;
    const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, 0.0);
    REQUIRE(placed.size() == 1);
    CHECK(placed[0].input == 0);
    CHECK(placed[0].count == 1);
}

TEST_CASE("a label under another name's is hidden, not counted into its own name's stack beside it") {
    // The player's label at 100 and a fox's at 170 are apart; the fox at 135 overlaps both.
    const std::array<gfx::LabelInput, 3> labels{ { Player(1, AT_100), Fox(2, AT_170), Fox(3, AT_135) } };
    gfx::LabelPass pass;
    const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, 0.0);
    REQUIRE(placed.size() == 2);
    CHECK(placed[0].count == 0);
    CHECK(placed[1].count == 0);
}

TEST_CASE("overlapping labels of different names give way by priority") {
    constexpr std::size_t PLAYER = 1;
    const std::array<gfx::LabelInput, 2> labels{ { Fox(1, AT_100), Player(2, AT_110) } };
    gfx::LabelPass pass;
    const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, 0.0);
    REQUIRE(placed.size() == 1);
    CHECK(placed[0].input == PLAYER);
    CHECK(placed[0].count == 0);
}

TEST_CASE("a hidden label's anchor is its own") {
    const std::array<gfx::LabelInput, 2> labels{ { Fox(1, AT_100), Player(2, AT_110) } };
    gfx::LabelPass pass;
    static_cast<void>(Place(pass, labels, 0.0));
    const std::optional<core::Vec2> anchor = pass.Anchor(core::StableId{ 1 });
    REQUIRE(anchor.has_value());
    const core::Vec2 where = anchor.value_or(core::Vec2{});
    CHECK(where.x == AT_100);
    CHECK(where.y == ROW);
}

TEST_CASE("a selected label never joins a stack") {
    constexpr std::size_t SELECTED = 1;
    const std::array<gfx::LabelInput, 3> labels{
        { Fox(1, AT_100), Label(2, FOX_KEY, core::LabelPriority::Selected, AT_110), Fox(3, AT_120) }
    };
    gfx::LabelPass pass;
    const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, 0.0);
    REQUIRE(placed.size() == 1);
    CHECK(placed[0].input == SELECTED);
    CHECK(placed[0].count == 0);
    CHECK(placed[0].anchor.x == AT_110);
}

TEST_CASE("a hovered label never joins a stack") {
    constexpr std::size_t HOVERED = 1;
    const std::array<gfx::LabelInput, 3> labels{
        { Fox(1, AT_100), Label(2, FOX_KEY, core::LabelPriority::Hovered, AT_110), Fox(3, AT_120) }
    };
    gfx::LabelPass pass;
    const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, 0.0);
    REQUIRE(placed.size() == 1);
    CHECK(placed[0].input == HOVERED);
    CHECK(placed[0].count == 0);
}

TEST_CASE("a hovered label beats the player's") {
    constexpr std::size_t HOVERED = 1;
    const std::array<gfx::LabelInput, 2> labels{ { Player(1, AT_100),
                                                   Label(2, FOX_KEY, core::LabelPriority::Hovered, AT_110) } };
    gfx::LabelPass pass;
    const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, 0.0);
    REQUIRE(placed.size() == 1);
    CHECK(placed[0].input == HOVERED);
}

TEST_CASE("labels come in drawing order, the highest priority last") {
    constexpr std::size_t PLAYER = 0;
    constexpr std::size_t FOX = 1;
    const std::array<gfx::LabelInput, 2> labels{ { Player(1, AT_100), Fox(2, ELSEWHERE) } };
    gfx::LabelPass pass;
    const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, 0.0);
    REQUIRE(placed.size() == 2);
    CHECK(placed[0].input == FOX);
    CHECK(placed[1].input == PLAYER);
}

TEST_CASE("a label below the readable size fades out but stays placed") {
    std::array<gfx::LabelInput, 1> labels{ { Fox(1, AT_100) } };
    gfx::LabelPass pass;
    static_cast<void>(Place(pass, labels, 0.0));
    labels[0].pixels = TOO_SMALL;
    const std::span<const gfx::PlacedLabel> halfway = Place(pass, labels, HALF_FADE);
    REQUIRE(halfway.size() == 1);
    CHECK(halfway[0].opacity == doctest::Approx(HALF));
    const std::span<const gfx::PlacedLabel> gone = Place(pass, labels, WHOLE_FADE);
    REQUIRE(gone.size() == 1);
    CHECK(gone[0].opacity == 0.0F);
    CHECK(pass.Anchor(core::StableId{ 1 }).value_or(core::Vec2{}).x == AT_100);
}

TEST_CASE("a larger text scale needs larger labels to read") {
    constexpr f32 DOUBLE = 2.0F;
    const std::array<gfx::LabelInput, 1> labels{ { Fox(1, AT_100) } }; // 13 pixels: under 9 × 2
    gfx::LabelPass pass;
    const std::span<const gfx::PlacedLabel> placed = pass.Place(labels, SCREEN_WIDTH, SCREEN_HEIGHT, 0.0, DOUBLE);
    REQUIRE(placed.size() == 1);
    CHECK(placed[0].opacity == 0.0F);
}

TEST_CASE("a label that gives way fades out rather than vanishing") {
    constexpr std::size_t FOX = 1;
    std::array<gfx::LabelInput, 2> labels{ { Player(1, AT_100), Fox(2, ELSEWHERE) } };
    gfx::LabelPass pass;
    static_cast<void>(Place(pass, labels, 0.0));
    labels[FOX].anchor.x = AT_110;
    const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, HALF_FADE);
    REQUIRE(placed.size() == 2);
    CHECK(placed[0].input == FOX);
    CHECK(placed[0].opacity == doctest::Approx(HALF));
    CHECK(placed[0].count == 0);
}

TEST_CASE("a label on the edge of overlapping doesn't flicker") {
    // The player's box spans x 80 to 120: the fox's overlaps it by 2 px at 138, is 2 px clear at 142, 10 px at 150.
    constexpr f32 OVERLAPPING = 138.0F;
    constexpr f32 JUST_APART = 142.0F;
    constexpr f32 WELL_APART = 150.0F;
    constexpr u32 FRAMES = 10;
    constexpr std::size_t FOX = 1;
    std::array<gfx::LabelInput, 2> labels{ { Player(1, AT_100), Fox(2, OVERLAPPING) } };
    gfx::LabelPass pass;
    f64 clock = 0.0;
    for (u32 frame = 0; frame < FRAMES; ++frame) {
        labels[FOX].anchor.x = frame % 2 == 0 ? OVERLAPPING : JUST_APART;
        const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, clock);
        INFO("frame ", frame);
        CHECK(placed.size() == 1);
        clock += FRAME_SECONDS;
    }
    labels[FOX].anchor.x = WELL_APART;
    const std::span<const gfx::PlacedLabel> shown = Place(pass, labels, clock);
    REQUIRE(shown.size() == 2);
    // Shown now, it stays shown on the edge: it keeps fading in, never out.
    f32 opacity = shown[0].opacity;
    for (u32 frame = 0; frame < FRAMES; ++frame) {
        clock += FRAME_SECONDS;
        labels[FOX].anchor.x = frame % 2 == 0 ? OVERLAPPING : JUST_APART;
        INFO("frame ", frame);
        const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, clock);
        REQUIRE(placed.size() == 2);
        CHECK(placed[0].opacity >= opacity);
        opacity = placed[0].opacity;
    }
    CHECK(opacity == 1.0F);
}

TEST_CASE("a small placed label still stacks when another covers it") {
    // A 6 px box shrunk by the full 4 px would turn inside out and, across a grid line (y 128), find nothing.
    constexpr f32 SMALL = 6.0F;
    constexpr f32 ACROSS_A_CELL = 130.0F;
    gfx::LabelInput small = Fox(2, AT_100);
    small.width = SMALL;
    small.height = SMALL;
    small.anchor.y = ACROSS_A_CELL;
    gfx::LabelInput large = Fox(1, AT_100);
    large.anchor.y = ACROSS_A_CELL;
    gfx::LabelPass pass;
    const std::array<gfx::LabelInput, 1> alone{ { small } };
    static_cast<void>(Place(pass, alone, 0.0));
    const std::array<gfx::LabelInput, 2> covered{ { small, large } };
    const std::span<const gfx::PlacedLabel> placed = Place(pass, covered, FRAME_SECONDS);
    const gfx::PlacedLabel stack = Of(placed, 1);
    CHECK(stack.count == 1);
}

TEST_CASE("a stack holds its creature until another is clearly nearer its middle, then glides to it") {
    // At 100, 110, 114 the middle is 108: over 110. At 100, 112, 109 it is 107: 109 is 3 px nearer than 112, within the
    // hysteresis, so the stack stays over the same creature, now at 112. At 100, 118, 109 the middle is 109: it moves
    // to 109, gliding from 118 over 0.15 s, a ninth of the way on the frame it starts.
    constexpr f32 HELD_AT = 112.0F;
    constexpr f32 NEARER = 109.0F;
    constexpr f32 FAR_OFF = 118.0F;
    constexpr f32 GLIDING = 117.0F;
    constexpr f32 THIRD = 114.0F;
    constexpr std::size_t HELD = 1;
    constexpr std::size_t OTHER = 2;
    std::array<gfx::LabelInput, 3> labels{ { Fox(1, AT_100), Fox(2, AT_110), Fox(3, THIRD) } };
    gfx::LabelPass pass;
    const std::span<const gfx::PlacedLabel> first = Place(pass, labels, 0.0);
    REQUIRE(first.size() == 1);
    CHECK(first[0].anchor.x == AT_110);
    labels[HELD].anchor.x = HELD_AT;
    labels[OTHER].anchor.x = NEARER;
    const std::span<const gfx::PlacedLabel> held = Place(pass, labels, FRAME_SECONDS);
    REQUIRE(held.size() == 1);
    CHECK(held[0].anchor.x == HELD_AT);
    labels[HELD].anchor.x = FAR_OFF;
    const std::span<const gfx::PlacedLabel> gliding = Place(pass, labels, 2 * FRAME_SECONDS);
    REQUIRE(gliding.size() == 1);
    CHECK(gliding[0].anchor.x == doctest::Approx(GLIDING));
    const std::span<const gfx::PlacedLabel> arrived = Place(pass, labels, (2 * FRAME_SECONDS) + WHOLE_FADE);
    REQUIRE(arrived.size() == 1);
    CHECK(arrived[0].anchor.x == NEARER);
}

TEST_CASE("a stack holds its creature when its head changes") {
    // 100, 110, 114: the middle is 108, over 110. A higher-ranked fox at 106 then heads the stack and moves the middle to
    // 107.5, 1.5 px from it and 2.5 px from 110: within the hysteresis, the stack stays over 110.
    constexpr f32 NEW_HEAD = 106.0F;
    constexpr f32 THIRD = 114.0F;
    constexpr std::size_t HEAD = 3;
    constexpr u32 OTHERS = 3;
    gfx::LabelPass pass;
    const std::array<gfx::LabelInput, 3> before{ { Fox(2, AT_100), Fox(3, AT_110), Fox(4, THIRD) } };
    static_cast<void>(Place(pass, before, 0.0));
    const std::array<gfx::LabelInput, 4> after{ { Fox(2, AT_100), Fox(3, AT_110), Fox(4, THIRD), Fox(1, NEW_HEAD) } };
    const gfx::PlacedLabel stack = Of(Place(pass, after, FRAME_SECONDS), HEAD);
    CHECK(stack.count == OTHERS);
    CHECK(stack.anchor.x == AT_110);
}

TEST_CASE("a stack's count cross-fades when it changes") {
    constexpr u32 ONE = 1;
    constexpr u32 TWO = 2;
    gfx::LabelPass pass;
    const std::array<gfx::LabelInput, 2> two{ { Fox(1, AT_100), Fox(2, AT_110) } };
    static_cast<void>(Place(pass, two, 0.0));
    const std::array<gfx::LabelInput, 3> three{ { Fox(1, AT_100), Fox(2, AT_110), Fox(3, AT_120) } };
    const gfx::PlacedLabel changing = Of(Place(pass, three, HALF_FADE), 0);
    CHECK(changing.count == TWO);
    CHECK(changing.previous == ONE);
    CHECK(changing.change == doctest::Approx(HALF));
    const gfx::PlacedLabel settled = Of(Place(pass, three, HALF_FADE + WHOLE_FADE), 0);
    CHECK(settled.change == 1.0F);
    CHECK(settled.previous == TWO);
}

TEST_CASE("a stack's count changing back mid-fade turns the cross-fade round") {
    // 1 → 2, a quarter faded at 0.0375 s; back to 1 an eighth later: 1 is the count again and 2 the previous, fading
    // from where 2 had got to (three quarters of 1 still showing) and an eighth on.
    constexpr u32 ONE = 1;
    constexpr u32 TWO = 2;
    constexpr f64 EIGHTH_FADE = 0.01875;
    constexpr f32 SEVEN_EIGHTHS = 0.875F;
    gfx::LabelPass pass;
    const std::array<gfx::LabelInput, 2> two{ { Fox(1, AT_100), Fox(2, AT_110) } };
    const std::array<gfx::LabelInput, 3> three{ { Fox(1, AT_100), Fox(2, AT_110), Fox(3, AT_120) } };
    static_cast<void>(Place(pass, two, 0.0));
    static_cast<void>(Place(pass, three, QUARTER_FADE));
    const gfx::PlacedLabel back = Of(Place(pass, two, QUARTER_FADE + EIGHTH_FADE), 0);
    CHECK(back.count == ONE);
    CHECK(back.previous == TWO);
    CHECK(back.change == doctest::Approx(SEVEN_EIGHTHS));
}

TEST_CASE("a third count mid-fade replaces the fainter text") {
    constexpr u32 ONE = 1;
    constexpr u32 TWO = 2;
    constexpr u32 THREE = 3;
    const std::array<gfx::LabelInput, 2> two{ { Fox(1, AT_100), Fox(2, AT_110) } };
    const std::array<gfx::LabelInput, 3> three{ { Fox(1, AT_100), Fox(2, AT_110), Fox(3, AT_120) } };
    const std::array<gfx::LabelInput, 4> four{ { Fox(1, AT_100), Fox(2, AT_110), Fox(3, AT_120), Fox(4, AT_130) } };
    SUBCASE("a quarter in, the old count still shows most: it stays, the new count takes the quarter") {
        gfx::LabelPass pass;
        static_cast<void>(Place(pass, two, 0.0));
        static_cast<void>(Place(pass, three, QUARTER_FADE));
        const gfx::PlacedLabel third = Of(Place(pass, four, 2 * QUARTER_FADE), 0);
        CHECK(third.count == THREE);
        CHECK(third.previous == ONE);
        CHECK(third.change == doctest::Approx(HALF));
    }
    SUBCASE("three quarters in, the newer count shows most: it becomes the previous") {
        gfx::LabelPass pass;
        static_cast<void>(Place(pass, two, 0.0));
        static_cast<void>(Place(pass, three, THREE_QUARTER_FADE));
        const gfx::PlacedLabel third = Of(Place(pass, four, THREE_QUARTER_FADE + QUARTER_FADE), 0);
        CHECK(third.count == THREE);
        CHECK(third.previous == TWO);
        CHECK(third.change == doctest::Approx(HALF));
    }
}

TEST_CASE("a stack turning to a third creature mid-glide glides on from where it is") {
    // 100, 110, 114: over 110. The fox at 110 moves to 130: over 114, gliding from 130, 16 px off, a ninth gone (14.2
    // left). Then 114 moves to 140 and 100 to 122: the middle is 130.7, over 130 again, gliding from where the label is,
    // 140 + 14.2, so 24.2 px off, a ninth gone.
    constexpr f32 MOVED = 130.0F;
    constexpr f32 THIRD = 114.0F;
    constexpr f32 THIRD_MOVED = 140.0F;
    constexpr f32 FIRST_MOVED = 122.0F;
    constexpr f32 OFF_AFTER_ONE = 16.0F * (1.0F - FRAME_STEP);
    constexpr f32 OFF_AFTER_TWO = (THIRD_MOVED + OFF_AFTER_ONE - MOVED) * (1.0F - FRAME_STEP);
    std::array<gfx::LabelInput, 3> labels{ { Fox(1, AT_100), Fox(2, AT_110), Fox(3, THIRD) } };
    gfx::LabelPass pass;
    static_cast<void>(Place(pass, labels, 0.0));
    labels[1].anchor.x = MOVED;
    const gfx::PlacedLabel first = Of(Place(pass, labels, FRAME_SECONDS), 0);
    CHECK(first.anchor.x == doctest::Approx(THIRD + OFF_AFTER_ONE));
    labels[2].anchor.x = THIRD_MOVED;
    labels[0].anchor.x = FIRST_MOVED;
    const gfx::PlacedLabel second = Of(Place(pass, labels, 2 * FRAME_SECONDS), 0);
    CHECK(second.anchor.x == doctest::Approx(MOVED + OFF_AFTER_TWO));
}

TEST_CASE("a stack whose creature is gone glides from where that creature was") {
    // 100, 110, 114: over 110. The fox at 110 goes: 100 and 114 tie, so over 100, gliding from 110, a ninth gone.
    constexpr f32 THIRD = 114.0F;
    constexpr f32 GLIDING = AT_100 + ((AT_110 - AT_100) * (1.0F - FRAME_STEP));
    gfx::LabelPass pass;
    const std::array<gfx::LabelInput, 3> before{ { Fox(1, AT_100), Fox(2, AT_110), Fox(3, THIRD) } };
    static_cast<void>(Place(pass, before, 0.0));
    const std::array<gfx::LabelInput, 2> after{ { Fox(1, AT_100), Fox(3, THIRD) } };
    const gfx::PlacedLabel stack = Of(Place(pass, after, FRAME_SECONDS), 0);
    CHECK(stack.anchor.x == doctest::Approx(GLIDING));
}

TEST_CASE("a stack's head going off the screen fades out") {
    // 100, 110, 120 stack over 110; the head at 100 leaves the screen while 110 stays: its label fades, a ninth a frame.
    constexpr f32 LEFT_OF_THE_SCREEN = -100.0F;
    std::array<gfx::LabelInput, 3> labels{ { Fox(1, AT_100), Fox(2, AT_110), Fox(3, AT_120) } };
    gfx::LabelPass pass;
    static_cast<void>(Place(pass, labels, 0.0));
    labels[0].anchor.x = LEFT_OF_THE_SCREEN;
    const gfx::PlacedLabel leaving = Of(Place(pass, labels, FRAME_SECONDS), 0);
    CHECK(leaving.opacity == doctest::Approx(1.0F - FRAME_STEP));
}

TEST_CASE("a label on the edge of a stack's room doesn't flicker") {
    // The player's box spans 80 to 120 and a fox heads a stack at 175. Another fox's name box overlaps that fox's from
    // 148 or 152, but its stack text, 60 px wide, would overlap the player's by 2 px at 148 and clear it by 2 at 152.
    constexpr f32 OVERLAPPING = 148.0F;
    constexpr f32 JUST_CLEAR = 152.0F;
    constexpr f32 WELL_CLEAR = 160.0F;
    constexpr f32 HEAD = 175.0F;
    constexpr u32 FRAMES = 10;
    constexpr std::size_t FOX = 1;
    constexpr std::size_t JOINER = 2;
    std::array<gfx::LabelInput, 3> labels{ { Player(1, AT_100), Fox(2, HEAD), Fox(3, OVERLAPPING) } };
    gfx::LabelPass pass;
    f64 clock = 0.0;
    for (u32 frame = 0; frame < FRAMES; ++frame) {
        labels[JOINER].anchor.x = frame % 2 == 0 ? OVERLAPPING : JUST_CLEAR;
        INFO("hidden, frame ", frame);
        const u32 count = Of(Place(pass, labels, clock), FOX).count;
        CHECK(count == 0);
        clock += FRAME_SECONDS;
    }
    labels[JOINER].anchor.x = WELL_CLEAR;
    const u32 count = Of(Place(pass, labels, clock), FOX).count;
    CHECK(count == 1);
    for (u32 frame = 0; frame < FRAMES; ++frame) {
        clock += FRAME_SECONDS;
        labels[JOINER].anchor.x = frame % 2 == 0 ? OVERLAPPING : JUST_CLEAR;
        INFO("counted, frame ", frame);
        const u32 counted = Of(Place(pass, labels, clock), FOX).count;
        CHECK(counted == 1);
    }
}

TEST_CASE("a label whose stack text reaches another stack of its name joins both") {
    // Foxes at 100 and 182 each head a stack. One at 135 overlaps the first; its stack text, 60 px wide, reaches 165,
    // over the second's box from 162: all three stack into one.
    constexpr f32 SECOND = 182.0F;
    constexpr u32 TWO = 2;
    const std::array<gfx::LabelInput, 3> labels{ { Fox(1, AT_100), Fox(2, SECOND), Fox(3, AT_135) } };
    gfx::LabelPass pass;
    const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, 0.0);
    REQUIRE(placed.size() == 1);
    CHECK(placed[0].count == TWO);
}

TEST_CASE("a stack merged through its text's room doesn't flicker on the edge") {
    // Foxes at 100 and 135 stack; the stack text's room over 135 (60 px wide, to 165) reaches the fox moving between
    // 180.5 and 181.5 (box from 160.5 or 161.5) on the edge of a shrink: once merged, it stays merged.
    constexpr f32 NEARER = 180.5F;
    constexpr f32 FURTHER = 181.5F;
    constexpr u32 FRAMES = 10;
    constexpr std::size_t MOVING = 1;
    std::array<gfx::LabelInput, 3> labels{ { Fox(1, AT_100), Fox(2, NEARER), Fox(3, AT_135) } };
    gfx::LabelPass pass;
    const u32 first = Of(Place(pass, labels, 0.0), 0).count;
    f64 clock = 0.0;
    for (u32 frame = 1; frame < FRAMES; ++frame) {
        clock += FRAME_SECONDS;
        labels[MOVING].anchor.x = frame % 2 == 0 ? NEARER : FURTHER;
        INFO("frame ", frame);
        const u32 count = Of(Place(pass, labels, clock), 0).count;
        CHECK(count == first);
    }
}

TEST_CASE("a label bridging two stacks doesn't flicker on the edge") {
    // Stack texts as wide as names, so only the boxes bridge. Foxes at 100 and 135 stack (the one at 135's box ends at
    // 155); a third fox moves to and fro, ranked before the fox at 135. So it is the fox at 135's box that is tested
    // against the mover's: grown by 4 if the two were in one stack last frame, shrunk by 4 if not.
    constexpr f32 PROBE_NEARER = 178.5F;  // box from 158.5: within 4 px of 155
    constexpr f32 PROBE_FURTHER = 179.5F; // box from 159.5: not
    constexpr f32 BAND_NEARER = 174.5F;   // box from 154.5: overlapping by 0.5
    constexpr f32 BAND_FURTHER = 175.5F;  // box from 155.5: apart by 0.5
    constexpr f32 BRIDGING = 170.0F;
    constexpr f32 APART = 190.0F;
    constexpr u32 FRAMES = 10;
    constexpr std::size_t MOVING = 1;
    constexpr u32 TWO = 2;
    constexpr u32 ONE = 1;
    f32 start = PROBE_NEARER;
    f32 nearer = PROBE_NEARER;
    f32 further = PROBE_FURTHER;
    u32 expected = ONE;
    SUBCASE("on the edge of the grown box, apart, it stays apart (the reviewer's probe)") {
    }
    SUBCASE("about the edge itself, bridged first, it stays bridged") {
        start = BRIDGING;
        nearer = BAND_NEARER;
        further = BAND_FURTHER;
        expected = TWO;
    }
    SUBCASE("about the edge itself, apart first, it stays apart") {
        start = APART;
        nearer = BAND_NEARER;
        further = BAND_FURTHER;
    }
    std::array<gfx::LabelInput, 3> labels{ { Fox(1, AT_100), Fox(2, start), Fox(3, AT_135) } };
    for (gfx::LabelInput& label : labels) {
        label.stackWidth = BOX_WIDTH;
    }
    gfx::LabelPass pass;
    const u32 count = Of(Place(pass, labels, 0.0), 0).count;
    CHECK(count == expected);
    f64 clock = 0.0;
    for (u32 frame = 1; frame < FRAMES; ++frame) {
        clock += FRAME_SECONDS;
        labels[MOVING].anchor.x = frame % 2 == 0 ? nearer : further;
        INFO("frame ", frame);
        const u32 counted = Of(Place(pass, labels, clock), 0).count;
        CHECK(counted == expected);
    }
}

TEST_CASE("a label hidden beside a stack of its name it has no room to join doesn't flicker") {
    // A fox at 100 heads a stack and the player's box spans 161.2 to 201.2. Another fox moves between 135.5 and 136.5:
    // its box overlaps the first's by 4.5 or 3.5, and its stack text's room (60 px wide, to 165.5 or 166.5) would
    // overlap the player's even shrunk by 4. Joining, it is hidden; hidden, its box grows toward the stack it couldn't
    // join, so it stays hidden rather than heading a stack of its own every other frame.
    constexpr f32 NEARER = 135.5F;
    constexpr f32 FURTHER = 136.5F;
    constexpr f32 PLAYER_AT = 181.2F;
    constexpr u32 FRAMES = 10;
    constexpr std::size_t JOINER = 1;
    constexpr u64 PLAYER_ENTITY = 9; // ranked first by priority, whatever its id
    std::array<gfx::LabelInput, 3> labels{ { Fox(1, AT_100), Fox(3, NEARER), Player(PLAYER_ENTITY, PLAYER_AT) } };
    gfx::LabelPass pass;
    f64 clock = 0.0;
    for (u32 frame = 0; frame < FRAMES; ++frame) {
        labels[JOINER].anchor.x = frame % 2 == 0 ? NEARER : FURTHER;
        const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, clock);
        INFO("frame ", frame);
        CHECK(std::ranges::find(placed, JOINER, &gfx::PlacedLabel::input) == placed.end());
        CHECK(Of(placed, 0).count == 0);
        clock += FRAME_SECONDS;
    }
}

TEST_CASE("a stack its text's room reaches needs room over its own label too") {
    // Foxes at 100 and 182 each head a stack, and the player's box spans 210 to 250. A fox at 135 joins the first and
    // its stack text's room reaches the second; but the room over 182 (152 to 212) would overlap the player's: so the
    // fox at 135 is hidden, and both stacks stay alone.
    constexpr f32 SECOND = 182.0F;
    constexpr f32 PLAYER_AT = 230.0F;
    const std::array<gfx::LabelInput, 4> labels{ { Fox(1, AT_100), Fox(2, SECOND), Fox(3, AT_135), Player(4, PLAYER_AT) } };
    gfx::LabelPass pass;
    const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, 0.0);
    REQUIRE(placed.size() == 3);
    for (const gfx::PlacedLabel& one : placed) {
        CHECK(one.count == 0);
    }
}

TEST_CASE("a label seen for the first time doesn't fade in") {
    constexpr f64 LATER = 5.0;
    const std::array<gfx::LabelInput, 1> labels{ { Fox(1, AT_100) } };
    gfx::LabelPass pass;
    static_cast<void>(Place(pass, {}, 0.0));
    const std::span<const gfx::PlacedLabel> placed = Place(pass, labels, LATER);
    REQUIRE(placed.size() == 1);
    CHECK(placed[0].opacity == 1.0F);
}

TEST_CASE("a label missing for a frame is forgotten, and comes back without fading in") {
    std::array<gfx::LabelInput, 1> labels{ { Fox(1, AT_100) } };
    labels[0].pixels = TOO_SMALL;
    gfx::LabelPass pass;
    static_cast<void>(Place(pass, labels, 0.0)); // faded out
    static_cast<void>(Place(pass, {}, FRAME_SECONDS));
    CHECK_FALSE(pass.Anchor(core::StableId{ 1 }).has_value());
    labels[0].pixels = READABLE;
    const std::span<const gfx::PlacedLabel> back = Place(pass, labels, 2 * FRAME_SECONDS);
    REQUIRE(back.size() == 1);
    CHECK(back[0].opacity == 1.0F);
}

TEST_CASE("a label gone off the screen, once faded out, isn't drawn but keeps its anchor") {
    constexpr f32 LEFT_OF_THE_SCREEN = -100.0F;
    std::array<gfx::LabelInput, 1> labels{ { Fox(1, AT_100) } };
    gfx::LabelPass pass;
    static_cast<void>(Place(pass, labels, 0.0));
    labels[0].anchor.x = LEFT_OF_THE_SCREEN;
    static_cast<void>(Place(pass, labels, FRAME_SECONDS)); // fading out
    const bool empty = Place(pass, labels, FRAME_SECONDS + WHOLE_FADE).empty();
    CHECK(empty);
    CHECK(pass.Anchor(core::StableId{ 1 }).value_or(core::Vec2{}).x == LEFT_OF_THE_SCREEN);
}

TEST_CASE("the stack label replaces its name and count") {
    constexpr u32 ELEVEN = 11;
    const std::string text = gfx::StackText("{name} (+{count})", "Fox", ELEVEN);
    CHECK(text == "Fox (+11)");
}

TEST_CASE("a Chinese stack label reads name（+N）") {
    constexpr u32 THREE = 3;
    const std::string text = gfx::StackText("{name}（+{count}）", "狐", THREE);
    CHECK(text == "狐（+3）");
}

TEST_CASE("a stack label without a placeholder leaves it out") {
    constexpr u32 TWO = 2;
    const std::string text = gfx::StackText("{name} and more", "Fox", TWO);
    CHECK(text == "Fox and more");
}

TEST_CASE("a stack label written into a string replaces what it held") {
    constexpr u32 FOUR = 4;
    std::string text = "a longer text from before";
    gfx::StackText(text, "{name} (+{count})", "Fox", FOUR);
    CHECK(text == "Fox (+4)");
}

TEST_CASE("a caption fades out with its size below the readable size") {
    constexpr f32 FADED_AT = THREE_QUARTERS * gfx::LABEL_MIN_PIXELS; // 6.75 pixels
    constexpr f32 BETWEEN = 7.875F;                                  // halfway from 6.75 to 9
    constexpr f32 DOUBLE = 2.0F;
    constexpr f32 BETWEEN_AT_DOUBLE = 15.75F;
    CHECK(gfx::CaptionOpacity(READABLE, SCALE) == 1.0F);
    CHECK(gfx::CaptionOpacity(gfx::LABEL_MIN_PIXELS, SCALE) == 1.0F);
    CHECK(gfx::CaptionOpacity(BETWEEN, SCALE) == doctest::Approx(HALF));
    CHECK(gfx::CaptionOpacity(FADED_AT, SCALE) == 0.0F);
    CHECK(gfx::CaptionOpacity(TOO_SMALL, SCALE) == 0.0F);
    CHECK(gfx::CaptionOpacity(BETWEEN_AT_DOUBLE, DOUBLE) == doctest::Approx(HALF));
}

} // namespace
