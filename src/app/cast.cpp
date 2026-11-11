#include "app/cast.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace app {

std::optional<core::Slot> SlotOf(Control control) {
    if (control == Control::Primary) {
        return core::Slot::Generator;
    }
    if (control == Control::Secondary) {
        return core::Slot::Spender;
    }
    if (control == Control::Movement) {
        return core::Slot::Movement;
    }
    if (control == Control::Special1) {
        return core::Slot::Special1;
    }
    if (control == Control::Special2) {
        return core::Slot::Special2;
    }
    if (control == Control::Special3) {
        return core::Slot::Special3;
    }
    if (control == Control::Special4) {
        return core::Slot::Special4;
    }
    if (control == Control::Ultimate) {
        return core::Slot::Ultimate;
    }
    return std::nullopt;
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
