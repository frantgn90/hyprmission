#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>

#include <hyprland/src/helpers/time/Time.hpp>

#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

// Snapshot-on-open overview, single monitor:
//  - top bar: centered row of workspace previews, active one highlighted,
//    plus a grayed "new workspace" slot at the right end
//  - below: the active workspace, scaled to fit the remaining height
// No interaction yet (drag/drop, create-on-drop, exploded windows).
class COverview {
  public:
    COverview();
    ~COverview();

    void toggle();
    bool isOpen() const;

  private:
    struct SPreview {
        int64_t                        workspaceID = -1;
        SP<Render::GL::CGLFramebuffer> fb;
        CBox                            box; // monitor pixel coords
    };

    void     addPassElements();
    void     onPress();
    void     onRelease();
    void     onMove();
    Vector2D cursorPx() const; // cursor in monitor pixel coords
    int      tileAt(const Vector2D& px) const; // index into m_previews, -1 if none
    CBox     windowBoxInMainView(PHLWINDOW w) const;
    void     dropWindow(PHLWINDOW w, const Vector2D& px);
    void     refresh();
    void     onKey(uint32_t keycode);
    void     activate(int64_t workspaceID); // switch to it, or close if already active
    int      selectedIndex() const;         // m_previews.size() == the "+" slot

    // same rule as Hyprland's `empty` workspace selector: first ID >= 1 that
    // doesn't exist or has no windows. Created if needed.
    PHLWORKSPACE nextEmptyWorkspace();

    static constexpr int64_t NEW_SLOT = -2; // m_selectedID value for the "+" slot

    void open();
    void close();
    bool captureAll();
    void layout();

    bool                  m_open = false;
    PHLMONITOR            m_monitor;
    int64_t               m_activeID   = -1;
    int64_t               m_selectedID = -1; // keyboard selection, arrows move it

    // keyboard grab: every key press while open is swallowed (and its release
    // later), except the combo that opened us, so the toggle bind can close.
    struct {
        uint32_t          keycode = 0;
        uint32_t          mods    = 0;
        Time::steady_tp   at;
    } m_lastPress;
    std::optional<std::pair<uint32_t, uint32_t>> m_toggleKey;
    std::unordered_set<uint32_t>                 m_swallowed;
    std::vector<SPreview> m_previews;
    CBox                  m_newSlotBox;
    CBox                  m_mainBox;
    SP<Render::ITexture>  m_plusTex;
    CHyprSignalListener   m_renderListener;
    CHyprSignalListener   m_buttonListener;
    CHyprSignalListener   m_moveListener;
    CHyprSignalListener   m_keyListener;

    // re-snapshot after a drop once Hyprland's move/resize animations settle
    SP<CEventLoopTimer> m_refreshTimer;

    // live previews: re-capture every workspace at plugin:hyprmission:live_fps
    SP<CEventLoopTimer> m_liveTimer;
    void                armLiveTimer();

    struct {
        bool         pressed = false; // left button down on the main view
        PHLWINDOWREF window;
        Vector2D     startPx;
        CBox         ghostBase;
        bool         moved = false;
    } m_drag;
};

inline std::unique_ptr<COverview> g_pOverview;
