PLUGIN_NAME = hyprmission

PKGS = pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon

CXXFLAGS ?= -O2
PLUGIN_FLAGS = -shared -fPIC -std=c++2b --no-gnu-unique $(shell pkg-config --cflags $(PKGS))

CORE_SOURCES = $(wildcard src/core/*.cpp)
SOURCES      = src/main.cpp src/Overview.cpp $(CORE_SOURCES)
HEADERS      = $(wildcard src/*.hpp src/core/*.hpp)

all: $(PLUGIN_NAME).so

$(PLUGIN_NAME).so: $(SOURCES) $(HEADERS)
	g++ $(CXXFLAGS) $(PLUGIN_FLAGS) $(SOURCES) -o $@

# --- unit tests: the pure logic in src/core, GoogleTest, no compositor needed
TEST_SOURCES = $(wildcard tests/unit/*.cpp)
TEST_FLAGS   = -std=c++2b -O0 -g --coverage -Isrc $(shell pkg-config --cflags hyprutils gtest)
TEST_LIBS    = $(shell pkg-config --libs hyprutils gtest gtest_main)

build/unit_tests: $(CORE_SOURCES) $(TEST_SOURCES) $(HEADERS)
	@mkdir -p build
	@rm -f build/*.gcda
	g++ $(TEST_FLAGS) $(CORE_SOURCES) $(TEST_SOURCES) -o $@ $(TEST_LIBS)

test: build/unit_tests
	./build/unit_tests

# needs gcovr; writes build/coverage.{txt,json,html}
coverage: test
	gcovr --root . --filter 'src/core/' build \
		--txt build/coverage.txt --json-summary build/coverage.json --html-details build/coverage.html \
		--print-summary

# --- integration tests: a real (nested) Hyprland with the plugin loaded.
# Needs a running Wayland session with a GPU, so it can't run on CI runners.
integration: all
	$(MAKE) -C devtools
	python3 tests/integration/run.py

clean:
	rm -rf $(PLUGIN_NAME).so build

.PHONY: all test coverage integration clean
