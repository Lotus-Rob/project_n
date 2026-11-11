#include "app/damage_numbers.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/settings.hpp"
#include "core/tick_clock.hpp"

#include <doctest/doctest.h>

namespace {

constexpr core::StableId FOX_A{ 1 };
constexpr core::StableId FOX_B{ 2 };
constexpr u64 USE = 7;
constexpr i32 VALUE = 5; // a hit's, where any does
constexpr core::Settings EVERY_HIT{};
constexpr core::Settings COMBINED{ .combinedDamageNumbers = true };

core::Event Hit(u64 tick, core::StableId target, i32 value, core::HitKind kind = core::HitKind::Damage) {
    return { .tick = tick, .kind = core::EventKind::Hit, .hit = kind, .use = USE, .target = target, .value = value };
}

core::Event UseEnded(u64 tick) {
    return { .tick = tick, .kind = core::EventKind::UseEnded, .use = USE };
}

std::vector<core::TextItem> TextsAt(app::DamageNumbers& numbers, f64 renderTick) {
    std::vector<core::TextItem> texts;
    numbers.Texts(renderTick, texts);
    return texts;
}

// How many ticks after its own the number of `hit` starts: the `ordinal`th of `hits` on its target in its tick.
f64 DelayTicks(const core::Event& hit, u32 ordinal, u32 hits) {
    return static_cast<f64>(app::ScatterOf(hit, ordinal, hits).delay) * static_cast<f64>(core::TICKS_PER_SECOND);
}

// Later than any one hit's number on its target starts: past NUMBER_STAGGER_MIN_SECONDS.
constexpr f64 STARTED = 6.0;

TEST_CASE("every hit of a burst is played") {
    // Each hit's value is its own, so the numbers drawn over the burst's whole life can be told apart.
    constexpr u64 TICK = 100;
    constexpr i32 BURST = 4'000;
    constexpr u64 LIFE = 156; // ticks: NUMBER_SECONDS and the longest stagger, and some
    app::DamageNumbers numbers(EVERY_HIT);
    for (i32 value = 1; value <= BURST; ++value) {
        numbers.Play(Hit(TICK, value % 2 == 0 ? FOX_A : FOX_B, value));
    }
    numbers.Play(UseEnded(TICK));
    CHECK(numbers.Live() == BURST);
    std::unordered_set<i32> drawn;
    for (u64 tick = TICK; tick <= TICK + LIFE; ++tick) {
        for (const core::TextItem& text : TextsAt(numbers, static_cast<f64>(tick))) {
            drawn.insert(text.value);
        }
    }
    CHECK(drawn.size() == BURST);
    CHECK(numbers.Live() == 0);
}

TEST_CASE("a number is gone a second after it starts") {
    constexpr f64 SECOND = 60.0;
    const core::Event hit = Hit(0, FOX_A, VALUE);
    const f64 start = DelayTicks(hit, 0, 1);
    app::DamageNumbers numbers(EVERY_HIT);
    numbers.Play(hit);
    CHECK(TextsAt(numbers, start + SECOND - 1.0).size() == 1);
    CHECK(TextsAt(numbers, start + SECOND + 1.0).empty());
    CHECK(numbers.Live() == 0);
}

TEST_CASE("a number shows its hit's value over its target, in its kind's colour") {
    constexpr i32 SHOWN = 42;
    constexpr core::Vec2 HEAD{ .x = 300.0F, .y = 200.0F };
    app::DamageNumbers numbers(EVERY_HIT);
    core::Event hit = Hit(0, FOX_A, SHOWN, core::HitKind::Burn);
    hit.position = HEAD;
    numbers.Play(hit);
    const std::vector<core::TextItem> texts = TextsAt(numbers, STARTED);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0].kind == core::TextKind::Number);
    CHECK(texts[0].value == SHOWN);
    CHECK(texts[0].entity == FOX_A);
    CHECK(texts[0].previous.x == HEAD.x);
    CHECK(texts[0].current.y == HEAD.y);
    CHECK(texts[0].colour == app::KIND_COLOURS.at(std::to_underlying(core::HitKind::Burn)));
}

