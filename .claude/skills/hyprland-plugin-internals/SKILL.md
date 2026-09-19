---
name: hyprland-plugin-internals
description: How Hyprland 0.56.x internals actually behave for a plugin that draws overlays, snapshots workspaces, grabs input, registers config/hyprctl/Lua entry points and touches workspace state. Use before changing src/Overview.cpp or src/main.cpp, when adding any new interaction with Hyprland, or when something doesn't render, crashes or freezes.
---

# Hyprland plugin internals (0.56.2)

Everything here was verified against the 0.56.2 headers, the exported symbols, and Hyprland's source at `v0.56.2`. The wiki only covers `PLUGIN_INIT`/`PLUGIN_EXIT`, function hooks and config. **For anything else, read the source:**

```sh
git clone --depth 1 --branch v0.56.2 https://github.com/hyprwm/Hyprland /tmp/hyprland-src
grep -rn "<thing>" /tmp/hyprland-src/src
nm -D /usr/bin/Hyprland | grep <mangled-or-plain-name>   # is it exported (T) ?
```

## Plugin entry points (src/main.cpp)

- `PLUGIN_INIT` must compare `__hyprland_api_get_hash()` with `__hyprland_api_get_client_hash()` and throw on a mismatch.
- Config values can only be registered in `PLUGIN_INIT`. The deprecated `addConfigValue` is replaced by:
  ```cpp
  auto v = makeShared<Config::Values::CBoolValue>("plugin:hyprmission:live_previews", "desc", true);
  HyprlandAPI::addConfigValueV2(PHANDLE, v);   // registers + commence()
  v->value();                                  // always current, also after `hyprctl reload`
  ```
  `CIntValue` takes `SIntValueOptions{.min, .max}`. Users set values in Lua with `hl.config({ plugin = { hyprmission = { ... } } })`, or in a classic `plugin { hyprmission { ... } }` block. Plugins don't use a separate config file.
- **Omarchy binding quirk.** `o.bind(keys, desc, "string")` always wraps the string in `hl.dsp.exec_cmd`, so it runs a shell command, not a dispatcher. `hyprctl dispatch X` evaluates `hl.dispatch(X)` as Lua, which only accepts `hl.dsp.*` objects. **Plugin dispatchers can't be reached either way.** Expose a Lua function instead:
  ```cpp
  HyprlandAPI::addLuaFunction(PHANDLE, "hyprmission", "toggle", luaFn);   // hl.plugin.hyprmission.toggle()
  ```
  ```lua
  hl.bind("SUPER + F12", function() hl.plugin.hyprmission.toggle() end, { description = "..." })
  ```
  Also keep `addDispatcherV2` for classic `bind =` configs. `hyprctl binds` shows a Lua bind as `dispatcher: __lua`, which is normal.
- hyprctl command: `registerHyprCtlCommand(PHANDLE, SHyprCtlCommand{.name, .exact, .fn})`. Keep the returned `SP` and unregister it in `PLUGIN_EXIT`.

## Drawing an overlay

`IHyprRenderer::renderMonitor()` (`src/render/Renderer.cpp`) works in this order:

1. `render.pre` event fires.
2. `beginRender()` calls **`m_renderPass.clear()`**.
3. Windows, layers and so on are added to the pass.
4. `render.stage(RENDER_LAST_MOMENT)` fires.
5. `endRender()` **draws the pass**.

So:
- Anything added at `render.pre` is wiped by the clear.
- Drawing directly with `g_pHyprOpenGL->renderRect/renderTexture` at `RENDER_LAST_MOMENT` gets painted over by `endRender`.
- The right way is to listen to `Event::bus()->m_events.render.stage`. On `RENDER_LAST_MOMENT`, add built-in `CRectPassElement`/`CTexPassElement` to `g_pHyprRenderer->m_renderPass`, the same way Hyprland adds its DPMS fade. There's no need for a custom `IPassElement`.
- Check `g_pHyprRenderer->m_renderData.pMonitor` so you only draw on your monitor.
- **Pass element boxes are in monitor *pixel* coordinates** (`m_transformedSize`). The author's monitor is at scale 1.6. Convert the cursor with `(pos - monitor->m_position) * monitor->m_scale`.
- `g_pHyprRenderer->renderText(text, color, pt)` gives you a texture.
- Nothing draws unless there's damage. Call `g_pHyprRenderer->damageMonitor(mon)` when state changes.

## Snapshotting a workspace

Follow `IHyprRenderer::makeSnapshotFB()` exactly:

```cpp
auto fb = g_pHyprRenderer->createFB("name");
fb->alloc(mon->m_pixelSize.x, mon->m_pixelSize.y, DRM_FORMAT_ABGR8888);
fb->setImageDescription(mon->workBufferImageDescription());   // REQUIRED
CRegion dmg{0, 0, INT16_MAX, INT16_MAX};
g_pHyprRenderer->beginFullFakeRender(mon, dmg, fb);
g_pHyprRenderer->renderWorkspace(mon, ws, Time::steadyNow(), CBox{{0,0}, mon->m_pixelSize});
g_pHyprRenderer->endRender();
```

