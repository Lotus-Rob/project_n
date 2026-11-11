#include "gfx/labels.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"
#include "gfx/text_place.hpp"

namespace gfx {

namespace {

constexpr f32 FADE_FROM = 0.75F; // of the readable size: a caption is gone this far below it
// Smoothstep's cubic, 3t² − 2t³: a ramp whose ends are flat, so the fade has no visible start or end.
constexpr f32 SMOOTHSTEP_SQUARE = 3.0F;
constexpr f32 SMOOTHSTEP_CUBE = 2.0F;
// A box shrinks by no more than this part of its smaller side, so a small one never turns inside out.
constexpr f32 MOST_SHRINK = 0.25F;
// A cross-fade past this point shows the new count more than the old.
constexpr f32 HALF_WAY = 0.5F;

// A selected or hovered label is the one the player is looking at: it never hides others' names in a count.
bool Stacks(core::LabelPriority priority) {
    return priority < core::LabelPriority::Hovered;
}

// `value` moved `step` toward `target`, no further.
f32 Toward(f32 value, f32 target, f32 step) {
    return target > value ? std::min(value + step, target) : std::max(value - step, target);
}

f32 Squared(core::Vec2 vector) {
    return (vector.x * vector.x) + (vector.y * vector.y);
}

std::size_t CellOf(f32 pixels, std::size_t cells) {
    return static_cast<std::size_t>(std::clamp(std::floor(pixels / LABEL_GRID_PIXELS), 0.0F, static_cast<f32>(cells - 1)));
}

} // namespace

std::span<const PlacedLabel> LabelPass::Place(std::span<const LabelInput> labels, u32 width, u32 height, f64 clock, f32 scale) {
    PN_PROFILE_ZONE();
    ++m_frame;
    const bool first = !m_clock;
    // A clock that went back holds every fade for a frame rather than running it backwards.
    const auto elapsed = static_cast<f32>(std::max(clock - m_clock.value_or(clock), 0.0));
    m_clock = clock;
    const f32 step = elapsed / LABEL_FADE_SECONDS;
    Begin(labels, width, height);
    Resolve(labels, width, height);
    Centre(labels);
    m_placed.clear();
    FadeGone(step);
    Fade(labels, step, scale, first);
    // Once faded out, a gone label has nothing left to draw, so the pass holds only what is on the screen.
    std::erase_if(m_seen, [this](const auto& entry) { return entry.second.frame != m_frame && entry.second.opacity <= 0.0F; });
    return m_placed;
}

std::optional<core::Vec2> LabelPass::Anchor(core::StableId entity) const {
    PN_PROFILE_ZONE();
    const auto seen = m_seen.find(entity);
    return seen == m_seen.end() || seen->second.frame != m_frame ? std::nullopt : seen->second.anchor;
}

void LabelPass::Begin(std::span<const LabelInput> labels, u32 width, u32 height) {
    PN_PROFILE_ZONE();
    m_seenOf.resize(labels.size());
    for (std::size_t index = 0; index < labels.size(); ++index) {
        const auto [entry, created] = m_seen.try_emplace(labels[index].entity);
        entry->second.fresh = created;
        entry->second.frame = m_frame;
        entry->second.index = index;
        entry->second.last = labels[index];
        m_seenOf[index] = &entry->second;
    }
    // Only boxes on the screen go in: one past its edge is in the edge cells.
    const auto cells = [](u32 pixels) {
        return std::max(static_cast<std::size_t>(std::ceil(static_cast<f32>(pixels) / LABEL_GRID_PIXELS)), std::size_t{ 1 });
    };
    m_columns = cells(width);
    m_rows = cells(height);
    m_cells.resize(m_columns * m_rows);
    for (std::vector<std::size_t>& cell : m_cells) {
        cell.clear();
    }
}

void LabelPass::Resolve(std::span<const LabelInput> labels, u32 width, u32 height) {
    PN_PROFILE_ZONE();
    m_order.resize(labels.size());
    std::ranges::iota(m_order, std::size_t{ 0 });
    std::ranges::sort(m_order, [labels](std::size_t first, std::size_t second) {
        if (labels[first].priority != labels[second].priority) {
            return labels[first].priority > labels[second].priority;
        }
        return std::to_underlying(labels[first].entity) < std::to_underlying(labels[second].entity);
    });
    m_role.assign(labels.size(), NONE);
    m_stacks.clear();
    m_footprints.clear();
    // A stack covers every member's box, so labels of one name overlapping in a chain stack into one, and a label of
    // another name gives way wherever the stack is: one stack per crowd, never stacks drawn over each other.
    for (const std::size_t index : m_order) {
        const LabelInput& label = labels[index];
        const Box box = BoxAt(label.anchor, label.width, label.height, 0.0F);
        if (box.right <= 0.0F || box.left >= static_cast<f32>(width) || box.bottom <= 0.0F ||
            box.top >= static_cast<f32>(height)) {
            m_role[index] = OFF_SCREEN; // nothing draws there, so it takes no room from those that do
            continue;
        }
        if (Collect(index, labels)) {
            continue;
        }
        if (m_joined.empty()) {
            const std::size_t stack = m_stacks.size();
            m_stacks.push_back({ .head = index, .into = stack, .members = 1, .sum = label.anchor });
            m_role[index] = stack;
            Cover(box, stack, index);
        }
        else {
            Join(index, labels);
        }
    }
}

void LabelPass::Join(std::size_t index, std::span<const LabelInput> labels) {
    PN_PROFILE_ZONE();
    const LabelInput& label = labels[index];
    // The stack's text, wider than a name, may be drawn over any member: there must be room for it over each. A room
    // reaching a stack of its name takes that stack in too, so it never hides a label its name's stack would count.
    const auto room = [labels](std::size_t member) {
        const LabelInput& over = labels[member];
        return BoxAt(over.anchor, over.stackWidth, over.height, 0.0F);
    };
    m_rooms.assign(1, index);
    for (const std::size_t stack : m_joined) {
        if (m_stacks[stack].members == 1) {
            m_rooms.push_back(m_stacks[stack].head);
        }
    }
    // Reaches() adds the rooms of lone stacks it takes in: worked off as a stack, as the order doesn't matter.
    while (!m_rooms.empty()) {
        const std::size_t member = m_rooms.back();
        m_rooms.pop_back();
        if (Reaches(member, index, labels)) {
            return;
        }
    }
    for (const std::size_t stack : m_joined) {
        if (m_stacks[stack].members == 1) {
            Cover(room(m_stacks[stack].head), stack, m_stacks[stack].head);
        }
    }
    // The highest-ranked of the stacks it bridges takes the others in.
    const std::size_t into = std::ranges::min(m_joined);
    for (const std::size_t other : m_joined) {
        if (other != into) {
            m_stacks[other].into = into;
            m_stacks[into].members += m_stacks[other].members;
            m_stacks[into].sum = m_stacks[into].sum + m_stacks[other].sum;
        }
    }
    ++m_stacks[into].members;
    m_stacks[into].sum = m_stacks[into].sum + label.anchor;
    m_role[index] = into;
    Cover(BoxAt(label.anchor, label.width, label.height, 0.0F), into, index);
    Cover(room(index), into, index);
}

void LabelPass::Centre(std::span<const LabelInput> labels) {
    PN_PROFILE_ZONE();
    for (Stack& stack : m_stacks) {
        stack.nearest = std::numeric_limits<f32>::max();
        stack.held = NONE;
        stack.heldDistance = std::numeric_limits<f32>::max();
    }
    // The first in order of a tie, so the stack holds its place frame to frame.
    for (const std::size_t index : m_order) {
        if (m_role[index] >= OFF_SCREEN) {
            continue;
        }
        m_role[index] = Root(m_role[index]);
        Stack& stack = m_stacks[m_role[index]];
        const core::Vec2 mean = stack.sum * (1.0F / static_cast<f32>(stack.members));
        const f32 distance = Squared(labels[index].anchor - mean);
        if (distance < stack.nearest) {
            stack.nearest = distance;
            stack.over = index;
        }
        if (m_seenOf[index]->drawnOver && distance < stack.heldDistance) {
            stack.held = index;
            stack.heldDistance = distance;
        }
    }
    // As creatures move round a crowd's middle, the nearest changes often: the stack would hop between them.
    for (Stack& stack : m_stacks) {
        if (stack.held != NONE && std::sqrt(stack.heldDistance) <= std::sqrt(stack.nearest) + LABEL_HYSTERESIS_PIXELS) {
            stack.over = stack.held;
        }
    }
}

void LabelPass::FadeGone(f32 step) {
    PN_PROFILE_ZONE();
    m_gone.clear();
    for (auto& [entity, seen] : m_seen) {
        if (seen.frame != m_frame) {
            m_gone.push_back(&seen);
        }
    }
    // In drawing order, as the labels on the screen, so the frame is the same however the map holds them.
    std::ranges::sort(m_gone, [](const Seen* first, const Seen* second) {
        if (first->last.priority != second->last.priority) {
            return first->last.priority < second->last.priority;
        }
        return std::to_underlying(first->last.entity) > std::to_underlying(second->last.entity);
    });
    for (Seen* seen : m_gone) {
        seen->drawnOver = false;
        seen->anchor = std::nullopt;
        seen->opacity = Toward(seen->opacity, 0.0F, step);
        Count(*seen, seen->count, step, true);
        if (seen->opacity > 0.0F) {
            m_placed.push_back(
                { .input = LABEL_GONE,
                  .label = &seen->last,
                  .anchor = seen->drawn,
                  .count = seen->count,
                  .previous = seen->previous,
                  .change = seen->change,
                  .opacity = seen->opacity }
            );
        }
    }
}

void LabelPass::Fade(std::span<const LabelInput> labels, f32 step, f32 scale, bool first) {
    PN_PROFILE_ZONE();
    const auto heads = [this](std::size_t index) {
        return m_role[index] < OFF_SCREEN && m_stacks[m_role[index]].head == index;
    };
    for (Seen* seen : m_seenOf) {
        seen->drawnOver = false;
    }
    for (const std::size_t index : m_order) {
        if (heads(index)) {
            Seen& seen = *m_seenOf[index];
            Stack& stack = m_stacks[m_role[index]];
            const bool drawn = !seen.fresh && seen.opacity > 0.0F;
            stack.drawn = Glide(seen, labels, stack.over, step, drawn);
            Count(seen, stack.members - 1, step, drawn);
            m_seenOf[stack.over]->drawnOver = true;
        }
    }
    // Lowest priority first, so the highest draws on top.
    for (std::size_t remaining = m_order.size(); remaining > 0; --remaining) {
        const std::size_t index = m_order[remaining - 1];
        const LabelInput& label = labels[index];
        Seen& seen = *m_seenOf[index];
        const std::size_t role = m_role[index];
        const bool head = heads(index);
        seen.stack = role < OFF_SCREEN ? labels[m_stacks[role].head].entity : core::StableId{};
        core::Vec2 drawnAt = role >= OFF_SCREEN ? label.anchor : m_stacks[role].drawn;
        seen.anchor = drawnAt;
        if (!head) {
            // Stacked into another, hidden or gone off the screen, it fades out where it is, showing what it showed.
            const bool drawn = !seen.fresh && seen.opacity > 0.0F;
            drawnAt = Glide(seen, labels, index, step, drawn);
            Count(seen, seen.count, step, drawn);
        }
        const f32 target = head && label.complete && label.pixels >= LABEL_MIN_PIXELS * scale ? 1.0F : 0.0F;
        seen.opacity = seen.fresh && first ? target : Toward(seen.opacity, target, step);
        seen.drawn = drawnAt;
        if (head || seen.opacity > 0.0F) {
            m_placed.push_back(
                { .input = index,
                  .label = &seen.last,
                  .anchor = drawnAt,
                  .count = seen.count,
                  .previous = seen.previous,
                  .change = seen.change,
                  .opacity = seen.opacity }
            );
        }
    }
}

core::Vec2 LabelPass::Glide(Seen& seen, std::span<const LabelInput> labels, std::size_t over, f32 step, bool drawn) {
    const LabelInput& target = labels[over];
    if (!drawn) {
        seen.offset = {};
        seen.glide = 0.0F;
    }
    else if (seen.over != target.entity) {
        // From where it was drawn, moved on with the creature it was over if that is still here.
        core::Vec2 from = seen.overAnchor;
        if (const auto before = m_seen.find(seen.over); before != m_seen.end() && before->second.frame == m_frame) {
            from = labels[before->second.index].anchor;
        }
        seen.offset = from + seen.offset - target.anchor;
        seen.glide = 1.0F;
    }
    if (seen.glide > 0.0F) {
        const f32 left = std::max(seen.glide - step, 0.0F);
        seen.offset = left > 0.0F ? seen.offset * (left / seen.glide) : core::Vec2{};
        seen.glide = left;
    }
    seen.over = target.entity;
    seen.overAnchor = target.anchor;
    return target.anchor + seen.offset;
}

void LabelPass::Count(Seen& seen, u32 count, f32 step, bool drawn) {
    if (!drawn) {
        seen.count = count;
        seen.previous = count;
        seen.change = 1.0F;
        return;
    }
    if (count != seen.count) {
        if (count == seen.previous) {
            std::swap(seen.count, seen.previous);
            seen.change = 1.0F - seen.change;
        }
        else {
            // Two texts at most: the fainter of the two fading gives its share to the new count, so the total holds.
            if (seen.change >= HALF_WAY) {
                seen.previous = seen.count;
            }
            seen.change = std::min(seen.change, 1.0F - seen.change);
            seen.count = count;
        }
    }
    seen.change = std::min(seen.change + step, 1.0F);
    if (seen.change >= 1.0F) {
        seen.previous = seen.count;
    }
}

LabelPass::Box LabelPass::BoxAt(core::Vec2 anchor, f32 width, f32 height, f32 grow) {
    const f32 half = width * 0.5F;
    return { .left = anchor.x - half - grow,
             .top = anchor.y - height - grow,
             .right = anchor.x + half + grow,
             .bottom = anchor.y + grow };
}

f32 LabelPass::Grow(const Seen& seen, f32 width, f32 height) {
    if (seen.fresh) {
        return 0.0F; // a label never seen before takes neither side
    }
    return seen.stack != core::StableId{} ? -std::min(LABEL_HYSTERESIS_PIXELS, MOST_SHRINK * std::min(width, height))
                                          : LABEL_HYSTERESIS_PIXELS;
}

f32 LabelPass::Band(const Seen& seen, const Seen& other, f32 width, f32 height) {
    if (seen.fresh) {
        return 0.0F;
    }
    if (seen.stack == core::StableId{}) {
        // A hidden label must not shrink toward its own name's stack, or it would miss that stack, head one of its own
        // and overlap it again once shown, and so on every frame.
        return LABEL_HYSTERESIS_PIXELS;
    }
    if (other.fresh) {
        return 0.0F;
    }
    return seen.stack != core::StableId{} && seen.stack == other.stack
               ? LABEL_HYSTERESIS_PIXELS
               : -std::min(LABEL_HYSTERESIS_PIXELS, MOST_SHRINK * std::min(width, height));
}

f32 LabelPass::Against(const Seen& seen, const Footprint& other, bool joins, f32 width, f32 height) const {
    return joins ? Band(seen, *m_seenOf[other.member], width, height) : Grow(seen, width, height);
}

std::size_t LabelPass::Root(std::size_t stack) {
    while (m_stacks[stack].into != stack) {
        m_stacks[stack].into = m_stacks[m_stacks[stack].into].into; // halves the path for the next look
        stack = m_stacks[stack].into;
    }
    return stack;
}

void LabelPass::Cells(const Box& box, std::size_t& left, std::size_t& top, std::size_t& right, std::size_t& bottom) const {
    left = CellOf(box.left, m_columns);
    right = CellOf(box.right, m_columns);
    top = CellOf(box.top, m_rows);
    bottom = CellOf(box.bottom, m_rows);
}

bool LabelPass::Joins(const LabelInput& head, const LabelInput& label) {
    return head.nameKey == label.nameKey && Stacks(head.priority) && Stacks(label.priority);
}

bool LabelPass::Overlap(const Box& first, const Box& second) {
    return first.left < second.right && second.left < first.right && first.top < second.bottom && second.top < first.bottom;
}

bool LabelPass::Collect(std::size_t index, std::span<const LabelInput> labels) {
    const LabelInput& label = labels[index];
    const Seen& seen = *m_seenOf[index];
    m_joined.clear();
    bool blocked = false;
    std::size_t left = 0;
    std::size_t top = 0;
    std::size_t right = 0;
    std::size_t bottom = 0;
    Cells(BoxAt(label.anchor, label.width, label.height, LABEL_HYSTERESIS_PIXELS), left, top, right, bottom);
    for (std::size_t row = top; row <= bottom; ++row) {
        for (std::size_t column = left; column <= right; ++column) {
            for (const std::size_t footprint : m_cells[(row * m_columns) + column]) {
                const Footprint& other = m_footprints[footprint];
                const std::size_t stack = Root(other.stack);
                const bool joins = Joins(labels[m_stacks[stack].head], label);
                const f32 grow = Against(seen, other, joins, label.width, label.height);
                if (!Overlap(BoxAt(label.anchor, label.width, label.height, grow), other.box)) {
                    continue;
                }
                if (!joins) {
                    blocked = true; // a label under another name's is hidden, not counted into a stack beside it
                }
                else if (std::ranges::find(m_joined, stack) == m_joined.end()) {
                    m_joined.push_back(stack);
                }
            }
        }
    }
    return blocked;
}

bool LabelPass::Reaches(std::size_t member, std::size_t index, std::span<const LabelInput> labels) {
    const LabelInput& over = labels[member];
    const LabelInput& label = labels[index];
    const Seen& seen = *m_seenOf[member];
    std::size_t left = 0;
    std::size_t top = 0;
    std::size_t right = 0;
    std::size_t bottom = 0;
    Cells(BoxAt(over.anchor, over.stackWidth, over.height, LABEL_HYSTERESIS_PIXELS), left, top, right, bottom);
    for (std::size_t row = top; row <= bottom; ++row) {
        for (std::size_t column = left; column <= right; ++column) {
            for (const std::size_t footprint : m_cells[(row * m_columns) + column]) {
                const Footprint& other = m_footprints[footprint];
                const std::size_t stack = Root(other.stack);
                if (std::ranges::find(m_joined, stack) != m_joined.end()) {
                    continue;
                }
                const bool joins = Joins(labels[m_stacks[stack].head], label);
                const f32 grow = Against(seen, other, joins, over.stackWidth, over.height);
                if (!Overlap(BoxAt(over.anchor, over.stackWidth, over.height, grow), other.box)) {
                    continue;
                }
                if (!joins) {
                    return true;
                }
                m_joined.push_back(stack);
                if (m_stacks[stack].members == 1) {
                    m_rooms.push_back(m_stacks[stack].head);
                }
            }
        }
    }
    return false;
}

void LabelPass::Cover(const Box& box, std::size_t stack, std::size_t member) {
    const std::size_t footprint = m_footprints.size();
    m_footprints.push_back({ .box = box, .stack = stack, .member = member });
    std::size_t left = 0;
    std::size_t top = 0;
    std::size_t right = 0;
    std::size_t bottom = 0;
    Cells(box, left, top, right, bottom);
    for (std::size_t row = top; row <= bottom; ++row) {
        for (std::size_t column = left; column <= right; ++column) {
            m_cells[(row * m_columns) + column].push_back(footprint);
        }
    }
}

void StackText(std::string& into, std::string_view stack, std::string_view name, u32 count) {
    PN_PROFILE_ZONE();
    constexpr std::string_view NAME = "{name}";
    constexpr std::string_view COUNT = "{count}";
    // Both placeholders are found in the table's text, so a name that itself reads "{count}" stays as it is.
    struct Fill {
        std::size_t place;
        std::string_view placeholder;
        bool name;
    };
    std::array<Fill, 2> fills{ { { .place = stack.find(NAME), .placeholder = NAME, .name = true },
                                 { .place = stack.find(COUNT), .placeholder = COUNT, .name = false } } };
    std::ranges::sort(fills, {}, &Fill::place);
    into.clear();
    std::size_t from = 0;
    for (const Fill& fill : fills) {
        if (fill.place == std::string_view::npos) {
            continue;
        }
        into.append(stack.substr(from, fill.place - from));
        if (fill.name) {
            into.append(name);
        }
        else {
            std::format_to(std::back_inserter(into), "{}", count);
        }
        from = fill.place + fill.placeholder.size();
    }
    into.append(stack.substr(from));
}

std::string StackText(std::string_view stack, std::string_view name, u32 count) {
    PN_PROFILE_ZONE();
    std::string text;
    StackText(text, stack, name, count);
    return text;
}

void CaptionFades::Begin(f64 clock) {
    ++m_frame;
    m_step = static_cast<f32>(std::max(clock - m_clock.value_or(clock), 0.0)) / LABEL_FADE_SECONDS;
    m_clock = clock;
}

f32 CaptionFades::Opacity(core::StringId string, bool complete) {
    Fade& fade = m_fades.at(std::to_underlying(string));
    if (!complete) {
        fade.opacity = 0.0F;
    }
    else if (fade.frame != m_frame) {
        fade.opacity = Toward(fade.opacity, 1.0F, m_step);
    }
    fade.frame = m_frame;
    return fade.opacity;
}

f32 CaptionOpacity(f32 pixels, f32 scale) {
    PN_PROFILE_ZONE();
    const f32 readable = LABEL_MIN_PIXELS * scale;
    const f32 from = FADE_FROM * readable;
    const f32 ramp = std::clamp((pixels - from) / (readable - from), 0.0F, 1.0F);
    return ramp * ramp * (SMOOTHSTEP_SQUARE - (SMOOTHSTEP_CUBE * ramp));
}

} // namespace gfx
