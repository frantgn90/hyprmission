---
name: hyprmission-testing
description: Run, write and debug hyprmission's tests - GoogleTest unit tests for src/core, the nested-Hyprland integration suite (tests/integration/run.py) with virtual pointer/keyboard and test windows, coverage, and the GitHub Actions CI with the coverage badge. Use whenever changing behavior, adding a feature, fixing a bug, or when a test or the CI fails.
---

# Testing hyprmission

## Which test for what

| Change | Test |
|---|---|
| Logic: geometry, selection, key handling, workspace rules, JSON | unit test in `tests/unit/test_<module>.cpp` (`make test`) |
| Anything touching Hyprland: rendering, capture, input events, workspace/window actions, lifetime | integration test in `tests/integration/run.py` (`make integration`) |
| A bug fix | a regression test that **fails with the bug reintroduced**. Check it: patch the fix out, rebuild, run the single test, then restore the file. If the test still passes, it's worthless: strengthen it or drop it. |

Put new logic in `src/core/` so it can be unit tested. Keep `Overview.cpp` thin.

## Unit tests

- GoogleTest (the framework Hyprland uses), linked against `hyprutils` and `gtest_main`. Built with `--coverage` into `build/unit_tests`.
- `make coverage` needs `gcovr`. If it isn't installed, use `python -m venv /tmp/v && /tmp/v/bin/pip install gcovr`. Target: `src/core/` lines about 99%, functions 100%. Uncovered branches are mostly exception edges inside `std::format`, which is fine.
- `Vector2D{int, double}` is ambiguous, so write `{1.0, y}` in tests.

## Integration tests (`make integration`)

It follows the same approach as Hyprland's `hyprtester` (`hyprtester/` in the Hyprland repo): launch Hyprland with a test config, drive it over the hyprctl socket, and check the results.

- **It runs nested** inside the current Wayland session, with its own runtime dir. You'll see a window on your desktop while it runs. It never touches the host compositor.
- **It can't run headless or in CI.** Hyprland always needs a DRM render node for buffer allocation: without a GPU, `CBackend::create() failed!`, in containers and on GitHub runners alike. Hyprland itself runs hyprtester in a NixOS VM with virtio-gpu. Run it locally before pushing.
- The runtime dir is `/tmp/hm-XXXX` and has to stay short, because unix socket paths are limited to 108 characters and Hyprland nests a ~60-character signature under it. With a longer dir, the socket file ends up truncated as `.socket.soc`.
- State: `h.state()` returns the parsed `hyprctl hyprmission` output (`open`, `active`, `selected` where `"new"` means the `+` slot, `workspaces`, `tiles`/`newSlot`/`main` boxes in **global logical coords**). Use those boxes to click. Tile positions move whenever a workspace appears or disappears, so never hard-code them.
- Harness helpers: `spawn(title, *args)`, `focus_ws(n)`, `toggle()`, `click(x, y)`, `drag(x1, y1, x2, y2)`, `key(*codes, mods=)`, `grab(box)` (PNG bytes), `pixel(x, y)` (RGB via `grim -t ppm`), `lua(code)`, `ctl(...)`/`ctlj(...)`. `reset()` runs between tests: it closes the overview, kills clients, runs `hyprctl reload` (which reverts any `hl.config` changes a test made) and goes back to workspace 1.
- Dispatch in the nested instance: `ctl("dispatch", "hl.dsp.focus({ workspace = '2' })")`.

### devtools (`make -C devtools`)

- `vclick X Y W H move|click|down|up|drag X2 Y2 HOLD_MS`: a `wlr-virtual-pointer` device, using **logical** coords with W×H being the layout size. `drag` is one process (press, glide, hold, release).
- `vkey K1 [K2 ...] [-m MODMASK]`: a virtual keyboard with the **default xkb keymap**, so evdev codes match a real keyboard (1=Esc, 28=Enter, 30=A, 105/106=←/→, 88=F12, 125=Super, mask 64=Super). **Don't use `wtype`**: it uploads its own keymap, so the codes don't match. `wtype -M logo -k F12` once opened Omarchy's System menu.
- `testclient [--title T] [--keylog FILE] [--animate] [--color RRGGBB]`: a plain xdg-shell window. `--keylog` records the keys it receives, which is how the keyboard grab is tested. `--animate` changes color on every frame callback, which is how live previews are tested. `--color ff00ff` makes it easy to spot with `pixel()`. It's built with `-std=gnu17`, because GCC 15 defaults to C23, where `void f()` means "no args".

### Pitfalls that already cost time

- **Dwindle places a new window on the side the cursor is on.** Don't assume left/right: locate windows through `h.client(title)["at"/"size"]`, and use `in_main_view()` to map them into the overview.
- Animations are off in the test config. Tests that need them enable them via `h.lua('hl.config({ animations = { enabled = true } })')`. Omarchy disables the `workspaces` animation but keeps fades.
- Live previews re-capture 30 times a second and can hide a transient bug within a few frames. For snapshot bugs, set `live_previews = false`.
- Windows that just spawned are still fading in, so a capture taken right away shows them dark.
- Nested crashes show up as desktop notifications ("Process crashed: Hyprland") and in `coredumpctl list Hyprland`. The report is at `~/.cache/hyprland/hyprlandCrashReport<pid>.txt`, with a backtrace that includes plugin frames. The host session is a different PID.
- When sending Esc or keys to the **host** session, move focus off the Claude terminal first: a leaked Esc interrupts the agent.

## CI (`.github/workflows/ci.yml`)

- It runs in the `archlinux:latest` container, **pinned to an Arch archive snapshot** (`archive.archlinux.org/repos/2026/09/10`), which ships `hyprland 0.56.2-2` with matching hyprutils, gtest and gcovr. It uses `pacman -Syyuu`, because the image may be newer than the snapshot.
- Steps: build the plugin, build devtools, `make coverage`, upload the report, and publish the badge JSON (shields endpoint schema) to the orphan `badges` branch on pushes to `main`.
- README badge: **URL-encode the shields `?url=` parameter** (`https%3A%2F%2F...`). If it isn't encoded, GitHub's camo image proxy returns 404 and the badge reads "resource not found". The CI badge uses `badge.svg?branch=main`. To verify a badge, fetch the `camo.githubusercontent.com` `src` from the repo page HTML, not the direct URL.
- Pushing workflow changes needs a token with the `workflow` scope (`gh auth refresh -h github.com -s workflow`).
- Check a run with `gh run watch <id> --exit-status`, then `gh run view <id> --log | grep -E "PASSED|lines:"`.
- You can simulate CI locally: `docker run --rm -v $PWD:/src:ro archlinux:latest bash -c '<same pacman steps>; cp -r /src /w && cd /w && make && make coverage'`.