TEST_CASE("combined numbers add one use's hits per target") {
    constexpr u64 TICK = 10;
    app::DamageNumbers numbers(COMBINED);
    constexpr i32 FIRST_ON_A = 5;
    constexpr i32 SECOND_ON_A = 7;
    constexpr i32 ON_B = 3;
    constexpr i32 SUM_ON_A = 12;
    numbers.Play(Hit(TICK, FOX_A, FIRST_ON_A));
    numbers.Play(Hit(TICK, FOX_A, SECOND_ON_A));
    numbers.Play(Hit(TICK, FOX_B, ON_B));
    numbers.Play(UseEnded(TICK));
    const std::vector<core::TextItem> texts = TextsAt(numbers, static_cast<f64>(TICK));
    REQUIRE(texts.size() == 2);
    const auto onA = std::ranges::find(texts, FOX_A, &core::TextItem::entity);
    const auto onB = std::ranges::find(texts, FOX_B, &core::TextItem::entity);
    REQUIRE(onA != texts.end());
    REQUIRE(onB != texts.end());
    CHECK(onA->value == SUM_ON_A);
    CHECK(onB->value == ON_B);
}

TEST_CASE("combined numbers keep kinds apart") {
    constexpr u64 TICK = 10;
    app::DamageNumbers numbers(COMBINED);
    constexpr i32 DAMAGE = 5;
    constexpr i32 BURN = 4;
    numbers.Play(Hit(TICK, FOX_A, DAMAGE, core::HitKind::Damage));
    numbers.Play(Hit(TICK, FOX_A, BURN, core::HitKind::Burn));
    numbers.Play(UseEnded(TICK));
    const std::vector<core::TextItem> texts = TextsAt(numbers, static_cast<f64>(TICK));
    REQUIRE(texts.size() == 2);
    const auto damage = std::ranges::find(texts, DAMAGE, &core::TextItem::value);
    const auto burn = std::ranges::find(texts, BURN, &core::TextItem::value);
    REQUIRE(damage != texts.end());
    REQUIRE(burn != texts.end());
    CHECK(damage->colour == app::KIND_COLOURS.at(std::to_underlying(core::HitKind::Damage)));
    CHECK(burn->colour == app::KIND_COLOURS.at(std::to_underlying(core::HitKind::Burn)));
}

TEST_CASE("combined numbers keep uses apart") {
    constexpr u64 TICK = 10;
    app::DamageNumbers numbers(COMBINED);
    numbers.Play(Hit(TICK, FOX_A, VALUE));
    core::Event other = Hit(TICK, FOX_A, VALUE);
    other.use = USE + 1;
    numbers.Play(other);
    numbers.Play(UseEnded(TICK));
    const std::vector<core::TextItem> texts = TextsAt(numbers, static_cast<f64>(TICK));
    CHECK(texts.size() == 2);
    CHECK(numbers.Live() == 2); // the other use's number still waits for its end
}

TEST_CASE("a combined number waits for its use to end") {
    constexpr u64 HIT_TICK = 10;
    constexpr u64 END_TICK = 40;
    constexpr f64 AFTER_END = 46.0;
    app::DamageNumbers numbers(COMBINED);
    numbers.Play(Hit(HIT_TICK, FOX_A, VALUE));
    const std::vector<core::TextItem> waiting = TextsAt(numbers, static_cast<f64>(END_TICK));
    REQUIRE(waiting.size() == 1);
    CHECK(waiting[0].lift == 0.0F);
    CHECK(waiting[0].opacity == 1.0F);
    numbers.Play(UseEnded(END_TICK));
    const std::vector<core::TextItem> playing = TextsAt(numbers, AFTER_END);
    REQUIRE(playing.size() == 1);
    CHECK(playing[0].lift > 0.0F);
}

TEST_CASE("healing is signed and coloured apart from damage") {
    app::DamageNumbers numbers(EVERY_HIT);
    numbers.Play(Hit(0, FOX_A, VALUE, core::HitKind::Healing));
    numbers.Play(Hit(0, FOX_B, VALUE, core::HitKind::Damage));
    const std::vector<core::TextItem> texts = TextsAt(numbers, STARTED);
    REQUIRE(texts.size() == 2);
    const auto healing = std::ranges::find(texts, FOX_A, &core::TextItem::entity);
    const auto damage = std::ranges::find(texts, FOX_B, &core::TextItem::entity);
    REQUIRE(healing != texts.end());
    REQUIRE(damage != texts.end());
    CHECK(healing->sign == core::NumberSign::Plus);
    CHECK(healing->colour == app::KIND_COLOURS.at(std::to_underlying(core::HitKind::Healing)));
    CHECK(damage->sign == core::NumberSign::Plain);
    CHECK(healing->colour != damage->colour);
}

