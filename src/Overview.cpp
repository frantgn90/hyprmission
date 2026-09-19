// Pre-include STL headers Hyprland's includes pull in transitively - the
// private/protected macro trick below corrupts their internal class
// definitions if they get first-included from inside the guarded section.
#include <any>
#include <sstream>
#include <chrono>

// renderWorkspace() is a protected member of Render::IHyprRenderer (exported
// by the Hyprland binary - verified with `nm -D` on 0.56.2 - so compile-time
// access is enough, no function hook needed).
// See: https://wiki.hypr.land/hyprland-plugins/development/advanced/#accessing-private-members
#define private public
#define protected public
#include <hyprland/src/render/Renderer.hpp>
#undef protected
#undef private

#include "Overview.hpp"
#include "Globals.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/pointer/PointerManager.hpp>
#include <hyprland/src/desktop/state/ViewState.hpp>
#include <hyprland/src/desktop/state/ViewHitTester.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/state/WorkspaceQuery.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>

#include <linux/input-event-codes.h>

COverview::COverview() {
    // IHyprRenderer::renderMonitor(): beginRender() clears m_renderPass, then
    // the frame's elements are added, RENDER_LAST_MOMENT is emitted, and only
    // then endRender() draws the pass. Adding our elements here puts them on
    // top of everything - same way Hyprland adds its own DPMS fade overlay.
    m_renderListener = Event::bus()->m_events.render.stage.listen([this](eRenderStage stage) {
        if (stage == RENDER_LAST_MOMENT)
            addPassElements();
    });

    // While open we swallow input so clicks/keys don't reach the windows
    // hidden underneath the overlay.
    m_buttonListener = Event::bus()->m_events.input.mouse.button.listen([this](IPointer::SButtonEvent e, Event::SCallbackInfo& info) {
        if (!m_open)
            return;
        info.cancelled = true;
        if (e.button != BTN_LEFT)
            return;
        if (e.state == WL_POINTER_BUTTON_STATE_PRESSED)
            onPress();
        else
            onRelease();
    });

    m_moveListener = Event::bus()->m_events.input.mouse.move.listen([this](Vector2D, Event::SCallbackInfo&) {
        if (m_open)
            onMove();
    });

    // This event fires before keybinds are resolved; cancelling it stops both
    // binds and delivery to the focused client.
    m_keyListener = Event::bus()->m_events.input.keyboard.key.listen([this](IKeyboard::SKeyEvent e, Event::SCallbackInfo& info) {
        if (e.state != WL_KEYBOARD_KEY_STATE_PRESSED) {
            // only swallow releases whose press we swallowed: releases of keys
            // held before opening must reach the client or they'd get stuck
            if (m_swallowed.erase(e.keycode))
                info.cancelled = true;
            return;
        }

        const uint32_t MODS = g_pInputManager->getModsFromAllKBs();

        if (!m_open) {
            m_lastPress = {e.keycode, MODS, Time::steadyNow()};
            return;
        }

        if (m_toggleKey && e.keycode == m_toggleKey->first && MODS == m_toggleKey->second)
            return; // let the bind that opened us run - it toggles us closed

        info.cancelled = true;
        m_swallowed.insert(e.keycode);
        onKey(e.keycode);
    });

    m_refreshTimer = makeShared<CEventLoopTimer>(
        std::nullopt,
        [this](SP<CEventLoopTimer>, void*) {
            if (!m_open)
                return;
            refresh();
            g_pHyprRenderer->damageMonitor(m_monitor);
        },
        nullptr);
    g_pEventLoopManager->addTimer(m_refreshTimer);

    m_liveTimer = makeShared<CEventLoopTimer>(
        std::nullopt,
        [this](SP<CEventLoopTimer>, void*) {
            if (!m_open)
                return;
            if (g_cfgLivePreviews && g_cfgLivePreviews->value()) {
                refresh();
                g_pHyprRenderer->damageMonitor(m_monitor);
            }
            armLiveTimer();
        },
        nullptr);
    g_pEventLoopManager->addTimer(m_liveTimer);
}

COverview::~COverview() {
    // the timers' callbacks live in this .so - make sure the event loop drops them
    g_pEventLoopManager->removeTimer(m_refreshTimer);
    g_pEventLoopManager->removeTimer(m_liveTimer);
}

