PLUGIN_NAME = hyprmission

PKGS = pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon

CXXFLAGS = -O2 -shared -fPIC -std=c++2b --no-gnu-unique $(shell pkg-config --cflags $(PKGS))

SOURCES = src/main.cpp src/Overview.cpp

all: $(PLUGIN_NAME).so

$(PLUGIN_NAME).so: $(SOURCES)
	g++ $(CXXFLAGS) $(SOURCES) -o $(PLUGIN_NAME).so

clean:
	rm -f $(PLUGIN_NAME).so

.PHONY: all clean
