#include "Layout.hpp"

namespace hm::core {
    SLayout computeLayout(const Vector2D& monitorPx, double scale, size_t workspaceCount) {
        SLayout layout;
        if (monitorPx.x <= 0 || monitorPx.y <= 0)
            return layout;

        const double MARGIN = 32.0 * scale;
        const double GAP    = 24.0 * scale;
        const double ASPECT = monitorPx.x / monitorPx.y;
        const auto   SLOTS  = (double)(workspaceCount + 1); // + "new workspace" slot

        double       tileH = monitorPx.y * 0.14;
        double       tileW = tileH * ASPECT;

        const double availableW = monitorPx.x - 2 * MARGIN;
        double       totalW     = SLOTS * tileW + (SLOTS - 1) * GAP;
        if (totalW > availableW) {
            // shrink tiles (not gaps) so the whole row fits
            const double FIT = (availableW - (SLOTS - 1) * GAP) / (SLOTS * tileW);
            tileW *= FIT;
            tileH *= FIT;
            totalW = SLOTS * tileW + (SLOTS - 1) * GAP;
        }

        double       x = (monitorPx.x - totalW) / 2.0;
        const double y = MARGIN;

        for (size_t i = 0; i < workspaceCount; ++i) {
            layout.tiles.emplace_back(x, y, tileW, tileH);
            x += tileW + GAP;
        }
        layout.newSlot = CBox{x, y, tileW, tileH};

        // main view: everything below the bar, keeping the monitor's aspect
        const double BAR_BOTTOM   = y + tileH + MARGIN;
        const double MAIN_AVAIL_H = monitorPx.y - BAR_BOTTOM - MARGIN;
        const double MAIN_AVAIL_W = monitorPx.x - 2 * MARGIN;
        double       mainW        = MAIN_AVAIL_W;
        double       mainH        = mainW / ASPECT;
        if (mainH > MAIN_AVAIL_H) {
            mainH = MAIN_AVAIL_H;
            mainW = mainH * ASPECT;
        }
        layout.main = CBox{(monitorPx.x - mainW) / 2.0, BAR_BOTTOM, mainW, mainH};

        return layout;
    }

    SHit hitTest(const SLayout& layout, const Vector2D& px) {
        for (size_t i = 0; i < layout.tiles.size(); ++i) {
            if (layout.tiles[i].containsPoint(px))
                return {eHit::TILE, (int)i};
        }
        if (layout.newSlot.containsPoint(px))
            return {eHit::NEW_SLOT, -1};
        if (layout.main.containsPoint(px))
            return {eHit::MAIN, -1};
        return {};
    }
}