void COverview::armLiveTimer() {
    // while live previews are off, keep polling slowly so turning the option
    // on (hyprctl reload / hl.config) takes effect without reopening
    if (!g_cfgLivePreviews || !g_cfgLivePreviews->value()) {
        m_liveTimer->updateTimeout(std::chrono::milliseconds(250));
        return;
    }
    const auto FPS = std::clamp<Config::INTEGER>(g_cfgLiveFps ? g_cfgLiveFps->value() : 30, 1, 144);
    m_liveTimer->updateTimeout(std::chrono::microseconds(1000000 / FPS));
}

Vector2D COverview::cursorPx() const {
    // global logical -> monitor pixel coords
    return (Pointer::mgr()->position() - m_monitor->m_position) * m_monitor->m_scale;
}

int COverview::tileAt(const Vector2D& px) const {
    for (size_t i = 0; i < m_previews.size(); ++i) {
        if (m_previews[i].box.containsPoint(px))
            return (int)i;
    }
    return -1;
}

CBox COverview::windowBoxInMainView(PHLWINDOW w) const {
    const CBox     WIN = w->getWindowMainSurfaceBox();
    const Vector2D REL = (WIN.pos() - m_monitor->m_position) / m_monitor->m_size;
    const Vector2D SZ  = WIN.size() / m_monitor->m_size;
    return CBox{m_mainBox.x + REL.x * m_mainBox.w, m_mainBox.y + REL.y * m_mainBox.h, SZ.x * m_mainBox.w, SZ.y * m_mainBox.h};
}

void COverview::onPress() {
    const Vector2D PX   = cursorPx();
    const int      TILE = tileAt(PX);

    if (TILE >= 0) {
        activate(m_previews[TILE].workspaceID);
        return;
    }

    if (m_newSlotBox.containsPoint(PX)) {
        activate(NEW_SLOT);
        return;
    }

    if (!m_mainBox.containsPoint(PX))
        return;

    // main view -> global logical coords of the real (hidden) workspace
    const Vector2D REL    = (PX - m_mainBox.pos()) / m_mainBox.size();
    const Vector2D GLOBAL = m_monitor->m_position + REL * m_monitor->m_size;
    const auto     WINDOW = Desktop::viewState()->hitTest().windowAt(GLOBAL, Desktop::View::RESERVED_EXTENTS | Desktop::View::INPUT_EXTENTS | Desktop::View::ALLOW_FLOATING);

    m_drag.pressed = true;
    m_drag.window  = WINDOW;
    m_drag.startPx = PX;
    m_drag.moved   = false;
    if (WINDOW)
        m_drag.ghostBase = windowBoxInMainView(WINDOW);
}

void COverview::onMove() {
    if (!m_drag.pressed || !m_drag.window)
        return;

    const Vector2D PX = cursorPx();
    if (!m_drag.moved && PX.distance(m_drag.startPx) > 8 * m_monitor->m_scale)
        m_drag.moved = true;

    g_pHyprRenderer->damageMonitor(m_monitor);
}

void COverview::onRelease() {
    if (!m_drag.pressed)
        return;

    const auto WINDOW = m_drag.window.lock();
    const bool MOVED  = m_drag.moved;
    m_drag            = {};

    if (WINDOW && MOVED) {
        dropWindow(WINDOW, cursorPx());
        g_pHyprRenderer->damageMonitor(m_monitor);
        return;
    }

    // plain click on the main view: go back to the desktop, focusing the
    // clicked window if there was one
    close();
    if (WINDOW)
        Config::Actions::focus(WINDOW);
}

void COverview::dropWindow(PHLWINDOW w, const Vector2D& px) {
    PHLWORKSPACE target;

    if (const int TILE = tileAt(px); TILE >= 0)
        target = State::workspaceState()->query().id(m_previews[TILE].workspaceID).run();
    else if (m_newSlotBox.containsPoint(px))
        target = nextEmptyWorkspace();

    if (!target || target == w->m_workspace)
        return;

    Config::Actions::moveToWorkspace(target, true, w);

    // immediate refresh so the new workspace tile appears right away, then a
    // second one once the move/resize animations have finished
    refresh();
    m_refreshTimer->updateTimeout(std::chrono::milliseconds(600));
}

