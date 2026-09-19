#include "Workspaces.hpp"
#include "Selection.hpp"

#include <algorithm>
#include <format>

namespace hm::core {
    int64_t nextEmptyWorkspaceID(const std::vector<SWorkspaceInfo>& existing) {
        for (int64_t id = 1;; ++id) {
            const auto IT = std::ranges::find(existing, id, &SWorkspaceInfo::id);
            if (IT == existing.end())
                return id;
            if (IT->windowCount == 0 && IT->onMonitor)
                return id;
        }
    }

    std::chrono::microseconds liveTimerInterval(bool liveEnabled, int64_t fps) {
        if (!liveEnabled)
            return std::chrono::milliseconds(250);
        return std::chrono::microseconds(1000000 / std::clamp<int64_t>(fps, 1, 144));
    }

    static std::string boxToJson(const Hyprutils::Math::CBox& b) {
        return std::format(R"({{"x":{:.1f},"y":{:.1f},"w":{:.1f},"h":{:.1f}}})", b.x, b.y, b.w, b.h);
    }

    std::string stateToJson(const SStateSnapshot& state) {
        std::string ws, tiles;
        for (size_t i = 0; i < state.workspaces.size(); ++i)
            ws += std::format("{}{}", i ? "," : "", state.workspaces[i]);
        for (size_t i = 0; i < state.tiles.size(); ++i)
            tiles += (i ? "," : "") + boxToJson(state.tiles[i]);

        const std::string SELECTED = state.selected == NEW_SLOT ? "\"new\"" : std::to_string(state.selected);

        return std::format(R"({{"open":{},"active":{},"selected":{},"workspaces":[{}],"dragging":{},"tiles":[{}],"newSlot":{},"main":{}}})", state.open ? "true" : "false",
                           state.active, SELECTED, ws, state.dragging ? "true" : "false", tiles, boxToJson(state.newSlot), boxToJson(state.main));
    }
}
