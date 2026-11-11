#include "app/damage_numbers.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"
#include "core/random.hpp"
#include "core/tick_clock.hpp"

namespace app {

namespace {

// Fast at first, slowing to a stop at 1.
f32 EaseOut(f32 progress) {
    const f32 left = 1.0F - progress;
    return 1.0F - (left * left);
}

// Slow at both ends.
f32 EaseInOut(f32 progress) {
    constexpr f32 THREE = 3.0F;
    constexpr f32 TWO = 2.0F;
    return progress * progress * (THREE - (TWO * progress));
}

// What ScatterOf()'s hashes are for, so each draws its own number.
enum class Draw : u8 {
    ScatterX,
    ScatterY,
    Stagger,
};

// Boost's hash_combine: `part` mixed into `hash` so nearby values spread.
std::size_t Combine(std::size_t hash, u64 part) {
    constexpr std::size_t MIX = 0x9E37'79B9U;
    constexpr u32 LEFT = 6;
    constexpr u32 RIGHT = 2;
    return hash ^ (std::hash<u64>{}(part) + MIX + (hash << LEFT) + (hash >> RIGHT));
}

// How far `age` is from `begin` to `end`, from 0 to 1.
f32 Progress(f32 age, f32 begin, f32 end) {
    return std::clamp((age - begin) / (end - begin), 0.0F, 1.0F);
}

} // namespace

NumberPose NumberMotionAt(core::NumberMotion motion, f32 seconds) {
    PN_PROFILE_ZONE();
    const f32 age = std::clamp(seconds, 0.0F, NUMBER_SECONDS);
    f32 lift = 0.0F;
    if (motion == core::NumberMotion::Rise) {
        lift = RISE_EMS * EaseOut(age / NUMBER_SECONDS);
    }
    else if (age < BOUNCE_TOP_SECONDS) {
        lift = BOUNCE_TOP_EMS * EaseOut(age / BOUNCE_TOP_SECONDS);
    }
    else {
        const f32 settled = EaseInOut(Progress(age, BOUNCE_TOP_SECONDS, BOUNCE_REST_SECONDS));
        lift = BOUNCE_TOP_EMS + ((BOUNCE_REST_EMS - BOUNCE_TOP_EMS) * settled);
    }
    return { .lift = lift, .opacity = 1.0F - Progress(age, NUMBER_FADE_START, NUMBER_SECONDS) };
}

f32 StaggerWindow(u32 hits) {
    return std::clamp(
        static_cast<f32>(hits) * NUMBER_STAGGER_PER_HIT_SECONDS, NUMBER_STAGGER_MIN_SECONDS, NUMBER_STAGGER_MAX_SECONDS
    );
}

f32 ScatterGrowth(u32 hits) {
    return std::clamp(
        std::sqrt(static_cast<f32>(hits) / static_cast<f32>(NUMBER_SCATTER_GROWTH_HITS)), 1.0F, NUMBER_SCATTER_GROWTH_MAX
    );
}

NumberScatter ScatterOf(const core::Event& hit, u32 ordinal, u32 hits) {
    PN_PROFILE_ZONE();
    // The use as the key: it already names the tick and the ability, and stays the same in a replay.
    const auto unit = [&](Draw draw) {
        return static_cast<f32>(
            core::ToUnit(core::Random(hit.use, hit.tick, std::to_underlying(hit.target), std::to_underlying(draw), ordinal))
        );
    };
    constexpr f32 TWO = 2.0F;
    const f32 growth = ScatterGrowth(hits);
    return { .offset = { .x = ((TWO * unit(Draw::ScatterX)) - 1.0F) * NUMBER_SCATTER_X_EMS * growth,
                         .y = ((TWO * unit(Draw::ScatterY)) - 1.0F) * NUMBER_SCATTER_Y_EMS * growth },
             .delay = unit(Draw::Stagger) * StaggerWindow(hits) };
}

DamageNumbers::DamageNumbers(const core::Settings& settings)
  : m_combined(settings.combinedDamageNumbers)
  , m_motion(settings.damageNumberMotion) {
    m_playing.reserve(NUMBER_RESERVE);
}

void DamageNumbers::Play(const core::Event& event) {
    PN_PROFILE_ZONE();
    if (event.kind == core::EventKind::UseEnded) {
        if (m_waiting.empty()) {
            return;
        }
        // Moved in the order they began, so the oldest still draws first; the rest keep theirs.
        std::size_t kept = 0;
        for (Number& number : m_waiting) {
            if (number.use == event.use) {
                number.start = event.tick;
                m_playing.push_back(number);
            }
            else {
                m_waiting[kept++] = number;
            }
        }
        m_waiting.resize(kept);
        m_waitingOf.clear();
        for (std::size_t index = 0; index < m_waiting.size(); ++index) {
            const Number& number = m_waiting[index];
            m_waitingOf.emplace(Key{ .use = number.use, .target = number.target, .kind = number.kind }, index);
        }
        return;
    }
    const Number number{ .use = event.use,
                         .target = event.target,
                         .kind = event.hit,
                         .position = event.position,
                         .value = event.value,
                         .start = event.tick };
    if (!m_combined) {
        u32& hits = m_hitsIn[Moment{ .target = event.target, .tick = event.tick }];
        m_playing.emplace_back(number).ordinal = hits++;
        return;
    }
    const auto [found, added] =
        m_waitingOf.try_emplace(Key{ .use = event.use, .target = event.target, .kind = event.hit }, m_waiting.size());
    if (added) {
        m_waiting.push_back(number);
    }
    else {
        m_waiting[found->second].value += event.value;
    }
}

void DamageNumbers::Clear() {
    if (!m_waiting.empty()) {
        PN_LOG_DEBUG(
            "a new timeline discarded {} combined damage numbers still waiting for their use to end", m_waiting.size()
        );
    }
    m_playing.clear();
    m_waiting.clear();
    m_waitingOf.clear();
    m_hitsIn.clear();
    m_unsettled = 0;
}

void DamageNumbers::Settle() {
    PN_PROFILE_ZONE();
    for (std::size_t index = m_unsettled; index < m_playing.size(); ++index) {
        Number& number = m_playing[index];
        const auto hits = m_hitsIn.find(Moment{ .target = number.target, .tick = number.start });
        if (hits != m_hitsIn.end()) { // combined numbers aren't counted: they start at once, where they are
            const NumberScatter scatter =
                ScatterOf({ .tick = number.start, .use = number.use, .target = number.target }, number.ordinal, hits->second);
            number.scatter = scatter.offset;
            number.delay = scatter.delay;
        }
    }
    m_hitsIn.clear();
    m_unsettled = m_playing.size();
}

void DamageNumbers::Forget(f64 renderTick) {
    PN_PROFILE_ZONE();
    Settle();
    std::erase_if(m_playing, [renderTick](const Number& number) { return AgeOf(number, renderTick) >= NUMBER_SECONDS; });
    m_unsettled = m_playing.size();
}

void DamageNumbers::Texts(f64 renderTick, std::vector<core::TextItem>& out) {
    PN_PROFILE_ZONE();
    Forget(renderTick);
    out.reserve(out.size() + Live());
    for (const Number& number : m_playing) {
        const f32 age = AgeOf(number, renderTick);
        if (age >= 0.0F) { // one still waiting out its stagger isn't drawn yet
            out.push_back(TextOf(number, NumberMotionAt(m_motion, age)));
        }
    }
    const NumberPose held = NumberMotionAt(m_motion, 0.0F);
    for (const Number& number : m_waiting) {
        out.push_back(TextOf(number, held));
    }
}

std::size_t DamageNumbers::Live() const {
    return m_playing.size() + m_waiting.size();
}

f32 DamageNumbers::AgeOf(const Number& number, f64 renderTick) {
    return static_cast<f32>((renderTick - static_cast<f64>(number.start)) / static_cast<f64>(core::TICKS_PER_SECOND)) -
           number.delay;
}

core::TextItem DamageNumbers::TextOf(const Number& number, NumberPose pose) {
    return { .kind = core::TextKind::Number,
             .value = number.value,
             .sign = number.kind == core::HitKind::Healing ? core::NumberSign::Plus : core::NumberSign::Plain,
             .previous = number.position,
             .current = number.position,
             .scatter = number.scatter,
             .lift = pose.lift,
             .opacity = pose.opacity,
             .entity = number.target,
             .colour = KIND_COLOURS.at(std::to_underlying(number.kind)) };
}

std::size_t DamageNumbers::KeyHash::operator()(const Key& key) const {
    return Combine(Combine(std::hash<u64>{}(key.use), std::to_underlying(key.target)), std::to_underlying(key.kind));
}

std::size_t DamageNumbers::MomentHash::operator()(const Moment& moment) const {
    return Combine(std::hash<u64>{}(moment.tick), std::to_underlying(moment.target));
}

} // namespace app