// re-snapshot every workspace and redo the layout, staying open
void COverview::refresh() {
    captureAll();
    layout();
    if (selectedIndex() < 0)
        m_selectedID = m_activeID;
}

int COverview::selectedIndex() const {
    if (m_selectedID == NEW_SLOT)
        return (int)m_previews.size();
    for (size_t i = 0; i < m_previews.size(); ++i) {
        if (m_previews[i].workspaceID == m_selectedID)
            return (int)i;
    }
    return -1;
}

PHLWORKSPACE COverview::nextEmptyWorkspace() {
    for (WORKSPACEID id = 1; id < INT32_MAX; ++id) {
        const auto WS = State::workspaceState()->query().id(id).run();
        if (!WS)
            return State::workspaceState()->create(id, m_monitor->m_id, std::to_string(id), true);
        if (WS->getWindowCount() == 0 && WS->m_monitor.lock() == m_monitor)
            return WS;
    }
    return nullptr;
}

// Switch to a workspace and keep the overview open (main view follows).
// Choosing the workspace that is already active closes the overview.
void COverview::activate(int64_t workspaceID) {
    if (workspaceID == m_activeID) {
        close();
        return;
    }

    PHLWORKSPACE ws = workspaceID == NEW_SLOT ? nextEmptyWorkspace() : State::workspaceState()->query().id(workspaceID).run();
    if (!ws)
        return;

    m_monitor->changeWorkspace(ws, false, true /* noMouseMove */);
    m_activeID   = ws->m_id;
    m_selectedID = ws->m_id;

    // a new workspace needs its tile; one we just left may have been empty
    // and gets destroyed by Hyprland (possibly a bit later) - re-snapshot now
    // and once more after that settles
    refresh();
    m_refreshTimer->updateTimeout(std::chrono::milliseconds(600));
    g_pHyprRenderer->damageMonitor(m_monitor);
}

void COverview::onKey(uint32_t keycode) {
    switch (keycode) {
        case KEY_ESC: close(); return;
        case KEY_ENTER:
        case KEY_KPENTER:
            if (selectedIndex() >= 0)
                activate(m_selectedID);
            return;
        case KEY_LEFT:
        case KEY_RIGHT: {
            if (m_previews.empty())
                return;
            // positions 0..size-1 are workspaces, size is the "+" slot
            int idx = selectedIndex();
            if (idx < 0)
                idx = 0;
            else
                idx = std::clamp(idx + (keycode == KEY_RIGHT ? 1 : -1), 0, (int)m_previews.size());
            m_selectedID = idx == (int)m_previews.size() ? NEW_SLOT : m_previews[idx].workspaceID;
            g_pHyprRenderer->damageMonitor(m_monitor);
            return;
        }
        default: return;
    }
}

bool COverview::isOpen() const {
    return m_open;
}

void COverview::toggle() {
    if (m_open)
        close();
    else
        open();
}

void COverview::open() {
    const auto& MONITORS = State::monitorState()->monitors();
    if (MONITORS.empty())
        return;

    m_monitor    = MONITORS.front();
    m_activeID   = m_monitor->activeWorkspaceID();
    m_selectedID = m_activeID;

    // If a keybind is opening us, our key listener saw that very press a
    // moment ago (listeners run right before binds are resolved). Remember it
    // so the same combo closes us. Opened any other way (Lua/hyprctl): none.
    m_toggleKey.reset();
    if (m_lastPress.keycode && Time::steadyNow() - m_lastPress.at < std::chrono::milliseconds(50))
        m_toggleKey = std::make_pair(m_lastPress.keycode, m_lastPress.mods);

    if (!captureAll()) {
        HyprlandAPI::addNotification(PHANDLE, "[hyprmission] no workspaces to preview", CHyprColor{1.0, 0.6, 0.2, 1.0}, 3000);
        m_previews.clear();
        m_monitor.reset();
        return;
    }

    layout();

    if (!m_plusTex)
        m_plusTex = g_pHyprRenderer->renderText("+", CHyprColor{1.0, 1.0, 1.0, 0.55}, (int)(48 * m_monitor->m_scale));

    m_open = true;
    g_pHyprRenderer->damageMonitor(m_monitor);
    armLiveTimer();
}

