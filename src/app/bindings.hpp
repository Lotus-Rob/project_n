#pragma once

/// @file
/// @ingroup app
/// Bindings (design doc 0002 §10): the table from the player's physical inputs to the game's
/// controls, and the held movement they add up to. Pure: tested with constructed input events.

#include "core/types.hpp"
#include "core/vec2.hpp"
#include "platform/window.hpp"

#include <optional>
#include <vector>

namespace app {

/// What the player can ask for, whatever the device.
enum class Control : u8 {
    Move,            ///< Walk, or pan without a follow target: an axis pair, held.
    Primary,         ///< The left button: casts the generator, or selects what is under the pointer.
    Secondary,       ///< The right button: casts the spender.
    Movement,        ///< Space: casts the movement ability.
    Special1,        ///< Key 1.
    Special2,        ///< Key 2.
    Special3,        ///< Key 3.
    Special4,        ///< Key 4.
    Ultimate,        ///< Key 5.
    Zoom,            ///< Zoom the camera by `value` notches.
    Quit,            ///< Leave the game.
    AdvanceCalendar, ///< Let a month pass (a debug key).
    Save,            ///< Save the game (a debug key).
    Load,            ///< Load the save (a debug key).
    ToggleRecording, ///< Start or finish recording a replay (a debug key).
    PlayReplay,      ///< Play the replay or stop it (a debug key).
    CycleScene,      ///< Switch to the next scene (a debug key).
    DebugBurst,      ///< Hit every fox thousands of times (a debug key).
    ToggleShowcase,  ///< Hide or show the arena's showcase seals (a debug key).
};

/// A control pressed or released.
struct ControlEvent {
    /// Which control.
    Control control = Control::Quit;
    /// Whether it went down (false: up). Zoom only ever goes down.
    bool pressed = false;
    /// Zoom's notches; 1 for a pressed button, 0 for a released one.
    f32 value = 0.0F;
    /// For a mouse button, the pointer in window coordinates.
    core::Vec2 position{};
    /// For a mouse button, whether Alt was held.
    bool alt = false;
};

/// One entry of the table.
struct Binding {
    /// The physical input.
    platform::InputCode code = platform::InputCode::KeyW;
    /// The control it drives.
    Control control = Control::Quit;
    /// For a Move key, the way it points; for a Zoom button, its notches in `x`.
    core::Vec2 direction{};
};

/// The stick's radial dead zone: a stick pushed less far than this, of its full travel, is at rest.
inline constexpr f32 STICK_DEAD_ZONE = 0.2F;

/// The device the player used last: what prompts show (phase 6), and whether the mouse cursor hovers.
class DeviceInUse {
public:
    /// Notes the device `input` came from; true when what prompts show changed (a gamepad after the
    /// keyboard or mouse, or the other way, or a gamepad of another family). The stick counts as
    /// used only past its dead zone, judged on both axes as Bindings::Move() judges it.
    [[nodiscard]] bool Take(const platform::InputEvent& input);

    /// Notes where the mouse cursor is over the window (none when it isn't): its moving is the mouse
    /// in use, though no input event says so. True when what prompts show changed, as Take().
    [[nodiscard]] bool TakeCursor(std::optional<core::Vec2> cursor);

    /// The device used last: the keyboard at the start.
    [[nodiscard]] platform::Device Used() const {
        return m_device;
    }

    /// The family of the gamepad used last; Other while the keyboard or mouse is in use.
    [[nodiscard]] platform::GamepadFamily Family() const {
        return m_family;
    }

private:
    platform::Device m_device = platform::Device::Keyboard;
    platform::GamepadFamily m_family = platform::GamepadFamily::Other;
    core::Vec2 m_stick{};                              // the stick's position as its axis events last put it
    std::optional<core::Vec2> m_cursor = std::nullopt; // where TakeCursor() saw the cursor last
};

/// The bindings in use, and the held state of the inputs that drive Move.
class Bindings {
public:
    /// The default bindings: WASD, the arrow keys and the left stick move; the left button casts or selects, the right casts,
    /// Space and 1 to 5 cast; the wheel and the shoulder buttons zoom; Escape quits; M, F2, F5, F9, F6 and F10 are the debug
    /// keys. Code, not data; bindings become data (a YAML table
    /// the player's rebinds save over) when rebinding lands with the settings screen (§10, §11).
    Bindings();

    /// The control `input` presses or releases, if any. Movement inputs update Move() instead and
    /// give none, so a key pressed and released between two frames still gives both events but
    /// leaves Move() as it was.
    [[nodiscard]] std::optional<ControlEvent> Handle(const platform::InputEvent& input);

    /// Where the held movement points: the keys held, else the stick past its dead zone, at most 1 long.
    [[nodiscard]] core::Vec2 Move() const;

private:
    std::vector<Binding> m_table;
    u32 m_heldKeys = 0; // a bit per InputCode of a Move key held down
    core::Vec2 m_stick{};
};

} // namespace app