- **Without `setImageDescription` Hyprland segfaults** as soon as a captured window has a drop shadow: `renderRoundedShadow` dereferences `currentFB->imageDescription()`.
- `renderWorkspace` is `protected`, but it's exported. Get access by wrapping the include in `#define private public` and `#define protected public`. **Pre-include `<any>`, `<sstream>` and `<chrono>` first**, or the macro breaks the STL (the wiki warns about this too).
- `beginSimple` sets the viewport to the monitor pixel size, so a smaller framebuffer needs a custom projection. That's why previews are captured at full resolution.
- Reuse framebuffers between captures, since live previews capture many times a second.

### Choosing which windows get rendered

`renderWorkspaceWindows()` **ignores its workspace argument** when it picks windows. `shouldRenderWindow()` decides, based on the workspace's `m_visible`, `m_forceRendering`, and whether its `m_renderOffset`/`m_alpha` **is animating**. An animating workspace gets its windows rendered no matter what. So, for each capture:

1. Save `m_visible`, `m_forceRendering`, and **both `value()` and `goal()`** of `m_renderOffset` and `m_alpha` for every workspace.
2. Freeze all of them with `setValueAndWarp(value())`. Otherwise a workspace sliding out leaks into every tile.
3. For the target, set visible, forceRendering, offset 0 and alpha 1. Make every other workspace invisible.
4. Restore with `setValueAndWarp(savedValue)`, then `*var = savedGoal` if the goal differs. **Warping alone freezes a mid-switch workspace at its first frame, so it stays invisible.**

### Live previews

Apps only draw when they receive Wayland frame callbacks, and Hyprland only sends those to visible workspaces. Call `g_pHyprRenderer->sendFrameEventsToWorkspace(mon, ws, now)` for hidden workspaces on every live capture. Measured cost: about 2% CPU at 30 fps with 3 workspaces on an Intel UHD 770.

## Input

- `Event::bus()->m_events.input.keyboard.key` is **emitted before keybinds are resolved** (`CInputManager::onKeyboardKey`). Setting `info.cancelled = true` blocks both the binds and the client. For keyboard grab rules, see `core/Keyboard` (`CKeyGrab`): swallow releases only when their press was swallowed, and let through the combo that opened the overview.
- `input.mouse.button` and `input.mouse.move` are cancellable too. Don't cancel move, because the cursor has to move.
- Modifiers: `g_pInputManager->getModsFromAllKBs()`. Cursor: `Pointer::mgr()->position()` (global logical).
- Window under a point: `Desktop::viewState()->hitTest().windowAt(pos, RESERVED_EXTENTS | INPUT_EXTENTS | ALLOW_FLOATING)`.

## Workspaces, monitors, windows

`CCompositor` got much smaller in 0.56. State lives in trackers:
- `State::monitorState()->monitors()`
- `State::workspaceState()->workspacesCopy()` and `->query().id(id).run()`, plus `->create(id, monID, name, isEmpty)`.
- Focused monitor: `Desktop::focusState()->monitor()` (`desktop/state/FocusState.hpp`). The plugin currently takes `monitors().front()` because v1 is single-monitor. Switch to this for multi-monitor support.

Other APIs:
- Switching: `monitor->changeWorkspace(ws, false, /*noMouseMove*/ true)`. `changeWorkspace(id)` is a **no-op** if the workspace doesn't exist, so create it first.
- Moving a window: `Config::Actions::moveToWorkspace(ws, /*silent*/ true, window)` (`config/shared/actions/ConfigActions.hpp`, the same code as `movetoworkspacesilent`). Focus a window with `Config::Actions::focus(w)`.
- Hyprland's `empty` workspace selector picks the first ID ≥ 1 that doesn't exist or has no windows on this monitor. `core::nextEmptyWorkspaceID` mirrors it.
- Empty workspaces are destroyed when you leave them, sometimes a bit later. Re-capture about 600 ms after changes.
- Right after a move or switch, windows are mid-animation, so an immediate snapshot shows stale positions. Capture again after a delay.

## Timers and lifetime

- `makeShared<CEventLoopTimer>(std::nullopt, cb, nullptr)` + `g_pEventLoopManager->addTimer(t)`, arm with `t->updateTimeout(dur)`, disarm with `std::nullopt`.
- **Anything that holds a callback into the `.so` must be released before unload.** Call `removeTimer` in the destructor. Store `CHyprSignalListener`s as members, so resetting `g_pOverview` in `PLUGIN_EXIT` drops them. The integration test `test_unload_while_open_is_safe` guards this.