void COverview::close() {
    m_open = false;
    m_previews.clear();
    m_activeID   = -1;
    m_selectedID = -1;
    m_toggleKey.reset();
    m_drag = {};
    m_refreshTimer->updateTimeout(std::nullopt);
    m_liveTimer->updateTimeout(std::nullopt);
    if (m_monitor)
        g_pHyprRenderer->damageMonitor(m_monitor);
    m_monitor.reset();
}

bool COverview::captureAll() {
    // keep framebuffers from the previous capture: with live previews this
    // runs many times a second, reallocating them every time would be waste
    std::unordered_map<int64_t, SP<Render::GL::CGLFramebuffer>> oldFbs;
    for (const auto& p : m_previews)
        oldFbs[p.workspaceID] = p.fb;
    m_previews.clear();

    if (!m_monitor)
        return false;

    std::vector<PHLWORKSPACE> workspaces;
    for (const auto& ws : State::workspaceState()->workspacesCopy()) {
        if (ws && ws->m_id >= 0 && ws->m_monitor.lock() == m_monitor)
            workspaces.push_back(ws);
    }

    std::ranges::sort(workspaces, {}, [](const PHLWORKSPACE& ws) { return ws->m_id; });

    // renderWorkspaceWindows() ignores its workspace argument when picking
    // windows: it draws every window whose workspace is visible (or has
    // m_forceRendering, which is what the workspace swipe gesture uses to
    // draw neighbours). So for each capture, make the target the only
    // visible workspace, then restore everything exactly as it was.
    // Workspaces may be mid-animation (e.g. we just switched to one): keep
    // both the current value and the goal, so restoring doesn't freeze the
    // animation at its first frame (setValueAndWarp alone would).
    struct SSavedState {
        bool     visible;
        bool     forceRendering;
        Vector2D offsetValue, offsetGoal;
        float    alphaValue, alphaGoal;
    };
    std::vector<SSavedState> saved;
    for (const auto& ws : workspaces)
        saved.push_back({ws->m_visible, ws->m_forceRendering, ws->m_renderOffset->value(), ws->m_renderOffset->goal(), ws->m_alpha->value(), ws->m_alpha->goal()});

    // shouldRenderWindow() draws a workspace's windows whenever its offset or
    // alpha is animating, regardless of visibility - so a workspace sliding
    // out (we just switched away from it) would leak into every capture.
    // Freeze all animations at their current value for the capture; the
    // restore below resumes them towards their saved goal.
    for (const auto& ws : workspaces) {
        ws->m_renderOffset->setValueAndWarp(ws->m_renderOffset->value());
        ws->m_alpha->setValueAndWarp(ws->m_alpha->value());
    }

    for (const auto& target : workspaces) {
        for (const auto& ws : workspaces) {
            const bool IS_TARGET = ws == target;
            ws->m_visible        = IS_TARGET;
            ws->m_forceRendering = IS_TARGET;
            if (IS_TARGET) {
                ws->m_renderOffset->setValueAndWarp(Vector2D{});
                ws->m_alpha->setValueAndWarp(1.F);
            }
        }

        SPreview preview;
        preview.workspaceID = target->m_id;
        if (const auto IT = oldFbs.find(target->m_id); IT != oldFbs.end() && IT->second && IT->second->m_size == m_monitor->m_pixelSize)
            preview.fb = IT->second;
        else {
            preview.fb = makeShared<Render::GL::CGLFramebuffer>("hyprmission-preview");
            preview.fb->alloc((int)m_monitor->m_pixelSize.x, (int)m_monitor->m_pixelSize.y);
        }

        CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};
        if (!g_pHyprRenderer->beginFullFakeRender(m_monitor, fakeDamage, preview.fb))
            continue;

        g_pHyprRenderer->renderWorkspace(m_monitor, target, Time::steadyNow(), CBox{{0, 0}, m_monitor->m_pixelSize});

        g_pHyprRenderer->endRender();

        m_previews.push_back(preview);
    }

    for (size_t i = 0; i < workspaces.size(); ++i) {
        const auto& WS = workspaces[i];
        const auto& SV = saved[i];
        WS->m_visible        = SV.visible;
        WS->m_forceRendering = SV.forceRendering;
        WS->m_renderOffset->setValueAndWarp(SV.offsetValue);
        if (SV.offsetGoal != SV.offsetValue)
            *WS->m_renderOffset = SV.offsetGoal; // resume the animation
        WS->m_alpha->setValueAndWarp(SV.alphaValue);
        if (SV.alphaGoal != SV.alphaValue)
            *WS->m_alpha = SV.alphaGoal;
    }

    // Wayland clients only draw when they get frame callbacks, and Hyprland
    // only sends those to visible workspaces - without this, apps on hidden
    // workspaces would stay frozen in the live previews.
    if (g_cfgLivePreviews && g_cfgLivePreviews->value()) {
        const auto NOW = Time::steadyNow();
        for (size_t i = 0; i < workspaces.size(); ++i) {
            if (!saved[i].visible)
                g_pHyprRenderer->sendFrameEventsToWorkspace(m_monitor, workspaces[i], NOW);
        }
    }

    return !m_previews.empty();
}

