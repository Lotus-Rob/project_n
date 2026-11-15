#include "app/camera_control.hpp"

#include "app/camera_path.hpp"
#include "app/view.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"

namespace app {

namespace {

constexpr f32 FRAME_STEP_LIMIT_SECONDS = static_cast<f32>(FRAME_STEP_LIMIT_NS) / static_cast<f32>(core::NS_PER_SECOND);

f32 LengthSquared(core::Vec2 vec) {
    return (vec.x * vec.x) + (vec.y * vec.y);
}

// `vec` shortened to at most 1 long.
core::Vec2 AtMostOne(core::Vec2 vec) {
    const f32 length = std::sqrt(LengthSquared(vec));
    return length > 1.0F ? vec * (1.0F / length) : vec;
}

bool Inside(core::Vec2 cursor, i32 width, i32 height) {
    return cursor.x >= 0.0F && cursor.y >= 0.0F && cursor.x < static_cast<f32>(width) && cursor.y < static_cast<f32>(height);
}

// Where the cursor in the window's edge band points: each axis -1, 0 or 1, at most 1 long; zero clear of the band, and
// unfocused or with the cursor outside the window.
core::Vec2 EdgeOf(const CameraInput& input) {
    if (!input.focused || !input.cursor || !Inside(*input.cursor, input.width, input.height)) {
        return {};
    }
    const auto width = static_cast<f32>(input.width);
    const auto height = static_cast<f32>(input.height);
    const f32 band = EDGE_BAND * std::min(width, height);
    const auto toward = [band](f32 along, f32 size) {
        if (along < band) {
            return -1.0F;
        }
        return along >= size - band ? 1.0F : 0.0F;
    };
    return AtMostOne({ .x = toward(input.cursor->x, width), .y = toward(input.cursor->y, height) });
}

// How far the camera moves by the pointer this frame: the world under where the drag's movement started comes under
// the pointer.
core::Vec2 Dragged(const gfx::Camera& camera, const CameraInput& input) {
    if (!input.focused || !input.dragFrom || !input.cursor || !Inside(*input.cursor, input.width, input.height)) {
        return {};
    }
    return WindowToWorld(camera, *input.dragFrom, input.width, input.height) -
           WindowToWorld(camera, *input.cursor, input.width, input.height);
}

// The world the window's shorter side shows through `camera`: a screen, what pan speeds are counted in.
f32 ScreenOf(const gfx::Camera& camera, const CameraInput& input) {
    const auto shorter = static_cast<f32>(std::max(std::min(input.width, input.height), 1));
    return shorter /
           gfx::PixelsPerUnit(camera, static_cast<u32>(std::max(input.width, 1)), static_cast<u32>(std::max(input.height, 1)));
}

} // namespace

bool DragsCamera(Control control, bool combat) {
    return control == Control::DragCamera || (control == Control::Secondary && !combat);
}

void Drag::Press(Control button, core::Vec2 where) {
    if (!m_from) {
        m_from = where;
        m_button = button;
        m_pressedAt = where;
        m_strayed = false;
    }
}

bool Drag::Release(Control button, core::Vec2 where) {
    if (button != m_button || !m_from) {
        return false;
    }
    m_from.reset();
    return button == Control::DragCamera && !m_strayed &&
           LengthSquared(where - m_pressedAt) <= CLICK_SLOP_PIXELS * CLICK_SLOP_PIXELS;
}

void Drag::End() {
    m_from.reset();
}

std::optional<core::Vec2> Drag::Take(std::optional<core::Vec2> cursor, bool focused, i32 width, i32 height) {
    PN_PROFILE_ZONE();
    if (!m_from) {
        return std::nullopt;
    }
    if (!focused || !cursor || !Inside(*cursor, width, height)) {
        m_from.reset(); // the button may come up where the window never hears of it
        return std::nullopt;
    }
    m_strayed = m_strayed || LengthSquared(*cursor - m_pressedAt) > CLICK_SLOP_PIXELS * CLICK_SLOP_PIXELS;
    return std::exchange(m_from, cursor);
}

CameraControl::CameraControl(f32 startZoom)
  : m_startZoom{ startZoom }
  , m_camera{ .zoom = startZoom } {
}

void CameraControl::Zoom(f32 notches) {
    m_notches += notches;
}

void CameraControl::SnapBack() {
    m_snapBack = true;
}

void CameraControl::LoseFocus() {
    m_edgeArmed = false;
}

void CameraControl::Update(const CameraInput& input) {
    PN_PROFILE_ZONE();
    if (input.change != m_change) {
        m_change = input.change;
        m_start = input.followed.value_or(gfx::REFERENCE_CENTRE);
        m_camera = { .centre = m_start, .zoom = m_startZoom };
        m_notches = 0.0F;
        m_snapBack = false;
        m_free = false;
        m_segment.reset();
    }
    const core::Vec2 edge = EdgeOf(input);
    const bool cursorIn = input.focused && input.cursor && Inside(*input.cursor, input.width, input.height);
    // A cursor arriving over the window is likely in the band it crossed: only one seen clear of it since pans.
    const bool armed = m_edgeArmed;
    m_edgeArmed = cursorIn && (armed || LengthSquared(edge) <= 0.0F);
    if (const std::optional<f32> path = input.path) {
        FollowPath(input, *path);
        return;
    }
    if (!input.still) {
        // A wheel notch a frame would let fast frames zoom several notches while one snapshot shows (CULL_MARGIN).
        const f32 most = std::min(std::clamp(input.seconds, 0.0F, FRAME_STEP_LIMIT_SECONDS) * ZOOM_NOTCHES_PER_SECOND, 1.0F);
        const f32 notch = std::clamp(m_notches, -most, most);
        m_notches -= notch;
        m_camera.zoom = gfx::ZoomedBy(m_camera.zoom, notch);
        if ((m_notches > 0.0F && m_camera.zoom >= gfx::MAX_ZOOM) || (m_notches < 0.0F && m_camera.zoom <= gfx::MIN_ZOOM)) {
            m_notches = 0.0F;
        }
    }
    const bool follows = input.kind == core::CameraKind::Follow || (input.kind == core::CameraKind::Roam && !m_free);
    if (follows && input.followed) {
        m_camera.centre = *input.followed;
    }
    if (input.kind == core::CameraKind::Follow) {
        m_snapBack = false; // it never left
        return;
    }
    if (input.still) {
        return;
    }
    if (std::exchange(m_snapBack, false)) {
        m_free = false;
        m_camera.centre = input.kind == core::CameraKind::Roam ? input.followed.value_or(m_camera.centre) : m_start;
        return;
    }
    const core::Vec2 held = input.kind == core::CameraKind::Pan ? input.move + input.pan : input.pan;
    const bool edgePans = input.edgePan && armed && !input.dragFrom;
    const core::Vec2 pan = AtMostOne(held + (edgePans ? edge : core::Vec2{}));
    const core::Vec2 dragged = Dragged(m_camera, input);
    if (LengthSquared(pan) <= 0.0F && LengthSquared(dragged) <= 0.0F) {
        return;
    }
    m_free = input.kind == core::CameraKind::Roam;
    const f32 seconds = std::clamp(input.seconds, 0.0F, FRAME_STEP_LIMIT_SECONDS);
    m_camera.centre = m_camera.centre + (pan * (PAN_SCREENS_PER_SECOND * ScreenOf(m_camera, input) * seconds)) + dragged;
}

void CameraControl::FollowPath(const CameraInput& input, f32 seconds) {
    // The path replaces the player's camera inputs: none waits to move the camera after it.
    m_notches = 0.0F;
    m_snapBack = false;
    const std::optional<std::size_t> index = CameraPathSegment(seconds);
    if (!index) {
        return; // over: the run quits
    }
    if (index != m_segment) {
        m_segment = index;
        m_segmentFrom = m_camera.centre;
    }
    const CameraSegment& segment = CameraPath()[*index];
    const f32 through = SegmentThrough(*index, seconds);
    const bool follows = input.kind == core::CameraKind::Follow || segment.centre == PathCentre::Follows;
    if (follows && input.followed) {
        m_camera.centre = *input.followed;
    }
    if (input.still) {
        return;
    }
    m_camera.zoom = SegmentZoom(segment, through);
    if (follows) {
        m_free = false;
        return;
    }
    m_free = input.kind == core::CameraKind::Roam;
    m_camera.centre = segment.centre == PathCentre::At ? segment.point
                                                       : m_segmentFrom + (segment.pan * (ScreenOf(m_camera, input) * through));
}

} // namespace app
