#include "core/Selection.hpp"
#include "core/Workspaces.hpp"

#include <gtest/gtest.h>

using namespace hm::core;
using namespace std::chrono_literals;

TEST(NextEmptyWorkspace, FirstIdWhenNothingExists) {
    EXPECT_EQ(nextEmptyWorkspaceID({}), 1);
}

TEST(NextEmptyWorkspace, FirstMissingId) {
    EXPECT_EQ(nextEmptyWorkspaceID({{1, 2, true}, {2, 1, true}, {3, 1, true}}), 4);
}

TEST(NextEmptyWorkspace, FillsGaps) {
    EXPECT_EQ(nextEmptyWorkspaceID({{1, 2, true}, {3, 1, true}}), 2);
}

TEST(NextEmptyWorkspace, ReusesExistingEmptyWorkspaceOnThisMonitor) {
    EXPECT_EQ(nextEmptyWorkspaceID({{1, 2, true}, {2, 0, true}, {3, 1, true}}), 2);
}

TEST(NextEmptyWorkspace, SkipsEmptyWorkspaceOnAnotherMonitor) {
    EXPECT_EQ(nextEmptyWorkspaceID({{1, 2, true}, {2, 0, false}}), 3);
}

TEST(NextEmptyWorkspace, OrderOfInputDoesNotMatter) {
    EXPECT_EQ(nextEmptyWorkspaceID({{3, 1, true}, {1, 1, true}, {2, 1, true}}), 4);
}

TEST(LiveTimer, IntervalFromFps) {
    EXPECT_EQ(liveTimerInterval(true, 30), std::chrono::microseconds(33333));
    EXPECT_EQ(liveTimerInterval(true, 60), std::chrono::microseconds(16666));
    EXPECT_EQ(liveTimerInterval(true, 1), std::chrono::microseconds(1000000));
}

TEST(LiveTimer, FpsIsClamped) {
    EXPECT_EQ(liveTimerInterval(true, 0), liveTimerInterval(true, 1));
    EXPECT_EQ(liveTimerInterval(true, -5), liveTimerInterval(true, 1));
    EXPECT_EQ(liveTimerInterval(true, 1000), liveTimerInterval(true, 144));
}

TEST(LiveTimer, DisabledPollsSlowly) {
    EXPECT_EQ(liveTimerInterval(false, 30), std::chrono::microseconds(250ms));
}

TEST(StateJson, ClosedDefault) {
    EXPECT_EQ(stateToJson({}), R"({"open":false,"active":-1,"selected":-1,"workspaces":[],"dragging":false,"tiles":[],)"
                               R"("newSlot":{"x":0.0,"y":0.0,"w":0.0,"h":0.0},"main":{"x":0.0,"y":0.0,"w":0.0,"h":0.0}})");
}

TEST(StateJson, OpenWithTiles) {
    SStateSnapshot s;
    s.open       = true;
    s.active     = 2;
    s.selected   = 3;
    s.workspaces = {1, 2, 3};
    s.dragging   = true;
    s.tiles      = {{1, 2, 3, 4}, {5.5, 6, 7, 8}};
    s.newSlot    = {9, 10, 11, 12};
    s.main       = {0, 100, 200, 100};
    EXPECT_EQ(stateToJson(s), R"({"open":true,"active":2,"selected":3,"workspaces":[1,2,3],"dragging":true,)"
                              R"("tiles":[{"x":1.0,"y":2.0,"w":3.0,"h":4.0},{"x":5.5,"y":6.0,"w":7.0,"h":8.0}],)"
                              R"("newSlot":{"x":9.0,"y":10.0,"w":11.0,"h":12.0},"main":{"x":0.0,"y":100.0,"w":200.0,"h":100.0}})");
}

TEST(StateJson, NewSlotSelectionIsAString) {
    SStateSnapshot s;
    s.selected = NEW_SLOT;
    EXPECT_NE(stateToJson(s).find(R"("selected":"new")"), std::string::npos);
}
