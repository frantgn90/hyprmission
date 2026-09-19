#pragma once

// Keyboard selection over the workspace bar. Positions 0..n-1 are the
// workspaces (by ID, left to right), position n is the "+" slot.

#include <cstdint>
#include <vector>

namespace hm::core {
    inline constexpr int64_t NEW_SLOT = -2;

    // index of `selected` in the bar, ids.size() for NEW_SLOT, -1 if gone
    int selectedIndex(const std::vector<int64_t>& ids, int64_t selected);

    // move the selection by delta positions, clamped to [first tile, "+" slot]
    int64_t moveSelection(const std::vector<int64_t>& ids, int64_t selected, int delta);

    // after the bar changed (workspace destroyed...): keep the selection if
    // it still exists, otherwise fall back to the active workspace
    int64_t reconcileSelection(const std::vector<int64_t>& ids, int64_t selected, int64_t active);

    enum class eActivation {
        CLOSE,      // chose the active workspace: close the overview
        SWITCH,     // switch to an existing workspace, stay open
        CREATE_NEW, // the "+" slot: go to a new empty workspace, stay open
    };

    eActivation activation(int64_t target, int64_t active);
}
