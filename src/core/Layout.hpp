#pragma once

// Pure geometry of the overview. No compositor dependencies, so it can be
// unit tested. All boxes are in monitor pixel coordinates.

#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include <cstddef>
#include <vector>

namespace hm::core {
    using Hyprutils::Math::CBox;
    using Hyprutils::Math::Vector2D;

    struct SLayout {
        std::vector<CBox> tiles;   // one per workspace, left to right
        CBox              newSlot; // the "+" slot, right after the last tile
        CBox              main;    // active workspace, below the bar
    };

    // monitorPx: monitor size in pixels (transformed), scale: monitor scale
    SLayout computeLayout(const Vector2D& monitorPx, double scale, size_t workspaceCount);

    enum class eHit {
        NONE,
        TILE,
        NEW_SLOT,
        MAIN,
    };

    struct SHit {
        eHit kind = eHit::NONE;
        int  tile = -1; // valid when kind == TILE
    };

    SHit hitTest(const SLayout& layout, const Vector2D& px);
}
