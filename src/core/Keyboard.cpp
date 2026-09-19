#include "Keyboard.hpp"

#include <linux/input-event-codes.h>

namespace hm::core {
    eKeyAction keyAction(uint32_t keycode) {
        switch (keycode) {
            case KEY_ESC: return eKeyAction::CLOSE;
            case KEY_ENTER:
            case KEY_KPENTER: return eKeyAction::ACTIVATE;
            case KEY_LEFT: return eKeyAction::LEFT;
            case KEY_RIGHT: return eKeyAction::RIGHT;
            default: return eKeyAction::NONE;
        }
    }

    eKeyVerdict CKeyGrab::onPress(uint32_t keycode, uint32_t mods, clock::time_point now, bool open) {
        if (!open) {
            m_lastPress = {keycode, mods, now};
            return eKeyVerdict::PASS;
        }

        if (m_toggleKey && m_toggleKey->first == keycode && m_toggleKey->second == mods)
            return eKeyVerdict::PASS;

        m_swallowed.insert(keycode);
        return eKeyVerdict::SWALLOW;
    }

    eKeyVerdict CKeyGrab::onRelease(uint32_t keycode) {
        return m_swallowed.erase(keycode) ? eKeyVerdict::SWALLOW : eKeyVerdict::PASS;
    }

    void CKeyGrab::onOpen(clock::time_point now) {
        m_toggleKey.reset();
        if (m_lastPress.keycode && now - m_lastPress.at < TOGGLE_WINDOW)
            m_toggleKey = std::make_pair(m_lastPress.keycode, m_lastPress.mods);
    }

    void CKeyGrab::onClose() {
        // keep m_swallowed: releases of keys pressed while open still arrive
        // after closing and must stay swallowed
        m_toggleKey.reset();
    }

    std::optional<std::pair<uint32_t, uint32_t>> CKeyGrab::toggleKey() const {
        return m_toggleKey;
    }
}