TEST_CASE("a number moves as the motion setting says") {
    constexpr core::Settings RISE{ .damageNumberMotion = core::NumberMotion::Rise };
    constexpr f64 HALF_A_SECOND = 30.0;
    constexpr f32 HALF = 0.5F;
    const core::Event hit = Hit(0, FOX_A, VALUE);
    const f64 start = DelayTicks(hit, 0, 1);
    app::DamageNumbers numbers(RISE);
    numbers.Play(hit);
    const std::vector<core::TextItem> texts = TextsAt(numbers, start + HALF_A_SECOND);
    REQUIRE(texts.size() == 1);
    const app::NumberPose pose = app::NumberMotionAt(core::NumberMotion::Rise, HALF);
    CHECK(texts[0].lift == doctest::Approx(pose.lift));
    CHECK(texts[0].opacity == doctest::Approx(pose.opacity));
}

TEST_CASE("rise lifts and fades a number") {
    constexpr f32 HALF = 0.5F;
    constexpr f32 END = 1.0F;
    const app::NumberPose start = app::NumberMotionAt(core::NumberMotion::Rise, 0.0F);
    CHECK(start.lift == 0.0F);
    CHECK(start.opacity == 1.0F);
    const app::NumberPose middle = app::NumberMotionAt(core::NumberMotion::Rise, HALF);
    CHECK(middle.lift > 0.0F);
    CHECK(middle.lift < app::RISE_EMS);
    CHECK(middle.opacity == 1.0F);
    const app::NumberPose end = app::NumberMotionAt(core::NumberMotion::Rise, END);
    CHECK(end.lift == doctest::Approx(app::RISE_EMS));
    CHECK(end.opacity == 0.0F);
}

TEST_CASE("bounce hops, settles and fades") {
    constexpr f32 TOP = 0.15F;
    constexpr f32 SETTLED = 0.5F;
    constexpr f32 END = 1.0F;
    const app::NumberPose start = app::NumberMotionAt(core::NumberMotion::Bounce, 0.0F);
    CHECK(start.lift == 0.0F);
    CHECK(start.opacity == 1.0F);
    const app::NumberPose top = app::NumberMotionAt(core::NumberMotion::Bounce, TOP);
    const app::NumberPose settled = app::NumberMotionAt(core::NumberMotion::Bounce, SETTLED);
    CHECK(top.lift == doctest::Approx(app::BOUNCE_TOP_EMS));
    CHECK(top.lift > settled.lift);
    CHECK(settled.lift == doctest::Approx(app::BOUNCE_REST_EMS));
    CHECK(settled.lift > 0.0F);
    CHECK(app::NumberMotionAt(core::NumberMotion::Bounce, END).opacity == 0.0F);
}

TEST_CASE("a number holds its opacity, then fades out evenly") {
    constexpr f32 FADE_START = 0.6F;
    constexpr f32 FADE_MIDDLE = 0.8F;
    constexpr f32 HALF = 0.5F;
    for (const core::NumberMotion motion : { core::NumberMotion::Bounce, core::NumberMotion::Rise }) {
        CHECK(app::NumberMotionAt(motion, FADE_START).opacity == 1.0F);
        CHECK(app::NumberMotionAt(motion, FADE_MIDDLE).opacity == doctest::Approx(HALF));
    }
}

// The user's addition: scatter and stagger, so numbers on one creature in one tick don't print over each other.

// The scatter of each number of `hits` hits on one target in one tick, by its hit's ordinal (its value), as drawn over
// their whole lives: they start up to NUMBER_STAGGER_MAX_SECONDS apart, so no one moment shows them all.
std::vector<core::Vec2> ScattersOf(u32 hits) {
    constexpr u64 LIFE = 156; // ticks: NUMBER_SECONDS and the longest stagger, and some
    app::DamageNumbers numbers(EVERY_HIT);
    for (u32 hit = 0; hit < hits; ++hit) {
        numbers.Play(Hit(0, FOX_A, static_cast<i32>(hit)));
    }
    std::vector<core::Vec2> scatters(hits);
    std::vector<bool> seen(hits);
    for (u64 tick = 0; tick <= LIFE; ++tick) {
        for (const core::TextItem& text : TextsAt(numbers, static_cast<f64>(tick))) {
            scatters.at(static_cast<std::size_t>(text.value)) = text.scatter;
            seen.at(static_cast<std::size_t>(text.value)) = true;
        }
    }
    REQUIRE(std::ranges::all_of(seen, [](bool shown) { return shown; }));
    return scatters;
}

