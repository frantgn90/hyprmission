# hyprmission

[![CI](https://github.com/frantgn90/hyprmission/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/frantgn90/hyprmission/actions/workflows/ci.yml)
[![core coverage](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Ffrantgn90%2Fhyprmission%2Fbadges%2Fcoverage.json)](https://github.com/frantgn90/hyprmission/actions/workflows/ci.yml)

A macOS Mission Control-style workspace overview for [Hyprland](https://hypr.land).

Toggle it and you get a bar of workspace previews along the top, with the active workspace shown large underneath. Switch workspaces, move windows between them, and create new ones, with the mouse or the keyboard.

## Features

- **Workspace bar**: centered row of live previews of every workspace on the monitor. The active one is outlined in blue.
- **Main view**: the active workspace, scaled to fit the remaining space, keeping its aspect ratio.
- **Switch workspaces**: click a preview (or select it with ←/→ and press Enter). The overview stays open and the main view follows.
- **Close**: click the main view (focuses the window you clicked), click or press Enter on the already-active workspace, press Esc, or toggle again.
- **Move windows**: drag a window from the main view onto another workspace's preview.
- **New workspace**: the grayed `+` slot at the end of the bar. Click it, select it with the arrows and press Enter, or drop a window on it. It uses the same rule as Hyprland's `empty` workspace selector (first ID ≥ 1 that doesn't exist or has no windows).
- **Keyboard grab**: while open, every key press is swallowed, so nothing reaches the apps underneath. The only exception is the key combo that opened the overview, which closes it.

## Requirements

- Hyprland **0.56.2**. That's the only version it has been built and tested against. Plugins use Hyprland internals, so other versions may need changes.
- Single monitor. Multi-monitor setups aren't handled yet.

## Install

With `hyprpm`:

```sh
hyprpm add https://github.com/frantgn90/hyprmission
hyprpm enable hyprmission
```

Or build and load it manually:

```sh
make
hyprctl plugin load "$PWD/hyprmission.so"
```

## Keybinding

Lua config (e.g. Omarchy's `~/.config/hypr/bindings.lua`):

```lua
hl.bind("SUPER + TAB", function()
    hl.plugin.hyprmission.toggle()
end, { description = "Workspace overview" })
```

Classic `hyprland.conf` (not tested yet):

```ini
bind = SUPER, TAB, hyprmission:toggle,
```

## Configuration

Options go in your Hyprland config under `plugin:hyprmission`, and a `hyprctl reload` applies them.

Lua config:

```lua
hl.config({
    plugin = {
        hyprmission = {
            live_previews = true, -- previews keep updating while the overview is open
            live_fps      = 30,   -- refresh rate of live previews (1-144)
        },
    },
})
```

Classic `hyprland.conf`:

```ini
plugin {
    hyprmission {
        live_previews = true
        live_fps = 30
    }
}
```

| Option          | Type | Default | Description |
|-----------------|------|---------|-------------|
| `live_previews` | bool | `true`  | Re-capture every workspace while the overview is open, so previews show what's happening (video, terminals, …). Apps on hidden workspaces keep drawing while it's open. With `false`, previews are snapshots taken on open and after changes. |
| `live_fps`      | int  | `30`    | How many times per second live previews are refreshed. |

## Known limitations

- No workspace reordering yet.
- Single monitor only.

## Development

```sh
make              # build hyprmission.so
make test         # unit tests
make coverage     # unit tests + coverage report in build/ (needs gcovr)
make integration  # integration tests against a real, nested Hyprland
```

Notes for contributors are in [`CLAUDE.md`](CLAUDE.md) and [`.claude/skills/`](.claude/skills). They cover how the Hyprland internals the plugin relies on actually behave, the testing setup, the dev loop and upgrading Hyprland. They're written so Claude Code can use them, but they read fine as plain docs.

The code is split in two layers:

- `src/core/`: the overview's logic as plain C++, with no compositor dependencies. This covers layout, hit testing, coordinate mapping, keyboard selection, the keyboard grab, and the workspace rules.
- `src/Overview.cpp`: the glue that connects that logic to Hyprland. It captures workspaces, draws the overlay and handles input events.

### Tests

- **Unit tests** (`tests/unit/`, GoogleTest, the framework Hyprland uses for its own tests) cover `src/core/`. They run in CI, and the *core coverage* badge measures them.
- **Integration tests** (`tests/integration/run.py`) take the same approach as Hyprland's `hyprtester`. They launch a real Hyprland with a test config, load the plugin and drive it: they click, drag and type through a virtual pointer and keyboard, open test windows, and check the results through `hyprctl` and screenshots. This covers the compositor glue (toggle, switching, drag and drop, the keyboard grab, live previews, unloading while open), plus regression tests for bugs found along the way. The run is nested in your Wayland session, like the Hyprland wiki recommends for plugin development, and uses its own runtime dir, so it never touches your real session. It needs a GPU, so it can't run on GitHub's runners. Run it locally before pushing.

`hyprctl hyprmission` prints the overview's state as JSON: whether it's open, the active and selected workspace, and the tile geometry. The integration tests use it, and it's handy for debugging.

`devtools/` has the small Wayland clients the integration tests use. They aren't part of the plugin:

- `vclick X Y W H move|click|drag ...`: a virtual pointer (`wlr-virtual-pointer`).
- `vkey KEYCODE... [-m MODMASK]`: a virtual keyboard with the default xkb keymap.
- `testclient [--title T] [--keylog FILE] [--animate] [--color RRGGBB]`: a plain window that can log the keys it receives, or repaint on every frame.

## License

MIT, see [LICENSE](LICENSE).
