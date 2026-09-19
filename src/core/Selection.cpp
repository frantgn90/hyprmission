#include "Selection.hpp"

#include <algorithm>

namespace hm::core {
    int selectedIndex(const std::vector<int64_t>& ids, int64_t selected) {
        if (selected == NEW_SLOT)
            return (int)ids.size();
        const auto IT = std::ranges::find(ids, selected);
        return IT == ids.end() ? -1 : (int)(IT - ids.begin());
    }

    int64_t moveSelection(const std::vector<int64_t>& ids, int64_t selected, int delta) {
        int idx = selectedIndex(ids, selected);
        if (idx < 0)
            idx = 0;
        else
            idx = std::clamp(idx + delta, 0, (int)ids.size());
        return idx == (int)ids.size() ? NEW_SLOT : ids[idx];
    }

    int64_t reconcileSelection(const std::vector<int64_t>& ids, int64_t selected, int64_t active) {
        return selectedIndex(ids, selected) < 0 ? active : selected;
    }

    eActivation activation(int64_t target, int64_t active) {
        if (target == active)
            return eActivation::CLOSE;
        if (target == NEW_SLOT)
            return eActivation::CREATE_NEW;
        return eActivation::SWITCH;
    }
}
