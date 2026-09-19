// Dev-only test tool: press+release one evdev keycode through
// zwp_virtual_keyboard_v1 using the default xkb keymap, so keycodes match a
// real keyboard (unlike wtype, which uploads a custom keymap).
// usage: vkey EVDEV_KEYCODE
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "virtual-keyboard-unstable-v1-client-protocol.h"

static struct wl_seat*                           seat;
static struct zwp_virtual_keyboard_manager_v1*   mgr;

static void global_add(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t ver) {
    if (!strcmp(iface, wl_seat_interface.name))
        seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
    else if (!strcmp(iface, zwp_virtual_keyboard_manager_v1_interface.name))
        mgr = wl_registry_bind(r, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
}
static void global_rm(void* d, struct wl_registry* r, uint32_t name) {}
static const struct wl_registry_listener reg_listener = {global_add, global_rm};

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(int argc, char** argv) {
    if (argc < 2)
        return 1;
    uint32_t key = atoi(argv[1]);

    struct wl_display* dpy = wl_display_connect(NULL);
    struct wl_registry* reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &reg_listener, NULL);
    wl_display_roundtrip(dpy);
    if (!mgr)
        return 1;

    struct xkb_context* ctx = xkb_context_new(0);
    struct xkb_keymap*  km  = xkb_keymap_new_from_names(ctx, NULL, 0);
    char*               str = xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1);
    size_t              len = strlen(str) + 1;
    int                 fd  = memfd_create("keymap", 0);
    write(fd, str, len);

    struct zwp_virtual_keyboard_v1* kb = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(mgr, seat);
    zwp_virtual_keyboard_v1_keymap(kb, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, len);
    wl_display_roundtrip(dpy);

    // vkey K1 [K2 ...] [-m MODMASK]: press all keys in order, release in
    // reverse. MODMASK (xkb depressed mask, e.g. 64 = Super) is sent after
    // the first key goes down and cleared before it comes back up.
    uint32_t keys[16];
    int      n    = 0;
    uint32_t mods = 0;
    for (int i = 1; i < argc && n < 16; ++i) {
        if (!strcmp(argv[i], "-m") && i + 1 < argc)
            mods = atoi(argv[++i]);
        else
            keys[n++] = atoi(argv[i]);
    }
    (void)key;

    for (int i = 0; i < n; ++i) {
        zwp_virtual_keyboard_v1_key(kb, now_ms(), keys[i], WL_KEYBOARD_KEY_STATE_PRESSED);
        if (i == 0 && mods)
            zwp_virtual_keyboard_v1_modifiers(kb, mods, 0, 0, 0);
        wl_display_roundtrip(dpy);
        usleep(30000);
    }
    usleep(50000);
    for (int i = n - 1; i >= 0; --i) {
        if (i == 0 && mods)
            zwp_virtual_keyboard_v1_modifiers(kb, 0, 0, 0, 0);
        zwp_virtual_keyboard_v1_key(kb, now_ms(), keys[i], WL_KEYBOARD_KEY_STATE_RELEASED);
        wl_display_roundtrip(dpy);
        usleep(30000);
    }

    zwp_virtual_keyboard_v1_destroy(kb);
    wl_display_roundtrip(dpy);
    wl_display_disconnect(dpy);
    return 0;
}
