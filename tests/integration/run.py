#!/usr/bin/env python3
"""Integration tests for hyprmission, against a real Hyprland.

Same approach as Hyprland's own hyprtester: launch Hyprland with a test
config, load the plugin, drive it and check results over the hyprctl socket.
The instance runs nested inside the current Wayland session (the setup the
Hyprland wiki recommends for plugin development) with its own runtime dir,
so it never touches the host compositor. Input is simulated with the
virtual pointer/keyboard tools in devtools/, and test windows are
devtools/testclient.

Usage: python3 tests/integration/run.py [test_name ...]
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import traceback

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PLUGIN = os.path.join(ROOT, "hyprmission.so")
DEVTOOLS = os.path.join(ROOT, "devtools")

KEY_ESC, KEY_ENTER, KEY_A, KEY_LEFT, KEY_RIGHT, KEY_F12, KEY_LEFTMETA = 1, 28, 30, 105, 106, 88, 125
MOD_SUPER = 64

TEST_CONFIG = """
hl.monitor({ output = "", mode = "1280x800@60", position = "auto", scale = "1" })
hl.config({ animations = { enabled = false } })
hl.bind("SUPER + F12", function()
    if hl.plugin.hyprmission then hl.plugin.hyprmission.toggle() end
end, { description = "hyprmission toggle" })
"""


class Failure(Exception):
    pass


def check(cond, msg):
    if not cond:
        raise Failure(msg)


def wait_until(pred, timeout=3.0, step=0.05):
    end = time.time() + timeout
    while time.time() < end:
        if pred():
            return True
        time.sleep(step)
    return pred()


class Nested:
    """A nested Hyprland instance with the plugin loaded."""

    def __init__(self):
        host_rt = os.environ.get("XDG_RUNTIME_DIR")
        host_wl = os.environ.get("WAYLAND_DISPLAY")
        if not host_rt or not host_wl:
            sys.exit("integration tests need a running Wayland session (WAYLAND_DISPLAY)")

        # short on purpose: socket paths are limited to 108 chars and Hyprland
        # nests a ~60 char instance signature under it
        self.runtime = tempfile.mkdtemp(prefix="hm-", dir="/tmp")
        os.chmod(self.runtime, 0o700)
        self.config = os.path.join(self.runtime, "test.lua")
        with open(self.config, "w") as f:
            f.write(TEST_CONFIG)

        env = {k: v for k, v in os.environ.items() if k not in ("HYPRLAND_INSTANCE_SIGNATURE", "DISPLAY")}
        env["XDG_RUNTIME_DIR"] = self.runtime
        env["WAYLAND_DISPLAY"] = host_wl if host_wl.startswith("/") else os.path.join(host_rt, host_wl)
        self.log = open(os.path.join(self.runtime, "hyprland.log"), "w")
        self.proc = subprocess.Popen(["Hyprland", "--config", self.config], env=env, stdout=self.log, stderr=subprocess.STDOUT)
        self.clients = []

        hypr = os.path.join(self.runtime, "hypr")
        check(wait_until(lambda: os.path.isdir(hypr) and any("_" in d for d in os.listdir(hypr)), 20), "nested Hyprland did not start")
        self.signature = next(d for d in os.listdir(hypr) if "_" in d)
        check(wait_until(lambda: "Hyprland" in self.ctl("version"), 20), "nested Hyprland socket not answering")
        wl = sorted(d for d in os.listdir(self.runtime) if d.startswith("wayland-") and not d.endswith(".lock"))
        self.wayland = os.path.join(self.runtime, wl[0])

        out = self.ctl("plugin", "load", PLUGIN)
        check(out.strip() == "ok", f"plugin load failed: {out}")

    # --- plumbing
    def ctl(self, *args):
        env = dict(os.environ, XDG_RUNTIME_DIR=self.runtime, HYPRLAND_INSTANCE_SIGNATURE=self.signature)
        return subprocess.run(["hyprctl", *args], env=env, capture_output=True, text=True, timeout=10).stdout

    def ctlj(self, *args):
        return json.loads(self.ctl("-j", *args))

    def lua(self, code):
        return self.ctl("repl", code)

    def client_env(self):
        return dict(os.environ, WAYLAND_DISPLAY=self.wayland, XDG_RUNTIME_DIR=self.runtime)

    def alive(self):
        return self.proc.poll() is None

    def stop(self):
        for c in self.clients:
            c.kill()
        self.proc.terminate()
        try:
            self.proc.wait(5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        self.log.close()

    # --- state
    def state(self):
        return json.loads(self.ctl("hyprmission"))

    def active_ws(self):
        return self.ctlj("activeworkspace")["id"]

    def workspaces(self):
        return sorted(w["id"] for w in self.ctlj("workspaces") if w["id"] > 0)

    def client(self, title):
        return next((c for c in self.ctlj("clients") if c["title"] == title), None)

    def monitor_size(self):
        m = self.ctlj("monitors")[0]
        return m["width"] / m["scale"], m["height"] / m["scale"]

    # --- actions
    def toggle(self):
        self.lua("hl.plugin.hyprmission.toggle()")

    def focus_ws(self, ws):
        self.ctl("dispatch", f"hl.dsp.focus({{ workspace = '{ws}' }})")
        check(wait_until(lambda: self.active_ws() == ws), f"could not focus workspace {ws}")

    def spawn(self, title, *extra):
        p = subprocess.Popen([os.path.join(DEVTOOLS, "testclient"), "--title", title, *extra], env=self.client_env())
        self.clients.append(p)
        check(wait_until(lambda: self.client(title) is not None, 5), f"client {title} never mapped")
        return p

    def click(self, x, y):
        w, h = self.monitor_size()
        subprocess.run([os.path.join(DEVTOOLS, "vclick"), str(int(x)), str(int(y)), str(int(w)), str(int(h)), "click"], env=self.client_env(), check=True)
        time.sleep(0.15)

    def drag(self, x1, y1, x2, y2):
        w, h = self.monitor_size()
        subprocess.run([os.path.join(DEVTOOLS, "vclick"), str(int(x1)), str(int(y1)), str(int(w)), str(int(h)), "drag", str(int(x2)), str(int(y2)), "200"],
                       env=self.client_env(), check=True)
        time.sleep(0.2)

    def key(self, *keys, mods=0):
        args = [os.path.join(DEVTOOLS, "vkey"), *map(str, keys)]
        if mods:
            args += ["-m", str(mods)]
        subprocess.run(args, env=self.client_env(), check=True)
        time.sleep(0.15)

    def grab(self, box):
        """PNG bytes of a logical-coords region of the nested output."""
        g = f"{int(box['x']) + 4},{int(box['y']) + 4} {int(box['w']) - 8}x{int(box['h']) - 8}"
        return subprocess.run(["grim", "-g", g, "-"], env=self.client_env(), capture_output=True, check=True).stdout

    def pixel(self, x, y):
        """(r, g, b) at a logical point of the nested output."""
        ppm = subprocess.run(["grim", "-t", "ppm", "-g", f"{int(x)},{int(y)} 1x1", "-"], env=self.client_env(), capture_output=True, check=True).stdout
        return tuple(ppm[-3:])

    def reset(self):
        if self.state()["open"]:
            self.toggle()
        for c in self.clients:
            c.kill()
            c.wait()
        self.clients = []
        self.ctl("reload")
        wait_until(lambda: not self.ctlj("clients"))
        self.focus_ws(1)
        wait_until(lambda: self.workspaces() == [1])


def center(box):
    return box["x"] + box["w"] / 2, box["y"] + box["h"] / 2


def is_magenta(px):
    r, g, b = px
    return r > 200 and g < 60 and b > 200


def window_center(h, title):
    c = h.client(title)
    return c["at"][0] + c["size"][0] / 2, c["at"][1] + c["size"][1] / 2


# ---------------------------------------------------------------- tests


def test_config_options_registered(h):
    check("bool: true" in h.ctl("getoption", "plugin:hyprmission:live_previews"), "live_previews should default to true")
    check("int: 30" in h.ctl("getoption", "plugin:hyprmission:live_fps"), "live_fps should default to 30")


def test_toggle_opens_and_closes(h):
    check(not h.state()["open"], "should start closed")
    h.toggle()
    s = h.state()
    check(s["open"], "toggle should open")
    check(s["active"] == 1 and s["selected"] == 1, f"active/selected should be 1: {s}")
    h.toggle()
    check(not h.state()["open"], "second toggle should close")


def test_layout_has_a_tile_per_workspace(h):
    h.spawn("a")
    h.focus_ws(2)
    h.spawn("b")
    h.focus_ws(1)
    h.toggle()
    s = h.state()
    check(s["workspaces"] == [1, 2], f"bar should list workspaces 1,2: {s['workspaces']}")
    check(len(s["tiles"]) == 2, "one tile per workspace")
    t0, t1, ns, main = s["tiles"][0], s["tiles"][1], s["newSlot"], s["main"]
    check(t0["x"] + t0["w"] < t1["x"] and t1["x"] + t1["w"] < ns["x"], "tiles then + slot, left to right")
    check(main["y"] > t0["y"] + t0["h"], "main view below the bar")
    w, _ = h.monitor_size()
    check(abs(t0["x"] - (w - (ns["x"] + ns["w"]))) < 1.5, "bar centered")


def test_arrows_move_selection_and_enter_switches(h):
    h.spawn("a")
    h.focus_ws(2)
    h.spawn("b")
    h.focus_ws(1)
    h.toggle()
    h.key(KEY_RIGHT)
    check(h.state()["selected"] == 2, "right selects workspace 2")
    check(h.active_ws() == 1, "moving the selection doesn't switch")
    h.key(KEY_ENTER)
    s = h.state()
    check(h.active_ws() == 2 and s["active"] == 2, "enter switches to the selected workspace")
    check(s["open"], "switching keeps the overview open")
    h.key(KEY_ENTER)
    check(not h.state()["open"], "enter on the active workspace closes")
    check(h.active_ws() == 2, "and stays there")


def test_arrows_clamp_and_reach_new_slot(h):
    h.spawn("a")
    h.toggle()
    h.key(KEY_LEFT)
    check(h.state()["selected"] == 1, "left on the first tile stays there")
    h.key(KEY_RIGHT)
    check(h.state()["selected"] == "new", "right past the last tile selects +")
    h.key(KEY_RIGHT)
    check(h.state()["selected"] == "new", "+ is the end")
    h.key(KEY_LEFT)
    check(h.state()["selected"] == 1, "left from + goes back")


def test_enter_on_new_slot_creates_next_empty_workspace(h):
    h.spawn("a")
    h.focus_ws(2)
    h.spawn("b")
    h.focus_ws(1)
    h.toggle()
    h.key(KEY_RIGHT)
    h.key(KEY_RIGHT)
    h.key(KEY_ENTER)
    s = h.state()
    check(h.active_ws() == 3, f"first free id is 3, got {h.active_ws()}")
    check(s["open"] and s["active"] == 3 and 3 in s["workspaces"], f"new workspace shows up in the bar: {s}")


def test_new_workspace_left_empty_disappears(h):
    h.spawn("a")
    h.toggle()
    h.click(*center(h.state()["newSlot"]))
    check(h.active_ws() == 2, "clicking + creates workspace 2")
    check(h.state()["workspaces"] == [1, 2], "and it gets a tile")
    h.click(*center(h.state()["tiles"][0]))
    check(h.active_ws() == 1, "back to 1")
    check(wait_until(lambda: h.state()["workspaces"] == [1], 2), f"empty workspace 2 should be gone from the bar: {h.state()['workspaces']}")
    check(h.workspaces() == [1], "and from Hyprland")


def test_click_tile_switches_and_click_active_closes(h):
    h.spawn("a")
    h.focus_ws(2)
    h.spawn("b")
    h.focus_ws(1)
    h.toggle()
    h.click(*center(h.state()["tiles"][1]))
    s = h.state()
    check(h.active_ws() == 2 and s["open"], "clicking a tile switches and stays open")
    check(s["selected"] == 2, "clicked tile becomes the selection")
    h.click(*center(s["tiles"][1]))
    check(not h.state()["open"], "clicking the active tile closes")


def test_click_background_does_nothing(h):
    h.spawn("a")
    h.toggle()
    _, hgt = h.monitor_size()
    h.click(2, hgt - 2)
    check(h.state()["open"], "background click keeps it open")
    check(h.active_ws() == 1, "and changes nothing")


def in_main_view(h, main, win):
    """Center of a window as drawn in the main view (logical coords)."""
    w, hgt = h.monitor_size()
    return (main["x"] + (win["at"][0] + win["size"][0] / 2) / w * main["w"],
            main["y"] + (win["at"][1] + win["size"][1] / 2) / hgt * main["h"])


def test_click_main_view_closes_and_focuses_window(h):
    h.spawn("one")
    h.spawn("two")
    # click the window that is NOT focused (dwindle picks sides from the
    # cursor position, so don't assume which one ended up where)
    focused = h.ctlj("activewindow")["title"]
    target = "one" if focused == "two" else "two"
    h.toggle()
    h.click(*in_main_view(h, h.state()["main"], h.client(target)))
    check(not h.state()["open"], "clicking the main view closes")
    check(h.ctlj("activewindow").get("title") == target, f"clicked window {target} gets focus, got {h.ctlj('activewindow').get('title')}")


def test_drag_window_to_other_workspace(h):
    h.spawn("mover")
    h.spawn("stayer")
    h.focus_ws(2)
    h.spawn("b")
    h.focus_ws(1)
    h.toggle()
    s = h.state()
    sx, sy = in_main_view(h, s["main"], h.client("mover"))
    h.drag(sx, sy, *center(s["tiles"][1]))
    check(h.client("mover")["workspace"]["id"] == 2, "dropped window moves to workspace 2")
    check(h.client("stayer")["workspace"]["id"] == 1, "the other one stays")
    check(h.active_ws() == 1 and h.state()["open"], "moving doesn't switch or close")


def test_drag_window_to_new_slot_creates_workspace(h):
    h.spawn("mover")
    h.spawn("stayer")
    h.toggle()
    s = h.state()
    sx, sy = in_main_view(h, s["main"], h.client("mover"))
    h.drag(sx, sy, *center(s["newSlot"]))
    check(h.client("mover")["workspace"]["id"] == 2, "dropping on + moves it to the next empty workspace")
    check(h.state()["workspaces"] == [1, 2], "which gets a tile")


def test_keys_are_swallowed_while_open(h):
    log = os.path.join(h.runtime, "keys.log")
    open(log, "w").close()
    h.spawn("typing", "--keylog", log)
    h.toggle()
    h.key(KEY_A)
    h.key(KEY_ESC)
    check(not h.state()["open"], "esc closes")
    check(open(log).read() == "", f"no key should reach the client while open: {open(log).read()!r}")
    h.key(KEY_A)
    check(wait_until(lambda: "press 30" in open(log).read()), "keys reach the client again after closing")


def test_toggle_combo_opens_and_closes(h):
    log = os.path.join(h.runtime, "keys.log")
    open(log, "w").close()
    h.spawn("typing", "--keylog", log)
    h.key(KEY_LEFTMETA, KEY_F12, mods=MOD_SUPER)
    check(wait_until(lambda: h.state()["open"]), "SUPER+F12 opens")
    h.key(KEY_A)
    h.key(KEY_LEFTMETA, KEY_F12, mods=MOD_SUPER)
    check(wait_until(lambda: not h.state()["open"]), "the same combo closes")
    check("press 30" not in open(log).read(), "keys in between were swallowed")


def test_live_previews_follow_hidden_workspaces(h):
    h.spawn("a")
    h.focus_ws(2)
    h.spawn("anim", "--animate")
    h.focus_ws(1)
    h.toggle()
    tile = h.state()["tiles"][1]
    frames = {h.grab(tile) for _ in range(4) if not time.sleep(0.15)}
    check(len(frames) > 1, "the hidden workspace's tile should keep changing")


def test_live_previews_can_be_disabled(h):
    h.lua("hl.config({ plugin = { hyprmission = { live_previews = false } } })")
    h.spawn("a")
    h.focus_ws(2)
    h.spawn("anim", "--animate")
    h.focus_ws(1)
    h.toggle()
    tile = h.state()["tiles"][1]
    time.sleep(0.3)
    frames = {h.grab(tile) for _ in range(4) if not time.sleep(0.15)}
    check(len(frames) == 1, "with live_previews = false the tile is a snapshot")


# --- regressions for bugs found while building this


def test_shadows_do_not_crash_captures(h):
    # capturing a workspace whose windows had drop shadows crashed Hyprland:
    # our framebuffers had no image description, the shadow shader reads it
    h.lua("hl.config({ decoration = { shadow = { enabled = true, range = 20 } } })")
    h.spawn("a")
    h.focus_ws(2)
    h.spawn("b")
    h.focus_ws(1)
    h.toggle()
    time.sleep(0.3)
    check(h.alive() and h.state()["open"], "opening with shadowed windows must not crash")


def test_switching_with_animations_keeps_workspace_visible(h):
    # capturing mid-animation used to freeze the workspace we switched to at
    # its first animation frame, i.e. invisible
    h.lua("hl.config({ animations = { enabled = true } })")
    h.spawn("pink", "--color", "ff00ff")
    h.focus_ws(2)
    h.spawn("b")
    h.focus_ws(1)
    h.toggle()
    tiles = h.state()["tiles"]
    h.click(*center(tiles[1]))
    h.click(*center(tiles[0]))
    h.click(*center(tiles[0]))  # active tile: close
    check(not h.state()["open"], "closed")
    time.sleep(1.5)  # let every animation finish
    px = h.pixel(*window_center(h, "pink"))
    check(is_magenta(px), f"workspace 1 must be visible after switching back, pixel {px}")


def test_unload_while_open_is_safe(h):
    h.spawn("a")
    h.toggle()
    check(h.ctl("plugin", "unload", PLUGIN).strip() == "ok", "unload")
    time.sleep(0.3)
    check(h.alive(), "Hyprland survives unloading while open")
    h.click(100, 100)  # input after unload must not reach dangling listeners
    h.key(KEY_A)
    check(h.alive(), "and input afterwards")
    check(h.ctl("plugin", "load", PLUGIN).strip() == "ok", "reload")
    check(not h.state()["open"], "fresh instance starts closed")


# ---------------------------------------------------------------- runner


def main():
    for tool in ("vclick", "vkey", "testclient"):
        if not os.path.exists(os.path.join(DEVTOOLS, tool)):
            sys.exit(f"missing devtools/{tool}: run `make -C devtools`")
    if not os.path.exists(PLUGIN):
        sys.exit("missing hyprmission.so: run `make`")
    if not shutil.which("grim"):
        sys.exit("grim is needed for the live preview tests")

    tests = [(n, f) for n, f in globals().items() if n.startswith("test_") and callable(f)]
    if len(sys.argv) > 1:
        tests = [(n, f) for n, f in tests if n in sys.argv[1:]]

    h = Nested()
    failed = []
    try:
        for name, fn in tests:
            try:
                h.reset()
                fn(h)
                print(f"PASS {name}")
            except Exception as e:
                failed.append(name)
                print(f"FAIL {name}: {e}")
                if not isinstance(e, Failure):
                    traceback.print_exc()
            if not h.alive():
                print("Hyprland died, aborting")
                failed.append("<crash>")
                break
    finally:
        h.stop()

    print(f"\n{len(tests) - len(failed)}/{len(tests)} passed")
    if failed:
        print(f"logs: {h.runtime}")
    else:
        shutil.rmtree(h.runtime, ignore_errors=True)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
