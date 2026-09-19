#pragma once

#include "core/Keyboard.hpp"
#include "core/Layout.hpp"
#include "core/Workspaces.hpp"

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>

#include <memory>
#include <vector>

// Compositor glue: turns Hyprland events into calls to the pure logic in
// core/, captures workspace previews and draws the overlay.
//  - top bar: centered row of workspace previews, active one highlighted,
//    plus a grayed "new workspace" slot at the right end
//  - below: the active workspace, scaled to fit the remaining height
class COverview {
  public:
    COverview();
    ~COverview();

    void                 toggle();
    bool                 isOpen() const;

    hm::core::SStateSnapshot snapshot() const;

  private:
    struct SPreview {
        int64_t                        workspaceID = -1;
        SP<Render::IFramebuffer>       fb;
    };

    void                 open();
    void                 close();
    void                 refresh(); // re-capture + re-layout, staying open
    bool                 captureAll();
    void                 relayout();
    void                 addPassElements();

    void                 onPress();
    void                 onRelease();
    void                 onMove();
    void                 onKeyAction(hm::core::eKeyAction action);
    void                 activate(int64_t workspaceID);
    void                 dropWindow(PHLWINDOW w, const Vector2D& px);
    PHLWORKSPACE         nextEmptyWorkspace();
    void                 armLiveTimer();

    Vector2D             cursorPx() const;
    std::vector<int64_t> barIDs() const;

    bool                  m_open = false;
    PHLMONITOR            m_monitor;
    int64_t               m_activeID   = -1;
    int64_t               m_selectedID = -1; // keyboard selection, arrows move it
    std::vector<SPreview> m_previews;
    hm::core::SLayout     m_layout;
    SP<Render::ITexture>  m_plusTex;
    hm::core::CKeyGrab    m_keys;

    struct {
        bool         pressed = false; // left button down on the main view
        PHLWINDOWREF window;
        Vector2D     startPx;
        CBox         ghostBase;
        bool         moved = false;
    } m_drag;

    CHyprSignalListener   m_renderListener;
    CHyprSignalListener   m_buttonListener;
    CHyprSignalListener   m_moveListener;
    CHyprSignalListener   m_keyListener;

    // re-snapshot after a change once Hyprland's animations settle
    SP<CEventLoopTimer>   m_refreshTimer;
    // live previews: re-capture at plugin:hyprmission:live_fps
    SP<CEventLoopTimer>   m_liveTimer;
};

inline std::unique_ptr<COverview> g_pOverview;
