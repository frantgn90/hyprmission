#include "core/Geometry.hpp"

#include <gtest/gtest.h>

using namespace hm::core;

namespace {
    void expectBox(const CBox& got, const CBox& want) {
        EXPECT_NEAR(got.x, want.x, 1e-9);
        EXPECT_NEAR(got.y, want.y, 1e-9);
        EXPECT_NEAR(got.w, want.w, 1e-9);
        EXPECT_NEAR(got.h, want.h, 1e-9);
    }
}

TEST(Geometry, GlobalToMonitorPxAppliesOffsetAndScale) {
    const auto P = globalToMonitorPx({2500, 100}, {2400, 0}, 1.5);
    EXPECT_DOUBLE_EQ(P.x, 150);
    EXPECT_DOUBLE_EQ(P.y, 150);
}

TEST(Geometry, MonitorPxToGlobalBoxIsInverseOfGlobalToMonitorPx) {
    const Vector2D POS{1920, 0};
    const double   SCALE = 1.6;
    const CBox     PX{160, 320, 480, 320};

    const CBox     G = monitorPxToGlobalBox(PX, POS, SCALE);
    expectBox(G, {1920 + 100, 200, 300, 200});

    const auto BACK = globalToMonitorPx(G.pos(), POS, SCALE);
    EXPECT_NEAR(BACK.x, PX.x, 1e-9);
    EXPECT_NEAR(BACK.y, PX.y, 1e-9);
}

TEST(Geometry, MainViewCornersMapToMonitorCorners) {
    const CBox     MAIN{384, 460, 3072, 2048};
    const Vector2D POS{0, 0}, SIZE{2400, 1600};

    const auto     TL = mainViewToGlobal(MAIN.pos(), MAIN, POS, SIZE);
    EXPECT_NEAR(TL.x, 0, 1e-9);
    EXPECT_NEAR(TL.y, 0, 1e-9);

    const auto BR = mainViewToGlobal(MAIN.pos() + MAIN.size(), MAIN, POS, SIZE);
    EXPECT_NEAR(BR.x, 2400, 1e-9);
    EXPECT_NEAR(BR.y, 1600, 1e-9);

    const auto MID = mainViewToGlobal(MAIN.middle(), MAIN, POS, SIZE);
    EXPECT_NEAR(MID.x, 1200, 1e-9);
    EXPECT_NEAR(MID.y, 800, 1e-9);
}

TEST(Geometry, MainViewHonoursMonitorPosition) {
    const auto G = mainViewToGlobal({100, 100}, CBox{100, 100, 200, 100}, {1920, 50}, {1920, 1080});
    EXPECT_NEAR(G.x, 1920, 1e-9);
    EXPECT_NEAR(G.y, 50, 1e-9);
}

TEST(Geometry, WindowBoxMapsIntoMainViewAndBack) {
    const CBox     MAIN{384, 460, 3072, 2048};
    const Vector2D POS{0, 0}, SIZE{2400, 1600};

    // right half of the monitor
    const CBox WIN{1200, 0, 1200, 1600};
    const CBox IN = globalBoxToMainView(WIN, MAIN, POS, SIZE);
    expectBox(IN, {384 + 1536, 460, 1536, 2048});

    const auto BACK = mainViewToGlobal(IN.pos(), MAIN, POS, SIZE);
    EXPECT_NEAR(BACK.x, WIN.x, 1e-9);
    EXPECT_NEAR(BACK.y, WIN.y, 1e-9);
}

TEST(Geometry, DragThresholdIsEightLogicalPixels) {
    EXPECT_FALSE(exceedsDragThreshold({0, 0}, {8, 0}, 1.0));
    EXPECT_TRUE(exceedsDragThreshold({0.0, 0.0}, {8.5, 0.0}, 1.0));
    // at scale 2, 8 logical px are 16 monitor px
    EXPECT_FALSE(exceedsDragThreshold({0, 0}, {10, 10}, 2.0));
    EXPECT_TRUE(exceedsDragThreshold({0, 0}, {12, 12}, 2.0));
}

TEST(Geometry, GhostFollowsCursorWhenSmallEnough) {
    const CBox G = dragGhost({100, 100, 50, 40}, {110, 110}, {210, 160}, 200);
    expectBox(G, {200, 150, 50, 40});
}

TEST(Geometry, GhostShrinksAndCentersOnCursorWhenTooTall) {
    const CBox G = dragGhost({0, 0, 400, 300}, {10, 10}, {500, 500}, 150);
    expectBox(G, {500 - 100, 500 - 75, 200, 150});
}

TEST(Geometry, GhostNotShrunkWithoutLimit) {
    const CBox G = dragGhost({0, 0, 400, 300}, {0, 0}, {5, 5}, 0);
    expectBox(G, {5, 5, 400, 300});
}
