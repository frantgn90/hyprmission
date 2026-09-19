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
#include "core/Geometry.hpp"
#include "core/Selection.hpp"

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
    // binds and delivery to the focused client. Policy lives in CKeyGrab.
    m_keyListener = Event::bus()->m_events.input.keyboard.key.listen([this](IKeyboard::SKeyEvent e, Event::SCallbackInfo& info) {
        if (e.state != WL_KEYBOARD_KEY_STATE_PRESSED) {
            info.cancelled = m_keys.onRelease(e.keycode) == hm::core::eKeyVerdict::SWALLOW;
            return;
        }

        const auto VERDICT = m_keys.onPress(e.keycode, g_pInputManager->getModsFromAllKBs(), std::chrono::steady_clock::now(), m_open);
        if (VERDICT == hm::core::eKeyVerdict::SWALLOW) {
            info.cancelled = true;
            onKeyAction(hm::core::keyAction(e.keycode));
        }
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
    const bool LIVE = g_cfgLivePreviews && g_cfgLivePreviews->value();
    m_liveTimer->updateTimeout(hm::core::liveTimerInterval(LIVE, g_cfgLiveFps ? g_cfgLiveFps->value() : 30));
}

Vector2D COverview::cursorPx() const {
    return hm::core::globalToMonitorPx(Pointer::mgr()->position(), m_monitor->m_position, m_monitor->m_scale);
}

std::vector<int64_t> COverview::barIDs() const {
    std::vector<int64_t> ids;
    for (const auto& p : m_previews)
        ids.push_back(p.workspaceID);
    return ids;
}

void COverview::onPress() {
    const Vector2D PX  = cursorPx();
    const auto     HIT = hm::core::hitTest(m_layout, PX);

    switch (HIT.kind) {
        case hm::core::eHit::TILE: activate(m_previews[HIT.tile].workspaceID); return;
        case hm::core::eHit::NEW_SLOT: activate(hm::core::NEW_SLOT); return;
        case hm::core::eHit::NONE: return; // background: ignored on purpose
        case hm::core::eHit::MAIN: break;
    }

    const Vector2D GLOBAL = hm::core::mainViewToGlobal(PX, m_layout.main, m_monitor->m_position, m_monitor->m_size);
    const auto     WINDOW = Desktop::viewState()->hitTest().windowAt(GLOBAL, Desktop::View::RESERVED_EXTENTS | Desktop::View::INPUT_EXTENTS | Desktop::View::ALLOW_FLOATING);

    m_drag.pressed = true;
    m_drag.window  = WINDOW;
    m_drag.startPx = PX;
    m_drag.moved   = false;
    if (WINDOW)
        m_drag.ghostBase = hm::core::globalBoxToMainView(WINDOW->getWindowMainSurfaceBox(), m_layout.main, m_monitor->m_position, m_monitor->m_size);
}

void COverview::onMove() {
    if (!m_drag.pressed || !m_drag.window)
        return;

    if (!m_drag.moved && hm::core::exceedsDragThreshold(m_drag.startPx, cursorPx(), m_monitor->m_scale))
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

    const auto   HIT = hm::core::hitTest(m_layout, px);
    if (HIT.kind == hm::core::eHit::TILE)
        target = State::workspaceState()->query().id(m_previews[HIT.tile].workspaceID).run();
    else if (HIT.kind == hm::core::eHit::NEW_SLOT)
        target = nextEmptyWorkspace();

    if (!target || target == w->m_workspace)
        return;

    Config::Actions::moveToWorkspace(target, true, w);

    // immediate refresh so the new workspace tile appears right away, then a
    // second one once the move/resize animations have finished
    refresh();
    m_refreshTimer->updateTimeout(std::chrono::milliseconds(600));
}

void COverview::refresh() {
    captureAll();
    relayout();
    m_selectedID = hm::core::reconcileSelection(barIDs(), m_selectedID, m_activeID);
}

PHLWORKSPACE COverview::nextEmptyWorkspace() {
    std::vector<hm::core::SWorkspaceInfo> infos;
    for (const auto& ws : State::workspaceState()->workspacesCopy()) {
        if (ws && ws->m_id > 0)
            infos.push_back({ws->m_id, ws->getWindowCount(), ws->m_monitor.lock() == m_monitor});
    }

    const auto ID = hm::core::nextEmptyWorkspaceID(infos);
    if (const auto WS = State::workspaceState()->query().id(ID).run())
        return WS;
    return State::workspaceState()->create(ID, m_monitor->m_id, std::to_string(ID), true);
}

// Switch to a workspace and keep the overview open (main view follows).
// Choosing the workspace that is already active closes the overview.
void COverview::activate(int64_t workspaceID) {
    PHLWORKSPACE ws;
    switch (hm::core::activation(workspaceID, m_activeID)) {
        case hm::core::eActivation::CLOSE: close(); return;
        case hm::core::eActivation::CREATE_NEW: ws = nextEmptyWorkspace(); break;
        case hm::core::eActivation::SWITCH: ws = State::workspaceState()->query().id(workspaceID).run(); break;
    }
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

void COverview::onKeyAction(hm::core::eKeyAction action) {
    switch (action) {
        case hm::core::eKeyAction::CLOSE: close(); return;
        case hm::core::eKeyAction::ACTIVATE:
            if (hm::core::selectedIndex(barIDs(), m_selectedID) >= 0)
                activate(m_selectedID);
            return;
        case hm::core::eKeyAction::LEFT:
        case hm::core::eKeyAction::RIGHT:
            m_selectedID = hm::core::moveSelection(barIDs(), m_selectedID, action == hm::core::eKeyAction::RIGHT ? 1 : -1);
            g_pHyprRenderer->damageMonitor(m_monitor);
            return;
        case hm::core::eKeyAction::NONE: return;
    }
}

hm::core::SStateSnapshot COverview::snapshot() const {
    hm::core::SStateSnapshot s;
    s.open       = m_open;
    s.active     = m_activeID;
    s.selected   = m_selectedID;
    s.workspaces = barIDs();
    s.dragging   = m_drag.pressed && m_drag.moved;
    if (m_monitor) {
        const auto POS   = m_monitor->m_position;
        const auto SCALE = m_monitor->m_scale;
        for (const auto& t : m_layout.tiles)
            s.tiles.push_back(hm::core::monitorPxToGlobalBox(t, POS, SCALE));
        s.newSlot = hm::core::monitorPxToGlobalBox(m_layout.newSlot, POS, SCALE);
        s.main    = hm::core::monitorPxToGlobalBox(m_layout.main, POS, SCALE);
    }
    return s;
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

    // if a keybind is opening us, remember its combo so it can also close us
    m_keys.onOpen(std::chrono::steady_clock::now());

    if (!captureAll()) {
        HyprlandAPI::addNotification(PHANDLE, "[hyprmission] no workspaces to preview", CHyprColor{1.0, 0.6, 0.2, 1.0}, 3000);
        m_previews.clear();
        m_monitor.reset();
        return;
    }

    relayout();

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
    m_keys.onClose();
    m_drag   = {};
    m_layout = {};
    m_refreshTimer->updateTimeout(std::nullopt);
    m_liveTimer->updateTimeout(std::nullopt);
    if (m_monitor)
        g_pHyprRenderer->damageMonitor(m_monitor);
    m_monitor.reset();
}

bool COverview::captureAll() {
    // keep framebuffers from the previous capture: with live previews this
    // runs many times a second, reallocating them every time would be waste
    std::unordered_map<int64_t, SP<Render::IFramebuffer>> oldFbs;
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
        // same setup as Hyprland's own IHyprRenderer::makeSnapshotFB(). The
        // image description matters: shadows (and other color-managed passes)
        // dereference it, a framebuffer without one crashes the compositor.
        if (const auto IT = oldFbs.find(target->m_id); IT != oldFbs.end() && IT->second && IT->second->m_size == m_monitor->m_pixelSize)
            preview.fb = IT->second;
        else {
            preview.fb = g_pHyprRenderer->createFB("hyprmission-preview");
            preview.fb->alloc((int)m_monitor->m_pixelSize.x, (int)m_monitor->m_pixelSize.y, DRM_FORMAT_ABGR8888);
        }
        preview.fb->setImageDescription(m_monitor->workBufferImageDescription());

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
void COverview::relayout() {
    if (!m_monitor)
        return;
    m_layout = hm::core::computeLayout(m_monitor->m_transformedSize, m_monitor->m_scale, m_previews.size());
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

    for (size_t i = 0; i < m_previews.size() && i < m_layout.tiles.size(); ++i) {
        const auto& preview = m_previews[i];
        const CBox& BOX     = m_layout.tiles[i];
        if (!preview.fb)
            continue;

        // keyboard selection (white) sits outside the active ring (blue) so
        // both stay visible when they're on the same tile
        if (preview.workspaceID == m_selectedID) {
            CRectPassElement::SRectData sel;
            sel.box   = BOX.copy().expand(BORDER * 2.5);
            sel.color = CHyprColor{1.0, 1.0, 1.0, 0.9};
            sel.round = ROUND + (int)(BORDER * 2.5);
            pass.add(makeUnique<CRectPassElement>(sel));
        }

        if (preview.workspaceID == m_activeID) {
            activeTex = preview.fb->getTexture();

            CRectPassElement::SRectData ring;
            ring.box   = BOX.copy().expand(BORDER);
            ring.color = CHyprColor{0.45, 0.65, 1.0, 1.0};
            ring.round = ROUND + (int)BORDER;
            pass.add(makeUnique<CRectPassElement>(ring));
        } else if (preview.workspaceID == m_selectedID) {
            // punch the white back to background so it reads as a ring
            CRectPassElement::SRectData gap;
            gap.box   = BOX.copy().expand(BORDER);
            gap.color = CHyprColor{0.05, 0.05, 0.07, 1.0};
            gap.round = ROUND + (int)BORDER;
            pass.add(makeUnique<CRectPassElement>(gap));
        }

        CTexPassElement::SRenderData tex;
        tex.tex   = preview.fb->getTexture();
        tex.box   = BOX;
        tex.round = ROUND;
        pass.add(makeUnique<CTexPassElement>(std::move(tex)));
    }

    if (m_selectedID == hm::core::NEW_SLOT) {
        CRectPassElement::SRectData sel;
        sel.box   = m_layout.newSlot.copy().expand(BORDER * 2.5);
        sel.color = CHyprColor{1.0, 1.0, 1.0, 0.9};
        sel.round = ROUND + (int)(BORDER * 2.5);
        pass.add(makeUnique<CRectPassElement>(sel));

        CRectPassElement::SRectData gap;
        gap.box   = m_layout.newSlot.copy().expand(BORDER);
        gap.color = CHyprColor{0.05, 0.05, 0.07, 1.0};
        gap.round = ROUND + (int)BORDER;
        pass.add(makeUnique<CRectPassElement>(gap));
    }

    CRectPassElement::SRectData slot;
    slot.box   = m_layout.newSlot;
    slot.color = CHyprColor{1.0, 1.0, 1.0, 0.08};
    slot.round = ROUND;
    pass.add(makeUnique<CRectPassElement>(slot));

    if (m_plusTex) {
        const Vector2D TS = m_plusTex->m_size;
        CTexPassElement::SRenderData plus;
        plus.tex = m_plusTex;
        plus.box = CBox{m_layout.newSlot.x + (m_layout.newSlot.w - TS.x) / 2.f, m_layout.newSlot.y + (m_layout.newSlot.h - TS.y) / 2.f, TS.x, TS.y};
        pass.add(makeUnique<CTexPassElement>(std::move(plus)));
    }

    if (activeTex) {
        CTexPassElement::SRenderData main;
        main.tex   = activeTex;
        main.box   = m_layout.main;
        main.round = ROUND;
        pass.add(makeUnique<CTexPassElement>(std::move(main)));
    }

    if (m_drag.pressed && m_drag.moved && m_drag.window) {
        const Vector2D PX = cursorPx();

        // highlight the drop target
        CBox       dropBox;
        const auto HIT = hm::core::hitTest(m_layout, PX);
        if (HIT.kind == hm::core::eHit::TILE)
            dropBox = m_layout.tiles[HIT.tile];
        else if (HIT.kind == hm::core::eHit::NEW_SLOT)
            dropBox = m_layout.newSlot;
        if (!dropBox.empty()) {
            CRectPassElement::SRectData hl;
            hl.box   = dropBox.copy().expand(BORDER);
            hl.color = CHyprColor{1.0, 1.0, 1.0, 0.35};
            hl.round = ROUND + (int)BORDER;
            pass.add(makeUnique<CRectPassElement>(hl));
        }

        // ghost of the dragged window, shrunk so it fits a tile, following the cursor
        const CBox ghost = hm::core::dragGhost(m_drag.ghostBase, m_drag.startPx, PX, m_layout.tiles.empty() ? 0.0 : m_layout.tiles.front().h);

        CRectPassElement::SRectData g;
        g.box   = ghost;
        g.color = CHyprColor{0.45, 0.65, 1.0, 0.45};
        g.round = ROUND;
        pass.add(makeUnique<CRectPassElement>(g));
    }
}