// Boxes are in monitor pixel coordinates: that's what pass elements expect
// (CRectPassElement::boundingBox() divides by the monitor scale).
void COverview::layout() {
    if (m_previews.empty() || !m_monitor)
        return;

    const Vector2D SIZE   = m_monitor->m_transformedSize;
    const float    SCALE  = m_monitor->m_scale;
    const float    MARGIN = 32.f * SCALE;
    const float    GAP    = 24.f * SCALE;
    const float    ASPECT = SIZE.x / SIZE.y;
    const int      SLOTS  = (int)m_previews.size() + 1; // + "new workspace" slot

    float tileH = SIZE.y * 0.14f;
    float tileW = tileH * ASPECT;

    const float availableW = SIZE.x - 2 * MARGIN;
    float       totalW     = SLOTS * tileW + (SLOTS - 1) * GAP;
    if (totalW > availableW) {
        const float FIT = availableW / totalW;
        tileW *= FIT;
        tileH *= FIT;
        totalW = SLOTS * tileW + (SLOTS - 1) * GAP;
    }

    float       x = (SIZE.x - totalW) / 2.f;
    const float y = MARGIN;

    for (auto& preview : m_previews) {
        preview.box = CBox{x, y, tileW, tileH};
        x += tileW + GAP;
    }
    m_newSlotBox = CBox{x, y, tileW, tileH};

    // main area: everything below the bar, active workspace keeps its aspect
    const float barBottom = y + tileH + MARGIN;
    const float mainAvailH = SIZE.y - barBottom - MARGIN;
    const float mainAvailW = SIZE.x - 2 * MARGIN;
    float       mainW      = mainAvailW;
    float       mainH      = mainW / ASPECT;
    if (mainH > mainAvailH) {
        mainH = mainAvailH;
        mainW = mainH * ASPECT;
    }
    m_mainBox = CBox{(SIZE.x - mainW) / 2.f, barBottom, mainW, mainH};
}

