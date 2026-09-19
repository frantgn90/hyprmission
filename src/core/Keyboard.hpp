#pragma once

// Keyboard grab while the overview is open, and the keys it reacts to.

#include <chrono>
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <utility>

namespace hm::core {
    enum class eKeyAction {
        NONE,
        CLOSE,    // Esc
        ACTIVATE, // Enter / keypad Enter
        LEFT,
        RIGHT,
    };

    // evdev keycode -> what the overview does with it
    eKeyAction keyAction(uint32_t keycode);

    enum class eKeyVerdict {
        PASS,    // let it through (binds / focused client)
        SWALLOW, // cancel it, and handle it as overview input
    };

    // Decides which key events the overview swallows.
    //  - closed: everything passes, but presses are remembered so that, if a
    //    keybind opens the overview, we know which combo did it
    //  - open: every press is swallowed except that toggle combo, so the same
    //    bind can close the overview
    //  - releases are swallowed only if their press was; a key held down
    //    before opening must get its release or the client sees it stuck
    class CKeyGrab {
      public:
        using clock = std::chrono::steady_clock;

        // a press this close to open() is the one that triggered it
        static constexpr auto TOGGLE_WINDOW = std::chrono::milliseconds(50);

        eKeyVerdict onPress(uint32_t keycode, uint32_t mods, clock::time_point now, bool open);
        eKeyVerdict onRelease(uint32_t keycode);

        void        onOpen(clock::time_point now);
        void        onClose();

        std::optional<std::pair<uint32_t, uint32_t>> toggleKey() const;

      private:
        struct {
            uint32_t          keycode = 0;
            uint32_t          mods    = 0;
            clock::time_point at;
        } m_lastPress;

        std::optional<std::pair<uint32_t, uint32_t>> m_toggleKey;
        std::unordered_set<uint32_t>                 m_swallowed;
    };
}
