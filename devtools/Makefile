# Dev-only input simulators for testing the overview without touching the
# real keyboard/mouse. Not part of the plugin.
#   ./vclick X Y W H move|click|down|up   (logical coords, W/H = layout size)
#   ./vkey EVDEV_KEYCODE                  (default xkb keymap, e.g. 1 = Esc)
all: vclick vkey

%-client-protocol.h: %.xml
	wayland-scanner client-header $< $@
%-protocol.c: %.xml
	wayland-scanner private-code $< $@

vclick: vclick.c wlr-virtual-pointer-unstable-v1-client-protocol.h wlr-virtual-pointer-unstable-v1-protocol.c
	gcc -O2 -o $@ vclick.c wlr-virtual-pointer-unstable-v1-protocol.c $(shell pkg-config --cflags --libs wayland-client)

vkey: vkey.c virtual-keyboard-unstable-v1-client-protocol.h virtual-keyboard-unstable-v1-protocol.c
	gcc -O2 -o $@ vkey.c virtual-keyboard-unstable-v1-protocol.c $(shell pkg-config --cflags --libs wayland-client xkbcommon)

clean:
	rm -f vclick vkey *-client-protocol.h *-protocol.c

.PHONY: all clean
