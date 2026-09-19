---
name: hyprmission-dev-loop
description: Day-to-day loop for developing hyprmission on a live Hyprland session - build, hot reload, trigger, screenshot and verify without the user, keybinds on Omarchy's Lua config, hyprpm installation, and diagnosing crashes. Use when starting work on the plugin, trying a change on the real desktop, or when the plugin misbehaves or Hyprland crashes.
---

# Dev loop

## Build → reload → verify

```sh
make
SO="$PWD/hyprmission.so"
hyprctl plugin unload "$SO"; hyprctl plugin load "$SO"
hyprctl repl "hl.plugin.hyprmission.toggle()"   # open, same function the keybind calls
sleep 0.5; grim -s 0.3 /tmp/shot.png            # screenshot (downscaled), then look at it
hyprctl hyprmission                              # state JSON: open, active, selected, tile boxes
hyprctl repl "hl.plugin.hyprmission.toggle()"   # close
```

- Load the plugin by absolute path. `hyprctl plugin list` shows what's loaded. If an installed copy exists, unload it first. hyprpm's copy lives at `/var/cache/hyprpm/$USER/hyprmission/hyprmission.so`.
- `hyprctl dispatch <name>` **doesn't work** for plugin dispatchers on this build (it evaluates Lua). Use `hyprctl repl` with the Lua function.
- To simulate input on the real session, use `devtools/vclick` and `devtools/vkey` (see the testing skill). Prefer the nested test instance for anything risky.
- Before adding logic to the glue code, run `make test` and `make integration`, and prefer putting the logic in `src/core/`.

## Keybind (Omarchy, Lua config)

Put it in `~/.config/hypr/bindings.lua`:

```lua
hl.bind("SUPER + TAB", function() hl.plugin.hyprmission.toggle() end, { description = "Workspace overview" })
```

Don't use `o.bind(..., "hyprmission:toggle")`: Omarchy's `o.bind` turns strings into shell commands. Check the bind with `hyprctl binds | grep -A6 TAB`. Seeing `dispatcher: __lua` is expected. Apply changes with `hyprctl reload`. `omarchy menu keybindings --print` lists what's taken.

## Config while testing

```sh
hyprctl repl "hl.config({ plugin = { hyprmission = { live_previews = false } } })"
hyprctl getoption plugin:hyprmission:live_fps
```

A `hyprctl reload` resets these to what the config files say.

## hyprpm

- `hyprpm add https://github.com/frantgn90/hyprmission`, `hyprpm enable hyprmission`, `hyprpm disable`, `hyprpm update`.
- Every state-changing hyprpm command runs `sudo install/mkdir/rm` internally. In a non-interactive shell they fail with a misleading "Failed to write plugin state". It isn't a permissions problem, so don't chown anything. **Ask the user to run it** with `! hyprpm ...`, then verify with `hyprpm list`, which works non-interactively.
- `hyprpm.toml` in the repo root drives hyprpm builds (`make all` → `hyprmission.so`). Add `commit_pins` when supporting several Hyprland versions.
- An enabled hyprpm copy loads at startup. Disable it while developing so it doesn't clash with the dev build (both register `hyprmission:toggle`).

## When something breaks

- **Nothing drawn**: check how the element is added to the render pass. See `hyprland-plugin-internals` → "Drawing an overlay".
- **Workspace blank or frozen after switching**: the animation restore is losing `goal()`. See the snapshot section of the internals skill.
- **Crash**: the report is at `~/.cache/hyprland/hyprlandCrashReport<pid>.txt`. Read the `Backtrace:` section, where plugin frames show as `hyprmission.so(...)`. `coredumpctl list Hyprland` shows which PID died, so you can tell a nested test instance from the real session. Reproduce it in the nested instance (`python3 tests/integration/run.py <test>`), never on the live session.
- **Timing-dependent bugs** usually come from Hyprland animations (window moves, workspace switches, fade-in of new windows) or from live re-captures masking them.
