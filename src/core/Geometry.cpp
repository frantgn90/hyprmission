#include "Geometry.hpp"

namespace hm::core {
    Vector2D globalToMonitorPx(const Vector2D& global, const Vector2D& monitorPos, double scale) {
        return (global - monitorPos) * scale;
    }

    CBox monitorPxToGlobalBox(const CBox& px, const Vector2D& monitorPos, double scale) {
        return CBox{monitorPos.x + px.x / scale, monitorPos.y + px.y / scale, px.w / scale, px.h / scale};
    }

    Vector2D mainViewToGlobal(const Vector2D& px, const CBox& mainView, const Vector2D& monitorPos, const Vector2D& monitorSize) {
        const Vector2D REL = (px - mainView.pos()) / mainView.size();
        return monitorPos + REL * monitorSize;
    }

    CBox globalBoxToMainView(const CBox& box, const CBox& mainView, const Vector2D& monitorPos, const Vector2D& monitorSize) {
        const Vector2D REL = (box.pos() - monitorPos) / monitorSize;
        const Vector2D SZ  = box.size() / monitorSize;
        return CBox{mainView.x + REL.x * mainView.w, mainView.y + REL.y * mainView.h, SZ.x * mainView.w, SZ.y * mainView.h};
    }

    bool exceedsDragThreshold(const Vector2D& startPx, const Vector2D& nowPx, double scale) {
        return startPx.distance(nowPx) > 8.0 * scale;
    }

    CBox dragGhost(const CBox& base, const Vector2D& startPx, const Vector2D& cursorPx, double maxH) {
        CBox ghost = base.copy().translate(cursorPx - startPx);
        if (maxH > 0 && ghost.h > maxH) {
            const double FIT = maxH / ghost.h;
            ghost            = CBox{cursorPx.x - ghost.w * FIT / 2.0, cursorPx.y - ghost.h * FIT / 2.0, ghost.w * FIT, ghost.h * FIT};
        }
        return ghost;
    }
}