TEST_CASE("a few numbers on one target scatter within the narrowest bounds, each its own way") {
    constexpr u32 FEW = 4;
    const std::vector<core::Vec2> scatters = ScattersOf(FEW);
    REQUIRE(scatters.size() == FEW);
    for (const core::Vec2 scatter : scatters) {
        CHECK(std::abs(scatter.x) <= app::NUMBER_SCATTER_X_EMS);
        CHECK(std::abs(scatter.y) <= app::NUMBER_SCATTER_Y_EMS);
    }
    CHECK(scatters[0].x != scatters[1].x);
    CHECK(scatters[0].y != scatters[1].y);
}

TEST_CASE("many numbers on one target scatter wider, up to the widest bounds") {
    constexpr u32 MANY = 64;
    const std::vector<core::Vec2> scatters = ScattersOf(MANY);
    REQUIRE(scatters.size() == MANY);
    f32 widest = 0.0F;
    for (const core::Vec2 scatter : scatters) {
        CHECK(std::abs(scatter.x) <= app::NUMBER_SCATTER_X_EMS * app::NUMBER_SCATTER_GROWTH_MAX);
        CHECK(std::abs(scatter.y) <= app::NUMBER_SCATTER_Y_EMS * app::NUMBER_SCATTER_GROWTH_MAX);
        widest = std::max(widest, std::abs(scatter.x));
    }
    CHECK(widest > app::NUMBER_SCATTER_X_EMS);
}

TEST_CASE("the scatter grows with the square root of the hits on a target, from four to sixty-four") {
    constexpr u32 FEW = 4;
    constexpr u32 SIXTEEN = 16;
    constexpr u32 MANY = 64;
    constexpr u32 MORE = 1'000;
    constexpr f32 DOUBLED = 2.0F;
    CHECK(app::ScatterGrowth(1) == 1.0F);
    CHECK(app::ScatterGrowth(FEW) == doctest::Approx(1.0F));
    CHECK(app::ScatterGrowth(SIXTEEN) == doctest::Approx(DOUBLED));
    CHECK(app::ScatterGrowth(MANY) == doctest::Approx(app::NUMBER_SCATTER_GROWTH_MAX));
    CHECK(app::ScatterGrowth(MORE) == doctest::Approx(app::NUMBER_SCATTER_GROWTH_MAX));
}

TEST_CASE("the same hits scatter and start the same way every run") {
    constexpr u32 HITS = 10;
    constexpr u64 LAST = 70;
    const core::Event hit = Hit(0, FOX_A, VALUE);
    const app::NumberScatter once = app::ScatterOf(hit, 3, HITS);
    const app::NumberScatter again = app::ScatterOf(hit, 3, HITS);
    CHECK(once.offset.x == again.offset.x);
    CHECK(once.offset.y == again.offset.y);
    CHECK(once.delay == again.delay);
    app::DamageNumbers first(EVERY_HIT);
    app::DamageNumbers second(EVERY_HIT);
    for (u32 index = 0; index < HITS; ++index) {
        first.Play(hit);
        second.Play(hit);
    }
    for (u64 tick = 0; tick <= LAST; ++tick) {
        const std::vector<core::TextItem> fromFirst = TextsAt(first, static_cast<f64>(tick));
        const std::vector<core::TextItem> fromSecond = TextsAt(second, static_cast<f64>(tick));
        REQUIRE(fromFirst.size() == fromSecond.size());
        for (std::size_t index = 0; index < fromFirst.size(); ++index) {
            CHECK(fromFirst[index].scatter.x == fromSecond[index].scatter.x);
            CHECK(fromFirst[index].scatter.y == fromSecond[index].scatter.y);
            CHECK(fromFirst[index].lift == fromSecond[index].lift);
        }
    }
}

