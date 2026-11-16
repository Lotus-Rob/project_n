#include "app/bindings.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"

namespace app {

namespace {

using platform::InputCode;

constexpr core::Vec2 MOVE_UP{ .x = 0.0F, .y = -1.0F };
constexpr core::Vec2 MOVE_DOWN{ .x = 0.0F, .y = 1.0F };
constexpr core::Vec2 MOVE_LEFT{ .x = -1.0F, .y = 0.0F };
constexpr core::Vec2 MOVE_RIGHT{ .x = 1.0F, .y = 0.0F };

// PadDpadRight is the last code: a code added after it goes here instead.
static_assert(
    std::to_underlying(InputCode::PadDpadRight) < std::numeric_limits<u64>::digits,
    "Bit() gives each input code a bit of a u64"
);

constexpr u64 Bit(InputCode code) {
    return u64{ 1 } << std::to_underlying(code);
}

f32 Length(core::Vec2 vec) {
    return std::hypot(vec.x, vec.y);
}

/// The held direction of `control`'s keys, else its stick past the dead zone, at most 1 long.
core::Vec2 Held(const std::vector<Binding>& table, u64 heldKeys, Control control, core::Vec2 stick) {
    core::Vec2 keys{};
    for (const Binding& binding : table) {
        if (binding.control == control && (heldKeys & Bit(binding.code)) != 0) {
            keys = keys + binding.direction;
        }
    }
    if (const f32 length = Length(keys); length > 0.0F) {
        return keys * (1.0F / length);
    }
    const f32 length = Length(stick);
    if (length <= STICK_DEAD_ZONE) {
        return {};
    }
    // Rescaled so movement starts from zero at the dead zone's edge.
    const f32 travel = std::min((length - STICK_DEAD_ZONE) / (1.0F - STICK_DEAD_ZONE), 1.0F);
    return stick * (travel / length);
}

} // namespace

Bindings::Bindings()
  : m_table{
      { .code = InputCode::KeyW, .control = Control::Move, .direction = MOVE_UP },
      { .code = InputCode::KeyA, .control = Control::Move, .direction = MOVE_LEFT },
      { .code = InputCode::KeyS, .control = Control::Move, .direction = MOVE_DOWN },
      { .code = InputCode::KeyD, .control = Control::Move, .direction = MOVE_RIGHT },
      { .code = InputCode::KeyUp, .control = Control::Move, .direction = MOVE_UP },
      { .code = InputCode::KeyLeft, .control = Control::Move, .direction = MOVE_LEFT },
      { .code = InputCode::KeyDown, .control = Control::Move, .direction = MOVE_DOWN },
      { .code = InputCode::KeyRight, .control = Control::Move, .direction = MOVE_RIGHT },
      { .code = InputCode::PadDpadUp, .control = Control::Move, .direction = MOVE_UP },
      { .code = InputCode::PadDpadLeft, .control = Control::Move, .direction = MOVE_LEFT },
      { .code = InputCode::PadDpadDown, .control = Control::Move, .direction = MOVE_DOWN },
      { .code = InputCode::PadDpadRight, .control = Control::Move, .direction = MOVE_RIGHT },
      { .code = InputCode::PadLeftX, .control = Control::Move, .direction = MOVE_RIGHT },
      { .code = InputCode::PadLeftY, .control = Control::Move, .direction = MOVE_DOWN },
      { .code = InputCode::KeyI, .control = Control::PanCamera, .direction = MOVE_UP },
      { .code = InputCode::KeyJ, .control = Control::PanCamera, .direction = MOVE_LEFT },
      { .code = InputCode::KeyK, .control = Control::PanCamera, .direction = MOVE_DOWN },
      { .code = InputCode::KeyL, .control = Control::PanCamera, .direction = MOVE_RIGHT },
      { .code = InputCode::PadRightX, .control = Control::PanCamera, .direction = MOVE_RIGHT },
      { .code = InputCode::PadRightY, .control = Control::PanCamera, .direction = MOVE_DOWN },
      { .code = InputCode::MouseMiddle, .control = Control::DragCamera },
      { .code = InputCode::KeyC, .control = Control::SnapBack },
      { .code = InputCode::PadRightStick, .control = Control::SnapBack },
      { .code = InputCode::MouseLeft, .control = Control::Primary },
      { .code = InputCode::MouseRight, .control = Control::Secondary },
      { .code = InputCode::KeySpace, .control = Control::Movement },
      { .code = InputCode::Key1, .control = Control::Special1 },
      { .code = InputCode::Key2, .control = Control::Special2 },
      { .code = InputCode::Key3, .control = Control::Special3 },
      { .code = InputCode::Key4, .control = Control::Special4 },
      { .code = InputCode::Key5, .control = Control::Ultimate },
      { .code = InputCode::MouseWheel, .control = Control::Zoom },
      { .code = InputCode::PadLeftShoulder, .control = Control::Zoom, .direction = MOVE_LEFT },
      { .code = InputCode::PadRightShoulder, .control = Control::Zoom, .direction = MOVE_RIGHT },
      { .code = InputCode::KeyMinus, .control = Control::Zoom, .direction = MOVE_LEFT },
      { .code = InputCode::KeyEquals, .control = Control::Zoom, .direction = MOVE_RIGHT },
      { .code = InputCode::KeyEscape, .control = Control::Quit },
      { .code = InputCode::KeyP, .control = Control::PauseWorld },
      { .code = InputCode::PadBack, .control = Control::PauseWorld },
      { .code = InputCode::KeyT, .control = Control::TargetCursor },
      { .code = InputCode::PadNorth, .control = Control::TargetCursor },
      { .code = InputCode::KeyEnter, .control = Control::Confirm },
      { .code = InputCode::PadSouth, .control = Control::Confirm },
      { .code = InputCode::KeyM, .control = Control::AdvanceCalendar },
      { .code = InputCode::KeyF2, .control = Control::CycleScene },
      { .code = InputCode::KeyF5, .control = Control::Save },
      { .code = InputCode::KeyF9, .control = Control::Load },
      { .code = InputCode::KeyF6, .control = Control::ToggleRecording },
      { .code = InputCode::KeyF7, .control = Control::DebugBurst },
      { .code = InputCode::KeyF8, .control = Control::ToggleShowcase },
      { .code = InputCode::KeyF10, .control = Control::PlayReplay },
  } {
}

std::optional<ControlEvent> Bindings::Handle(const platform::InputEvent& input) {
    PN_PROFILE_ZONE();
    const auto found = std::ranges::find(m_table, input.code, &Binding::code);
    if (found == m_table.end()) {
        return std::nullopt;
    }
    if (found->control == Control::Move) {
        if (input.code == InputCode::PadLeftX) {
            m_stick.x = input.value;
        }
        else if (input.code == InputCode::PadLeftY) {
            m_stick.y = input.value;
        }
        else if (input.pressed) {
            m_heldKeys |= Bit(input.code);
        }
        else {
            m_heldKeys &= ~Bit(input.code);
        }
        return std::nullopt;
    }
    if (found->control == Control::PanCamera) {
        if (input.code == InputCode::PadRightX) {
            m_panStick.x = input.value;
        }
        else if (input.code == InputCode::PadRightY) {
            m_panStick.y = input.value;
        }
        else if (input.pressed) {
            m_heldKeys |= Bit(input.code);
        }
        else {
            m_heldKeys &= ~Bit(input.code);
        }
        return std::nullopt;
    }
    if (found->control == Control::Zoom) {
        if (input.code == InputCode::MouseWheel) {
            return ControlEvent{ .control = Control::Zoom, .pressed = true, .value = input.value };
        }
        if (!input.pressed) {
            return std::nullopt;
        }
        return ControlEvent{ .control = Control::Zoom, .pressed = true, .value = found->direction.x };
    }
    return ControlEvent{
        .control = found->control, .pressed = input.pressed, .value = input.value, .position = input.position, .alt = input.alt
    };
}

bool DeviceInUse::Take(const platform::InputEvent& input) {
    PN_PROFILE_ZONE();
    const bool left = input.code == InputCode::PadLeftX || input.code == InputCode::PadLeftY;
    const bool right = input.code == InputCode::PadRightX || input.code == InputCode::PadRightY;
    if (left || right) {
        core::Vec2& stick = left ? m_stick : m_rightStick;
        (input.code == InputCode::PadLeftX || input.code == InputCode::PadRightX ? stick.x : stick.y) = input.value;
        if (Length(stick) <= STICK_DEAD_ZONE) {
            return false; // a stick at rest, or drifting: not the player's hand
        }
    }
    const bool keyboardOrMouse = input.device != platform::Device::Gamepad;
    const bool same =
        keyboardOrMouse == (m_device != platform::Device::Gamepad) && (keyboardOrMouse || input.family == m_family);
    m_device = input.device;
    m_family = input.family;
    return !same;
}

bool DeviceInUse::TakeCursor(std::optional<core::Vec2> cursor) {
    PN_PROFILE_ZONE();
    // Bit for bit, as HeldInput compares: any change of position is a move (-Wfloat-equal rules out ==).
    const bool moved = cursor && (!m_cursor || std::bit_cast<u64>(*cursor) != std::bit_cast<u64>(*m_cursor));
    m_cursor = cursor;
    return moved && Take({ .device = platform::Device::Mouse });
}

core::Vec2 Bindings::Move() const {
    PN_PROFILE_ZONE();
    return Held(m_table, m_heldKeys, Control::Move, m_stick);
}

core::Vec2 Bindings::Pan() const {
    PN_PROFILE_ZONE();
    return Held(m_table, m_heldKeys, Control::PanCamera, m_panStick);
}

} // namespace app
