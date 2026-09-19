#include "core/Selection.hpp"

#include <gtest/gtest.h>

using namespace hm::core;

namespace {
    const std::vector<int64_t> BAR{1, 2, 5};
}

TEST(Selection, IndexOfWorkspaces) {
    EXPECT_EQ(selectedIndex(BAR, 1), 0);
    EXPECT_EQ(selectedIndex(BAR, 5), 2);
}

TEST(Selection, NewSlotIsPastTheLastTile) {
    EXPECT_EQ(selectedIndex(BAR, NEW_SLOT), 3);
    EXPECT_EQ(selectedIndex({}, NEW_SLOT), 0);
}

TEST(Selection, UnknownIsMinusOne) {
    EXPECT_EQ(selectedIndex(BAR, 3), -1);
    EXPECT_EQ(selectedIndex(BAR, -1), -1);
}

TEST(Selection, RightWalksToTheNewSlotAndStops) {
    int64_t sel = 1;
    sel         = moveSelection(BAR, sel, +1);
    EXPECT_EQ(sel, 2);
    sel = moveSelection(BAR, sel, +1);
    EXPECT_EQ(sel, 5);
    sel = moveSelection(BAR, sel, +1);
    EXPECT_EQ(sel, NEW_SLOT);
    sel = moveSelection(BAR, sel, +1);
    EXPECT_EQ(sel, NEW_SLOT); // clamped, no wrap
}

TEST(Selection, LeftFromNewSlotAndClampsAtFirst) {
    int64_t sel = moveSelection(BAR, NEW_SLOT, -1);
    EXPECT_EQ(sel, 5);
    sel = moveSelection(BAR, 1, -1);
    EXPECT_EQ(sel, 1);
}

TEST(Selection, LostSelectionRestartsAtFirstTile) {
    EXPECT_EQ(moveSelection(BAR, 42, +1), 1);
    EXPECT_EQ(moveSelection(BAR, 42, -1), 1);
}

TEST(Selection, EmptyBarOnlyHasNewSlot) {
    EXPECT_EQ(moveSelection({}, -1, +1), NEW_SLOT);
    EXPECT_EQ(moveSelection({}, NEW_SLOT, -1), NEW_SLOT);
}

TEST(Selection, ReconcileKeepsExistingSelection) {
    EXPECT_EQ(reconcileSelection(BAR, 5, 1), 5);
    EXPECT_EQ(reconcileSelection(BAR, NEW_SLOT, 1), NEW_SLOT);
}

TEST(Selection, ReconcileFallsBackToActiveWhenWorkspaceVanished) {
    // e.g. an empty workspace we left got destroyed
    EXPECT_EQ(reconcileSelection(BAR, 4, 2), 2);
}

TEST(Activation, ActiveWorkspaceCloses) {
    EXPECT_EQ(activation(2, 2), eActivation::CLOSE);
}

TEST(Activation, OtherWorkspaceSwitches) {
    EXPECT_EQ(activation(5, 2), eActivation::SWITCH);
}

TEST(Activation, NewSlotCreates) {
    EXPECT_EQ(activation(NEW_SLOT, 2), eActivation::CREATE_NEW);
}
