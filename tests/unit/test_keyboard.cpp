#include "core/Keyboard.hpp"

#include <gtest/gtest.h>
#include <linux/input-event-codes.h>

using namespace hm::core;
using namespace std::chrono_literals;

namespace {
    constexpr uint32_t SUPER = 64; // xkb Mod4 mask
    const auto         T0    = CKeyGrab::clock::time_point{} + 10s;

    // simulate a keybind opening the overview: the press arrives, then open()
    CKeyGrab openedByBind(uint32_t key, uint32_t mods) {
        CKeyGrab g;
        EXPECT_EQ(g.onPress(key, mods, T0, false), eKeyVerdict::PASS);
        g.onOpen(T0 + 1ms);
        return g;
    }
}

TEST(KeyAction, MapsOverviewKeys) {
    EXPECT_EQ(keyAction(KEY_ESC), eKeyAction::CLOSE);
    EXPECT_EQ(keyAction(KEY_ENTER), eKeyAction::ACTIVATE);
    EXPECT_EQ(keyAction(KEY_KPENTER), eKeyAction::ACTIVATE);
    EXPECT_EQ(keyAction(KEY_LEFT), eKeyAction::LEFT);
    EXPECT_EQ(keyAction(KEY_RIGHT), eKeyAction::RIGHT);
}

TEST(KeyAction, EverythingElseDoesNothing) {
    EXPECT_EQ(keyAction(KEY_A), eKeyAction::NONE);
    EXPECT_EQ(keyAction(KEY_UP), eKeyAction::NONE);
    EXPECT_EQ(keyAction(KEY_TAB), eKeyAction::NONE);
}

TEST(KeyGrab, ClosedPassesEverything) {
    CKeyGrab g;
    EXPECT_EQ(g.onPress(KEY_A, 0, T0, false), eKeyVerdict::PASS);
    EXPECT_EQ(g.onRelease(KEY_A), eKeyVerdict::PASS);
}

TEST(KeyGrab, OpenSwallowsPressesAndTheirReleases) {
    CKeyGrab g;
    g.onOpen(T0);
    EXPECT_EQ(g.onPress(KEY_A, 0, T0, true), eKeyVerdict::SWALLOW);
    EXPECT_EQ(g.onRelease(KEY_A), eKeyVerdict::SWALLOW);
    // only once
    EXPECT_EQ(g.onRelease(KEY_A), eKeyVerdict::PASS);
}

TEST(KeyGrab, ReleaseOfKeyHeldBeforeOpeningPasses) {
    // otherwise the focused client would see the key stuck down
    CKeyGrab g;
    g.onPress(KEY_W, 0, T0, false);
    g.onOpen(T0 + 1s);
    EXPECT_EQ(g.onRelease(KEY_W), eKeyVerdict::PASS);
}

TEST(KeyGrab, ReleaseAfterClosingStillSwallowed) {
    CKeyGrab g;
    g.onOpen(T0);
    g.onPress(KEY_ENTER, 0, T0, true); // e.g. Enter closes the overview
    g.onClose();
    EXPECT_EQ(g.onRelease(KEY_ENTER), eKeyVerdict::SWALLOW);
}

TEST(KeyGrab, ToggleComboPassesWhileOpen) {
    auto g = openedByBind(KEY_F12, SUPER);
    ASSERT_TRUE(g.toggleKey().has_value());
    EXPECT_EQ(g.toggleKey()->first, (uint32_t)KEY_F12);
    EXPECT_EQ(g.toggleKey()->second, SUPER);
    EXPECT_EQ(g.onPress(KEY_F12, SUPER, T0 + 1s, true), eKeyVerdict::PASS);
}

TEST(KeyGrab, SameKeyWithDifferentModsIsSwallowed) {
    auto g = openedByBind(KEY_F12, SUPER);
    EXPECT_EQ(g.onPress(KEY_F12, 0, T0 + 1s, true), eKeyVerdict::SWALLOW);
    EXPECT_EQ(g.onPress(KEY_F12, SUPER | 1, T0 + 1s, true), eKeyVerdict::SWALLOW);
}

TEST(KeyGrab, NoToggleComboWhenOpenedWithoutAKey) {
    // e.g. opened with hyprctl a while after the last key press
    CKeyGrab g;
    g.onPress(KEY_ENTER, 0, T0, false);
    g.onOpen(T0 + CKeyGrab::TOGGLE_WINDOW + 1ms);
    EXPECT_FALSE(g.toggleKey().has_value());
    EXPECT_EQ(g.onPress(KEY_ENTER, 0, T0 + 1s, true), eKeyVerdict::SWALLOW);
}

TEST(KeyGrab, NoToggleComboWithoutAnyPress) {
    CKeyGrab g;
    g.onOpen(T0);
    EXPECT_FALSE(g.toggleKey().has_value());
}

TEST(KeyGrab, CloseForgetsToggleCombo) {
    auto g = openedByBind(KEY_F12, SUPER);
    g.onClose();
    EXPECT_FALSE(g.toggleKey().has_value());
}

TEST(KeyGrab, ReopeningPicksTheNewCombo) {
    auto g = openedByBind(KEY_F12, SUPER);
    g.onClose();
    g.onPress(KEY_TAB, SUPER, T0 + 5s, false);
    g.onOpen(T0 + 5s + 1ms);
    ASSERT_TRUE(g.toggleKey().has_value());
    EXPECT_EQ(g.toggleKey()->first, (uint32_t)KEY_TAB);
    EXPECT_EQ(g.onPress(KEY_F12, SUPER, T0 + 6s, true), eKeyVerdict::SWALLOW);
}
