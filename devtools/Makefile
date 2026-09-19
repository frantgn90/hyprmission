# Dev-only input simulators for testing the overview without touching the
# real keyboard/mouse. Not part of the plugin.
#   ./vclick X Y W H move|click|down|up   (logical coords, W/H = layout size)
#   ./vkey EVDEV_KEYCODE                  (default xkb keymap, e.g. 1 = Esc)
#   ./testclient [--title T] [--keylog FILE] [--animate]   (xdg-shell window)
XDG_SHELL = $(shell pkg-config --variable=pkgdatadir wayland-protocols)/stable/xdg-shell/xdg-shell.xml

all: vclick vkey testclient

xdg-shell-client-protocol.h:
	wayland-scanner client-header $(XDG_SHELL) $@
xdg-shell-protocol.c:
	wayland-scanner private-code $(XDG_SHELL) $@

%-client-protocol.h: %.xml
	wayland-scanner client-header $< $@
%-protocol.c: %.xml
	wayland-scanner private-code $< $@

vclick: vclick.c wlr-virtual-pointer-unstable-v1-client-protocol.h wlr-virtual-pointer-unstable-v1-protocol.c
	gcc -O2 -o $@ vclick.c wlr-virtual-pointer-unstable-v1-protocol.c $(shell pkg-config --cflags --libs wayland-client)

vkey: vkey.c virtual-keyboard-unstable-v1-client-protocol.h virtual-keyboard-unstable-v1-protocol.c
	gcc -O2 -o $@ vkey.c virtual-keyboard-unstable-v1-protocol.c $(shell pkg-config --cflags --libs wayland-client xkbcommon)

testclient: testclient.c xdg-shell-client-protocol.h xdg-shell-protocol.c
	gcc -std=gnu17 -O2 -o $@ testclient.c xdg-shell-protocol.c $(shell pkg-config --cflags --libs wayland-client)

clean:
	rm -f vclick vkey testclient *-client-protocol.h *-protocol.c

.PHONY: all clean
