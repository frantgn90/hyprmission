---
name: hyprland-upgrade
description: Port hyprmission to a new Hyprland release (or check whether it still works after the system Hyprland was updated). Use when Hyprland's version changes, when the plugin fails to load with a version mismatch, or when the CI snapshot needs to move forward.
---

# Moving to a new Hyprland version

Plugins use Hyprland internals and have no ABI stability. Treat every release as a potential break.

1. **Check the installed version**: `hyprctl version` (tag and commit) and `pacman -Qi hyprland`. If the plugin was built against other headers, `PLUGIN_INIT` refuses to load it with "Version mismatch".
2. **Get the matching source**: `git clone --depth 1 --branch vX.Y.Z https://github.com/hyprwm/Hyprland /tmp/hyprland-src`.
3. **Rebuild**: `make clean && make`. Compile errors point at renamed or moved APIs. Look them up in the new headers under `/usr/include/hyprland/src`. Past moves: `CCompositor` getters went into `State::*`/`Desktop::*` trackers, and `CMonitor` moved into namespace `Monitor`.
4. **Re-verify the assumptions in `hyprland-plugin-internals`** against the new source, especially:
   - the `renderMonitor()` order: `beginRender` clears the pass, then `RENDER_LAST_MOMENT`, then `endRender` draws it
   - what `makeSnapshotFB()` does to set up a framebuffer
   - the rules in `shouldRenderWindow()`
   - that `renderWorkspace`, `beginFullFakeRender` and `sendFrameEventsToWorkspace` are still exported: `nm -D /usr/bin/Hyprland | grep -E "renderWorkspace|beginFullFakeRender|sendFrameEvents"`
   - that the keyboard event is still emitted before keybinds in `CInputManager::onKeyboardKey`
5. **Run everything**: `make test`, then `make integration`. The integration suite is what catches behavior changes.
6. **Move CI forward**: change the snapshot date in `.github/workflows/ci.yml` to one that ships the new version. Check it with `docker run --rm archlinux bash -c 'echo "Server=https://archive.archlinux.org/repos/YYYY/MM/DD/\$repo/os/\$arch" > /etc/pacman.d/mirrorlist; pacman -Syy >/dev/null; pacman -Si hyprland | grep Version'`.
7. **Update the docs**: the version in `README.md` (Requirements), `CLAUDE.md` and these skills.
8. **Supporting several versions**: add `commit_pins` in `hyprpm.toml`. Each entry maps a Hyprland commit to the plugin commit that works with it.
