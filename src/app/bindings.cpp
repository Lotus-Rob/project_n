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

// PadRightShoulder is the last code: a code added after it goes here too.
static_assert(
    std::to_underlying(InputCode::PadRightShoulder) < std::numeric_limits<u32>::digits,
    "Bit() gives each input code a bit of a u32"
);

constexpr u32 Bit(InputCode code) {
    return 1U << std::to_underlying(code);
}

f32 Length(core::Vec2 vec) {
    return std::hypot(vec.x, vec.y);
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
      { .code = InputCode::PadLeftX, .control = Control::Move, .direction = MOVE_RIGHT },
      { .code = InputCode::PadLeftY, .control = Control::Move, .direction = MOVE_DOWN },
      { .code = InputCode::MouseLeft, .control = Control::Select },
      { .code = InputCode::MouseWheel, .control = Control::Zoom },
      { .code = InputCode::PadLeftShoulder, .control = Control::Zoom, .direction = MOVE_LEFT },
      { .code = InputCode::PadRightShoulder, .control = Control::Zoom, .direction = MOVE_RIGHT },
      { .code = InputCode::KeyEscape, .control = Control::Quit },
      { .code = InputCode::KeyM, .control = Control::AdvanceCalendar },
      { .code = InputCode::KeyF2, .control = Control::CycleScene },
      { .code = InputCode::KeyF5, .control = Control::Save },
      { .code = InputCode::KeyF9, .control = Control::Load },
      { .code = InputCode::KeyF6, .control = Control::ToggleRecording },
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
        .control = found->control, .pressed = input.pressed, .value = input.value, .position = input.position
    };
}

bool DeviceInUse::Take(const platform::InputEvent& input) {
    PN_PROFILE_ZONE();
    const bool stick = input.code == InputCode::PadLeftX || input.code == InputCode::PadLeftY;
    if (stick && std::abs(input.value) <= STICK_DEAD_ZONE) {
        return false; // a stick at rest, or drifting: not the player's hand
    }
    const bool keyboardOrMouse = input.device != platform::Device::Gamepad;
    const bool same =
        keyboardOrMouse == (m_device != platform::Device::Gamepad) && (keyboardOrMouse || input.family == m_family);
    m_device = input.device;
    m_family = input.family;
    return !same;
}

core::Vec2 Bindings::Move() const {
    PN_PROFILE_ZONE();
    core::Vec2 keys{};
    for (const Binding& binding : m_table) {
        if (binding.control == Control::Move && (m_heldKeys & Bit(binding.code)) != 0) {
            keys = keys + binding.direction;
        }
    }
    if (const f32 length = Length(keys); length > 0.0F) {
        return keys * (1.0F / length);
    }
    const f32 length = Length(m_stick);
    if (length <= STICK_DEAD_ZONE) {
        return {};
    }
    // Rescaled so movement starts from zero at the dead zone's edge.
    const f32 travel = std::min((length - STICK_DEAD_ZONE) / (1.0F - STICK_DEAD_ZONE), 1.0F);
    return m_stick * (travel / length);
}

} // namespace app
