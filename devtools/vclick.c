// Dev-only test tool: move the pointer to absolute logical coords and
// optionally left-click, via zwlr_virtual_pointer_v1.
// usage: vclick X Y OUTPUT_W OUTPUT_H [move|click|down|up]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

static struct wl_seat*                               seat;
static struct zwlr_virtual_pointer_manager_v1*       mgr;

static void global_add(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t ver) {
    if (!strcmp(iface, wl_seat_interface.name))
        seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
    else if (!strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name))
        mgr = wl_registry_bind(r, name, &zwlr_virtual_pointer_manager_v1_interface, 1);
}
static void global_rm(void* d, struct wl_registry* r, uint32_t name) {}
static const struct wl_registry_listener reg_listener = {global_add, global_rm};

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(int argc, char** argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: vclick X Y W H move|click|down|up\n");
        return 1;
    }
    uint32_t x = atoi(argv[1]), y = atoi(argv[2]), w = atoi(argv[3]), h = atoi(argv[4]);
    const char* action = argv[5];

    struct wl_display* dpy = wl_display_connect(NULL);
    if (!dpy) {
        fprintf(stderr, "no display\n");
        return 1;
    }
    struct wl_registry* reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &reg_listener, NULL);
    wl_display_roundtrip(dpy);
    if (!mgr) {
        fprintf(stderr, "no virtual pointer manager\n");
        return 1;
    }

    struct zwlr_virtual_pointer_v1* vp = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(mgr, seat);

    zwlr_virtual_pointer_v1_motion_absolute(vp, now_ms(), x, y, w, h);
    zwlr_virtual_pointer_v1_frame(vp);
    wl_display_roundtrip(dpy);
    usleep(50000);

    // drag X Y W H drag X2 Y2 HOLD_MS: press at X,Y, glide to X2,Y2, wait, release
    if (!strcmp(action, "drag") && argc >= 9) {
        uint32_t x2 = atoi(argv[6]), y2 = atoi(argv[7]), hold = atoi(argv[8]);
        zwlr_virtual_pointer_v1_button(vp, now_ms(), BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
        zwlr_virtual_pointer_v1_frame(vp);
        wl_display_roundtrip(dpy);
        for (int i = 1; i <= 20; ++i) {
            zwlr_virtual_pointer_v1_motion_absolute(vp, now_ms(), x + ((int)x2 - (int)x) * i / 20, y + ((int)y2 - (int)y) * i / 20, w, h);
            zwlr_virtual_pointer_v1_frame(vp);
            wl_display_roundtrip(dpy);
            usleep(20000);
        }
        usleep(hold * 1000);
        zwlr_virtual_pointer_v1_button(vp, now_ms(), BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
        zwlr_virtual_pointer_v1_frame(vp);
        wl_display_roundtrip(dpy);
    }

    if (!strcmp(action, "click") || !strcmp(action, "down")) {
        zwlr_virtual_pointer_v1_button(vp, now_ms(), BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
        zwlr_virtual_pointer_v1_frame(vp);
        wl_display_roundtrip(dpy);
        usleep(50000);
    }
    if (!strcmp(action, "click") || !strcmp(action, "up")) {
        zwlr_virtual_pointer_v1_button(vp, now_ms(), BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
        zwlr_virtual_pointer_v1_frame(vp);
        wl_display_roundtrip(dpy);
    }

    zwlr_virtual_pointer_v1_destroy(vp);
    wl_display_roundtrip(dpy);
    wl_display_disconnect(dpy);
    return 0;
}