void COverview::addPassElements() {
    if (!m_open || !m_monitor)
        return;

    if (g_pHyprRenderer->m_renderData.pMonitor.lock() != m_monitor)
        return;

    auto& pass = g_pHyprRenderer->m_renderPass;

    const float SCALE  = m_monitor->m_scale;
    const int   ROUND  = (int)(8 * SCALE);
    const float BORDER = 3.f * SCALE;

    CRectPassElement::SRectData bg;
    bg.box   = {{0, 0}, m_monitor->m_transformedSize};
    bg.color = CHyprColor{0.05, 0.05, 0.07, 0.94};
    pass.add(makeUnique<CRectPassElement>(bg));

    SP<Render::ITexture> activeTex;

    for (const auto& preview : m_previews) {
        if (!preview.fb)
            continue;

        // keyboard selection (white) sits outside the active ring (blue) so
        // both stay visible when they're on the same tile
        if (preview.workspaceID == m_selectedID) {
            CRectPassElement::SRectData sel;
            sel.box   = preview.box.copy().expand(BORDER * 2.5);
            sel.color = CHyprColor{1.0, 1.0, 1.0, 0.9};
            sel.round = ROUND + (int)(BORDER * 2.5);
            pass.add(makeUnique<CRectPassElement>(sel));
        }

        if (preview.workspaceID == m_activeID) {
            activeTex = preview.fb->getTexture();

            CRectPassElement::SRectData ring;
            ring.box   = preview.box.copy().expand(BORDER);
            ring.color = CHyprColor{0.45, 0.65, 1.0, 1.0};
            ring.round = ROUND + (int)BORDER;
            pass.add(makeUnique<CRectPassElement>(ring));
        } else if (preview.workspaceID == m_selectedID) {
            // punch the white back to background so it reads as a ring
            CRectPassElement::SRectData gap;
            gap.box   = preview.box.copy().expand(BORDER);
            gap.color = CHyprColor{0.05, 0.05, 0.07, 1.0};
            gap.round = ROUND + (int)BORDER;
            pass.add(makeUnique<CRectPassElement>(gap));
        }

        CTexPassElement::SRenderData tex;
        tex.tex   = preview.fb->getTexture();
        tex.box   = preview.box;
        tex.round = ROUND;
        pass.add(makeUnique<CTexPassElement>(std::move(tex)));
    }

    if (m_selectedID == NEW_SLOT) {
        CRectPassElement::SRectData sel;
        sel.box   = m_newSlotBox.copy().expand(BORDER * 2.5);
        sel.color = CHyprColor{1.0, 1.0, 1.0, 0.9};
        sel.round = ROUND + (int)(BORDER * 2.5);
        pass.add(makeUnique<CRectPassElement>(sel));

        CRectPassElement::SRectData gap;
        gap.box   = m_newSlotBox.copy().expand(BORDER);
        gap.color = CHyprColor{0.05, 0.05, 0.07, 1.0};
        gap.round = ROUND + (int)BORDER;
        pass.add(makeUnique<CRectPassElement>(gap));
    }

    CRectPassElement::SRectData slot;
    slot.box   = m_newSlotBox;
    slot.color = CHyprColor{1.0, 1.0, 1.0, 0.08};
    slot.round = ROUND;
    pass.add(makeUnique<CRectPassElement>(slot));

    if (m_plusTex) {
        const Vector2D TS = m_plusTex->m_size;
        CTexPassElement::SRenderData plus;
        plus.tex = m_plusTex;
        plus.box = CBox{m_newSlotBox.x + (m_newSlotBox.w - TS.x) / 2.f, m_newSlotBox.y + (m_newSlotBox.h - TS.y) / 2.f, TS.x, TS.y};
        pass.add(makeUnique<CTexPassElement>(std::move(plus)));
    }

    if (activeTex) {
        CTexPassElement::SRenderData main;
        main.tex   = activeTex;
        main.box   = m_mainBox;
        main.round = ROUND;
        pass.add(makeUnique<CTexPassElement>(std::move(main)));
    }

    if (m_drag.pressed && m_drag.moved && m_drag.window) {
        const Vector2D PX = cursorPx();

        // highlight the drop target
        CBox dropBox;
        if (const int TILE = tileAt(PX); TILE >= 0)
            dropBox = m_previews[TILE].box;
        else if (m_newSlotBox.containsPoint(PX))
            dropBox = m_newSlotBox;
        if (!dropBox.empty()) {
            CRectPassElement::SRectData hl;
            hl.box   = dropBox.copy().expand(BORDER);
            hl.color = CHyprColor{1.0, 1.0, 1.0, 0.35};
            hl.round = ROUND + (int)BORDER;
            pass.add(makeUnique<CRectPassElement>(hl));
        }

        // ghost of the dragged window, shrunk so it fits a tile, following the cursor
        CBox ghost = m_drag.ghostBase.copy().translate(PX - m_drag.startPx);
        if (!m_previews.empty() && ghost.h > m_previews.front().box.h) {
            const double FIT = m_previews.front().box.h / ghost.h;
            ghost            = CBox{PX.x - ghost.w * FIT / 2.0, PX.y - ghost.h * FIT / 2.0, ghost.w * FIT, ghost.h * FIT};
        }

        CRectPassElement::SRectData g;
        g.box   = ghost;
        g.color = CHyprColor{0.45, 0.65, 1.0, 0.45};
        g.round = ROUND;
        pass.add(makeUnique<CRectPassElement>(g));
    }
}
