#pragma once

// Coordinate conversions between Hyprland's global logical space, monitor
// pixel space, and the scaled-down main view.

#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

namespace hm::core {
    using Hyprutils::Math::CBox;
    using Hyprutils::Math::Vector2D;

    // global logical (cursor) -> monitor pixel coords
    Vector2D globalToMonitorPx(const Vector2D& global, const Vector2D& monitorPos, double scale);

    // monitor pixel box -> global logical coords (inverse of globalToMonitorPx)
    CBox monitorPxToGlobalBox(const CBox& px, const Vector2D& monitorPos, double scale);

    // point in the main view (monitor px) -> global logical coords of the real workspace
    Vector2D mainViewToGlobal(const Vector2D& px, const CBox& mainView, const Vector2D& monitorPos, const Vector2D& monitorSize);

    // box in global logical coords (e.g. a window) -> where it shows up in the main view
    CBox globalBoxToMainView(const CBox& box, const CBox& mainView, const Vector2D& monitorPos, const Vector2D& monitorSize);

    // a press becomes a drag once the cursor moved further than this (8 logical px)
    bool exceedsDragThreshold(const Vector2D& startPx, const Vector2D& nowPx, double scale);

    // ghost of a dragged window: follows the cursor, shrunk to at most maxH tall
    CBox dragGhost(const CBox& base, const Vector2D& startPx, const Vector2D& cursorPx, double maxH);
}
