#include "app/cast.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace app {

namespace {

struct SlotControl {
    Control control = Control::Primary;
    core::Slot slot = core::Slot::Generator;
};
constexpr std::array SLOT_CONTROLS{
    SlotControl{ .control = Control::Primary, .slot = core::Slot::Generator },
    SlotControl{ .control = Control::Secondary, .slot = core::Slot::Spender },
    SlotControl{ .control = Control::Movement, .slot = core::Slot::Movement },
    SlotControl{ .control = Control::Special1, .slot = core::Slot::Special1 },
    SlotControl{ .control = Control::Special2, .slot = core::Slot::Special2 },
    SlotControl{ .control = Control::Special3, .slot = core::Slot::Special3 },
    SlotControl{ .control = Control::Special4, .slot = core::Slot::Special4 },
    SlotControl{ .control = Control::Ultimate, .slot = core::Slot::Ultimate },
};

} // namespace

std::optional<core::Slot> SlotOf(Control control) {
    const std::span controls{ SLOT_CONTROLS }; // a span's iterator is a class everywhere, unlike std::array's
    const auto found = std::ranges::find(controls, control, &SlotControl::control);
    if (found == controls.end()) {
        return std::nullopt;
    }
    return found->slot;
}

bool SelectsInstead(Control control, bool combat, bool alt) {
    return control == Control::Primary && (!combat || alt);
}

std::optional<core::Action> CastAction(Control control, bool pressed, bool combat, bool alt, std::optional<core::Vec2> point) {
    const std::optional<core::Slot> slot = SlotOf(control);
    if (!pressed || !combat || !slot || !point || SelectsInstead(control, combat, alt)) {
        return std::nullopt;
    }
    return core::Action{ .kind = core::ActionKind::Cast, .slot = *slot, .point = *point };
}

} // namespace app
