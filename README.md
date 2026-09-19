# hyprmission

A macOS Mission Control-style workspace overview for [Hyprland](https://hypr.land).

Toggle it and you get a bar of workspace previews along the top, with the active workspace shown large underneath. Switch workspaces, move windows between them, and create new ones, with the mouse or the keyboard.

## Features

- **Workspace bar**: centered row of previews of every workspace on the monitor. The active one is outlined in blue.
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

## Known limitations

- Previews are snapshots taken when the overview opens (and refreshed after a switch or a window move), not live video.
- No workspace reordering yet.
- Single monitor only.

## Development

`devtools/` has two small Wayland clients used to test the overview without touching the real keyboard or mouse. They aren't part of the plugin:

- `vclick X Y W H move|click|drag ...` uses a virtual pointer (`wlr-virtual-pointer`)
- `vkey KEYCODE... [-m MODMASK]` uses a virtual keyboard with the default xkb keymap

```sh
make -C devtools
```
