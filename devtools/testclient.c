// Dev-only test client: a plain xdg-shell window for the integration tests.
//   testclient [--title T] [--keylog FILE] [--animate] [--color RRGGBB]
// --keylog: append "press <key>" / "release <key>" for every key it receives
// --animate: repaint with a new color on every frame callback, so it only
//            changes if the compositor keeps sending it frame events
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

static struct wl_compositor* compositor;
static struct wl_shm*        shm;
static struct xdg_wm_base*   wmBase;
static struct wl_seat*       seat;
static struct wl_surface*    surface;
static FILE*                 keylog;
static bool                  animate;
static int                   width = 320, height = 240;
static uint32_t              frameNo;
static uint32_t              color = 0xff808080;
static bool                  running = true;

static void                  draw(void);

static void                  noop() {}

static void global_add(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t ver) {
    if (!strcmp(iface, wl_compositor_interface.name))
        compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, wl_shm_interface.name))
        shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, xdg_wm_base_interface.name))
        wmBase = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name))
        seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
}
static const struct wl_registry_listener regListener = {global_add, noop};

static void wmPing(void* d, struct xdg_wm_base* b, uint32_t serial) {
    xdg_wm_base_pong(b, serial);
}
static const struct xdg_wm_base_listener wmListener = {wmPing};

static void surfaceConfigure(void* d, struct xdg_surface* s, uint32_t serial) {
    xdg_surface_ack_configure(s, serial);
    draw();
}
static const struct xdg_surface_listener surfaceListener = {surfaceConfigure};

static void toplevelConfigure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h, struct wl_array* states) {
    if (w > 0 && h > 0) {
        width  = w;
        height = h;
    }
}
static void toplevelClose(void* d, struct xdg_toplevel* t) {
    running = false;
}
static const struct xdg_toplevel_listener toplevelListener = {toplevelConfigure, toplevelClose, noop, noop};

static void frameDone(void* d, struct wl_callback* cb, uint32_t time);
static const struct wl_callback_listener frameListener = {frameDone};

static void frameDone(void* d, struct wl_callback* cb, uint32_t time) {
    wl_callback_destroy(cb);
    frameNo++;
    draw();
}

static void draw(void) {
    const int stride = width * 4, size = stride * height;
    int       fd     = memfd_create("buf", 0);
    ftruncate(fd, size);
    uint32_t* px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    // animate: cycle through clearly different colors every frame
    static const uint32_t COLORS[] = {0xffd03030, 0xff30d030, 0xff3030d0, 0xffd0d030};
    const uint32_t        c        = animate ? COLORS[frameNo % 4] : color;
    for (int i = 0; i < width * height; ++i)
        px[i] = c;
    munmap(px, size);

    struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer*   buf  = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    wl_surface_attach(surface, buf, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, width, height);
    if (animate)
        wl_callback_add_listener(wl_surface_frame(surface), &frameListener, NULL);
    wl_surface_commit(surface);
}

static void kbKey(void* d, struct wl_keyboard* k, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    if (!keylog)
        return;
    fprintf(keylog, "%s %u\n", state == WL_KEYBOARD_KEY_STATE_PRESSED ? "press" : "release", key);
    fflush(keylog);
}
static const struct wl_keyboard_listener kbListener = {noop, noop, noop, kbKey, noop, noop};

int main(int argc, char** argv) {
    const char* title = "hyprmission-test";
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--title") && i + 1 < argc)
            title = argv[++i];
        else if (!strcmp(argv[i], "--keylog") && i + 1 < argc)
            keylog = fopen(argv[++i], "a");
        else if (!strcmp(argv[i], "--animate"))
            animate = true;
        else if (!strcmp(argv[i], "--color") && i + 1 < argc)
            color = 0xff000000 | (uint32_t)strtoul(argv[++i], NULL, 16);
    }

    struct wl_display* dpy = wl_display_connect(NULL);
    if (!dpy)
        return 1;
    struct wl_registry* reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &regListener, NULL);
    wl_display_roundtrip(dpy);
    if (!compositor || !shm || !wmBase)
        return 1;

    xdg_wm_base_add_listener(wmBase, &wmListener, NULL);
    if (seat && keylog)
        wl_keyboard_add_listener(wl_seat_get_keyboard(seat), &kbListener, NULL);

    surface                         = wl_compositor_create_surface(compositor);
    struct xdg_surface*  xdgSurface = xdg_wm_base_get_xdg_surface(wmBase, surface);
    struct xdg_toplevel* toplevel   = xdg_surface_get_toplevel(xdgSurface);
    xdg_surface_add_listener(xdgSurface, &surfaceListener, NULL);
    xdg_toplevel_add_listener(toplevel, &toplevelListener, NULL);
    xdg_toplevel_set_title(toplevel, title);
    xdg_toplevel_set_app_id(toplevel, "hyprmission-test");
    wl_surface_commit(surface);

    while (running && wl_display_dispatch(dpy) != -1) {}
    return 0;
}
