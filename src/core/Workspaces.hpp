#pragma once

#include <hyprutils/math/Box.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace hm::core {
    struct SWorkspaceInfo {
        int64_t id          = -1;
        int     windowCount = 0;
        bool    onMonitor   = false; // lives on the overview's monitor
    };

    // Same rule as Hyprland's `empty` workspace selector: the first ID >= 1
    // that either doesn't exist yet (caller creates it) or exists on this
    // monitor with no windows.
    int64_t nextEmptyWorkspaceID(const std::vector<SWorkspaceInfo>& existing);

    // Live preview timer period. When live previews are off we still poll
    // slowly so enabling them from the config applies without reopening.
    std::chrono::microseconds liveTimerInterval(bool liveEnabled, int64_t fps);

    struct SStateSnapshot {
        bool                 open       = false;
        int64_t              active     = -1;
        int64_t              selected   = -1; // NEW_SLOT for the "+" slot
        std::vector<int64_t> workspaces;      // bar order
        bool                 dragging   = false;

        // global logical coords (what a pointer device targets)
        std::vector<Hyprutils::Math::CBox> tiles;
        Hyprutils::Math::CBox              newSlot;
        Hyprutils::Math::CBox              main;
    };

    // JSON for `hyprctl hyprmission` (used by the integration tests)
    std::string stateToJson(const SStateSnapshot& state);
}
