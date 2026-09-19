# hyprmission

A macOS Mission Control-style workspace overview plugin for Hyprland. A top bar shows a preview of every workspace plus a `+` slot; below it the active workspace is drawn scaled. You can switch workspaces, drag windows between them, and create new ones, with the mouse or the keyboard.

**Target: Hyprland 0.56.2 only.** Plugins link against Hyprland internals with no stable ABI. Assume nothing carries over to another version unless you verified it. See `.claude/skills/hyprland-upgrade`.

## Commands

```sh
make                        # build hyprmission.so
make test                   # unit tests (GoogleTest, src/core)
make coverage               # unit tests + gcovr report in build/ (needs gcovr)
make integration            # integration tests in a nested Hyprland (needs a Wayland session + GPU)
make -C devtools            # virtual pointer/keyboard + test client used by the integration tests

hyprctl plugin unload "$PWD/hyprmission.so"; hyprctl plugin load "$PWD/hyprmission.so"   # dev reload
hyprctl hyprmission         # overview state as JSON
hyprctl repl "hl.plugin.hyprmission.toggle()"   # toggle without a keybind
```

## Layout

- `src/core/`: **all decision logic, as pure C++** (it only uses hyprutils math). Covers layout, hit testing, coordinate mapping, keyboard selection, the keyboard grab, the next-empty-workspace rule, and state JSON. It's unit tested at about 99% line coverage. **Put new logic here and test it here.**
- `src/Overview.cpp`: compositor glue. It captures workspace previews, adds render pass elements, and turns Hyprland events into `core/` calls. Integration tests cover it.
- `src/main.cpp`: `PLUGIN_INIT`/`PLUGIN_EXIT`. It registers config values, the dispatcher, the Lua function and the hyprctl command.
- `tests/unit/`: GoogleTest suites, one per `core/` module.
- `tests/integration/run.py`: a hyprtester-style runner (stdlib Python). It drives a nested Hyprland.
- `devtools/`: `vclick`, `vkey` and `testclient`, the Wayland clients used by the integration tests. They aren't part of the plugin.
- `.github/workflows/ci.yml`: builds against 0.56.2 from a pinned Arch archive snapshot, runs the unit tests, and publishes the coverage badge to the `badges` branch.

## Rules

- **Verify Hyprland APIs, don't guess.** Read the installed headers (`/usr/include/hyprland/src/...`), check that the symbol is exported (`nm -D /usr/bin/Hyprland | grep <name>`), and read Hyprland's own source at the matching tag (`git clone --depth 1 --branch v0.56.2 https://github.com/hyprwm/Hyprland`). The wiki doesn't cover rendering internals. Old plugins such as Hyprspace or hyprexpo target older APIs and are *not* a reference for this project.
- **Test every change.** Unit test the logic in `core/`. For anything that touches the compositor, add or extend an integration test and run `make integration`. A regression test only counts if it fails when you reintroduce the bug (mutation-check it).
- **Never experiment on the user's real session** in ways that can break it. Run risky code in the nested test instance. Never use `LIBSEAT_BACKEND=noop` for a second instance: it opens the real GPU and monitor.
- `hyprpm add/enable/disable/update` shell out to `sudo`. They fail silently ("Failed to write plugin state") in a non-interactive shell, so ask the user to run them. For development use `hyprctl plugin load/unload` instead.
- Public repo: code, comments, commits and docs are in English. Keep comments to the non-obvious *why*.
- Commit trailer: `Co-Authored-By: Claude <noreply@anthropic.com>`. Pushing `.github/workflows/*` needs a token with the `workflow` scope (`gh auth refresh -h github.com -s workflow`).

## Skills

- `.claude/skills/hyprland-plugin-internals`: how rendering, snapshots, input, timers, config, state and animations actually work in 0.56.2, plus the traps that crashed or froze things.
- `.claude/skills/hyprmission-testing`: running and writing unit and integration tests, simulating input, the pitfalls of the nested harness, CI and the badge.
- `.claude/skills/hyprmission-dev-loop`: the build, reload and verify loop on a live session, keybinds on Omarchy, screenshots, and crash debugging.
- `.claude/skills/hyprland-upgrade`: steps for moving the plugin to a new Hyprland release.