TEST_CASE("a number isn't drawn before its delay, then starts at the start of its motion") {
    // The latest of a hundred hits on one target in one tick: its value is its ordinal, to find it.
    constexpr u32 HITS = 100;
    constexpr f64 MOMENT = 0.01; // ticks
    constexpr f32 STILL = 0.01F; // ems
    u32 latest = 0;
    for (u32 ordinal = 0; ordinal < HITS; ++ordinal) {
        if (DelayTicks(Hit(0, FOX_A, 0), ordinal, HITS) > DelayTicks(Hit(0, FOX_A, 0), latest, HITS)) {
            latest = ordinal;
        }
    }
    const f64 start = DelayTicks(Hit(0, FOX_A, 0), latest, HITS);
    REQUIRE(start > 1.0);
    app::DamageNumbers numbers(EVERY_HIT);
    for (u32 ordinal = 0; ordinal < HITS; ++ordinal) {
        numbers.Play(Hit(0, FOX_A, static_cast<i32>(ordinal)));
    }
    const std::vector<core::TextItem> before = TextsAt(numbers, start - MOMENT);
    CHECK(std::ranges::find(before, static_cast<i32>(latest), &core::TextItem::value) == before.end());
    const std::vector<core::TextItem> after = TextsAt(numbers, start + MOMENT);
    const auto started = std::ranges::find(after, static_cast<i32>(latest), &core::TextItem::value);
    REQUIRE(started != after.end());
    CHECK(started->opacity == 1.0F);
    CHECK(started->lift < STILL);
}

TEST_CASE("a replay scatters and staggers as the run it recorded: the timeline doesn't count") {
    constexpr u32 HITS = 10;
    constexpr u64 REPLAY_EPOCH = 3;
    const core::Event hit = Hit(0, FOX_A, VALUE);
    core::Event replayed = hit;
    replayed.epoch = REPLAY_EPOCH;
    const app::NumberScatter recorded = app::ScatterOf(hit, 3, HITS);
    const app::NumberScatter played = app::ScatterOf(replayed, 3, HITS);
    CHECK(recorded.offset.x == played.offset.x);
    CHECK(recorded.offset.y == played.offset.y);
    CHECK(recorded.delay == played.delay);
}

TEST_CASE("the stagger window grows with the hits on a target in a tick") {
    constexpr u32 TEN = 10;
    constexpr u32 HUNDRED = 100;
    constexpr f32 TEN_HITS_SECONDS = 0.2F;
    CHECK(app::StaggerWindow(1) == doctest::Approx(app::NUMBER_STAGGER_MIN_SECONDS));
    CHECK(app::StaggerWindow(TEN) == doctest::Approx(TEN_HITS_SECONDS));
    CHECK(app::StaggerWindow(HUNDRED) == doctest::Approx(app::NUMBER_STAGGER_MAX_SECONDS));
    constexpr f32 WIDEST_SECONDS = 1.5F;
    CHECK(app::NUMBER_STAGGER_MAX_SECONDS == doctest::Approx(WIDEST_SECONDS));
}

TEST_CASE("combined numbers have no scatter or delay") {
    constexpr u64 TICK = 10;
    app::DamageNumbers numbers(COMBINED);
    numbers.Play(Hit(TICK, FOX_A, VALUE));
    numbers.Play(Hit(TICK, FOX_A, VALUE));
    numbers.Play(UseEnded(TICK));
    const std::vector<core::TextItem> texts = TextsAt(numbers, static_cast<f64>(TICK));
    REQUIRE(texts.size() == 1);
    CHECK(texts[0].scatter.x == 0.0F);
    CHECK(texts[0].scatter.y == 0.0F);
    CHECK(texts[0].lift == 0.0F);
    CHECK(texts[0].opacity == 1.0F);
}

TEST_CASE("forgetting ages numbers out as drawing them does, drawing nothing") {
    constexpr f64 SECOND = 60.0;
    const core::Event hit = Hit(0, FOX_A, VALUE);
    const f64 start = DelayTicks(hit, 0, 1);
    app::DamageNumbers numbers(EVERY_HIT);
    numbers.Play(hit);
    numbers.Forget(start + SECOND - 1.0);
    CHECK(numbers.Live() == 1);
    numbers.Forget(start + SECOND + 1.0);
    CHECK(numbers.Live() == 0);
}

TEST_CASE("a new epoch clears the live numbers") {
    app::DamageNumbers numbers(COMBINED);
    numbers.Play(Hit(0, FOX_A, 1));
    numbers.Play(UseEnded(0));
    numbers.Play(Hit(0, FOX_B, 2));
    numbers.Play(Hit(0, FOX_A, 3, core::HitKind::Poison));
    REQUIRE(numbers.Live() == 3);
    numbers.Clear();
    CHECK(TextsAt(numbers, 0.0).empty());
    CHECK(numbers.Live() == 0);
}

} // namespace
