#include "core/Layout.hpp"

#include <gtest/gtest.h>

using namespace hm::core;

namespace {
    // the author's monitor: 3840x2560 px at scale 1.6
    const Vector2D MONITOR{3840, 2560};
    constexpr double SCALE = 1.6;

    bool inside(const CBox& inner, const CBox& outer) {
        return inner.x >= outer.x - 1e-6 && inner.y >= outer.y - 1e-6 && inner.x + inner.w <= outer.x + outer.w + 1e-6 && inner.y + inner.h <= outer.y + outer.h + 1e-6;
    }
}

TEST(Layout, OneTilePerWorkspacePlusNewSlot) {
    const auto L = computeLayout(MONITOR, SCALE, 3);
    ASSERT_EQ(L.tiles.size(), 3u);
    EXPECT_FALSE(L.newSlot.empty());
    EXPECT_FALSE(L.main.empty());
}

TEST(Layout, ZeroWorkspacesStillHasNewSlotAndMain) {
    const auto L = computeLayout(MONITOR, SCALE, 0);
    EXPECT_TRUE(L.tiles.empty());
    EXPECT_FALSE(L.newSlot.empty());
    EXPECT_FALSE(L.main.empty());
}

TEST(Layout, InvalidMonitorGivesEmptyLayout) {
    const auto L = computeLayout({0, 0}, 1.0, 3);
    EXPECT_TRUE(L.tiles.empty());
    EXPECT_TRUE(L.newSlot.empty());
    EXPECT_TRUE(L.main.empty());
}

TEST(Layout, RowIsCenteredHorizontally) {
    for (size_t n : {0u, 1u, 2u, 5u}) {
        const auto   L     = computeLayout(MONITOR, SCALE, n);
        const double LEFT  = n ? L.tiles.front().x : L.newSlot.x;
        const double RIGHT = L.newSlot.x + L.newSlot.w;
        EXPECT_NEAR(LEFT, MONITOR.x - RIGHT, 1e-6) << "n=" << n;
    }
}

TEST(Layout, TilesAreOrderedEqualAndNonOverlapping) {
    const auto L = computeLayout(MONITOR, SCALE, 4);
    for (size_t i = 1; i < L.tiles.size(); ++i) {
        EXPECT_GT(L.tiles[i].x, L.tiles[i - 1].x + L.tiles[i - 1].w);
        EXPECT_DOUBLE_EQ(L.tiles[i].w, L.tiles[0].w);
        EXPECT_DOUBLE_EQ(L.tiles[i].h, L.tiles[0].h);
        EXPECT_DOUBLE_EQ(L.tiles[i].y, L.tiles[0].y);
    }
    EXPECT_GT(L.newSlot.x, L.tiles.back().x + L.tiles.back().w);
    EXPECT_DOUBLE_EQ(L.newSlot.w, L.tiles[0].w);
}

TEST(Layout, TilesKeepMonitorAspectRatio) {
    const auto L = computeLayout(MONITOR, SCALE, 3);
    EXPECT_NEAR(L.tiles[0].w / L.tiles[0].h, MONITOR.x / MONITOR.y, 1e-9);
    EXPECT_NEAR(L.main.w / L.main.h, MONITOR.x / MONITOR.y, 1e-9);
}

TEST(Layout, ManyWorkspacesStillFitOnScreen) {
    // regression: the fit factor used to ignore the gaps, pushing the row off screen
    for (size_t n : {10u, 20u, 50u}) {
        const auto L = computeLayout(MONITOR, SCALE, n);
        EXPECT_GE(L.tiles.front().x, 32 * SCALE - 1e-6) << "n=" << n;
        EXPECT_LE(L.newSlot.x + L.newSlot.w, MONITOR.x - 32 * SCALE + 1e-6) << "n=" << n;
        EXPECT_GT(L.tiles.front().w, 0) << "n=" << n;
    }
}

TEST(Layout, FewWorkspacesUseDefaultTileHeight) {
    const auto L = computeLayout(MONITOR, SCALE, 2);
    EXPECT_NEAR(L.tiles[0].h, MONITOR.y * 0.14, 1e-9);
}

TEST(Layout, MainViewSitsBelowTheBarInsideTheMonitor) {
    const auto L = computeLayout(MONITOR, SCALE, 3);
    EXPECT_GT(L.main.y, L.tiles[0].y + L.tiles[0].h);
    EXPECT_TRUE(inside(L.main, CBox{0, 0, MONITOR.x, MONITOR.y}));
    EXPECT_NEAR(L.main.x, MONITOR.x - (L.main.x + L.main.w), 1e-6); // centered
}

TEST(Layout, MainViewLimitedByHeightOnUsualMonitors) {
    // the bar eats vertical space, so on landscape and ordinary portrait
    // monitors the main view is as tall as it can be
    for (const Vector2D& MON : {Vector2D{3840, 2560}, Vector2D{1920, 1080}, Vector2D{1080, 1920}}) {
        const auto L = computeLayout(MON, 1.0, 2);
        EXPECT_NEAR(L.main.y + L.main.h, MON.y - 32, 1e-6);
        EXPECT_LE(L.main.w, MON.x - 2 * 32 + 1e-6);
    }
}

TEST(Layout, MainViewLimitedByWidthOnVeryNarrowMonitors) {
    const Vector2D NARROW{300, 3000};
    const auto     L = computeLayout(NARROW, 1.0, 2);
    EXPECT_NEAR(L.main.w, NARROW.x - 2 * 32, 1e-6);
    EXPECT_LT(L.main.y + L.main.h, NARROW.y - 32);
    EXPECT_TRUE(inside(L.main, CBox{0, 0, NARROW.x, NARROW.y}));
}

TEST(Layout, ScaleOnlyAffectsMarginsAndGaps) {
    const auto A = computeLayout({1920, 1080}, 1.0, 2);
    const auto B = computeLayout({1920, 1080}, 2.0, 2);
    EXPECT_DOUBLE_EQ(A.tiles[0].h, B.tiles[0].h);
    EXPECT_DOUBLE_EQ(A.tiles[0].y, 32.0);
    EXPECT_DOUBLE_EQ(B.tiles[0].y, 64.0);
}

TEST(HitTest, FindsEachRegion) {
    const auto L = computeLayout(MONITOR, SCALE, 3);

    for (int i = 0; i < 3; ++i) {
        const auto H = hitTest(L, L.tiles[i].middle());
        EXPECT_EQ(H.kind, eHit::TILE);
        EXPECT_EQ(H.tile, i);
    }
    EXPECT_EQ(hitTest(L, L.newSlot.middle()).kind, eHit::NEW_SLOT);
    EXPECT_EQ(hitTest(L, L.main.middle()).kind, eHit::MAIN);
}

TEST(HitTest, GapsAndBackgroundHitNothing) {
    const auto L = computeLayout(MONITOR, SCALE, 3);

    const Vector2D GAP{L.tiles[0].x + L.tiles[0].w + 1, L.tiles[0].y + 10};
    EXPECT_EQ(hitTest(L, GAP).kind, eHit::NONE);
    EXPECT_EQ(hitTest(L, {1, 1}).kind, eHit::NONE);
    EXPECT_EQ(hitTest(L, {1.0, MONITOR.y - 1}).kind, eHit::NONE); // left of the main view
    EXPECT_EQ(hitTest(L, {-100, -100}).kind, eHit::NONE);
    EXPECT_EQ(hitTest(L, {1, 1}).tile, -1);
}
