# antirazer — build naga-mapper, naga-remap and naga-ui
#
#   make              build everything
#   make test         run the unit tests
#   make tools        build the research tools
#   make install      install into $(DESTDIR)$(PREFIX)
#
# libusb-1.0, libevdev and GLFW come from the system (or the Nix dev shell).
# cJSON and cimgui/ImGui are vendored under vendor/ and built from source.

PREFIX     ?= /usr/local
UDEVDIR    ?= $(PREFIX)/lib/udev/rules.d
BUILD      ?= build

CC         ?= cc
CXX        ?= c++
PKG_CONFIG ?= pkg-config

CPPFLAGS   += -D_DEFAULT_SOURCE -Isrc
CFLAGS     ?= -O2 -g
CFLAGS     += -std=c11 -Wall -Wextra -Wpedantic -MMD -MP
CXXFLAGS   ?= -O2 -g
CXXFLAGS   += -std=c++17 -Wall -Wextra -MMD -MP

# Third-party code is built without our warning set.
VENDOR_CFLAGS   := -O2 -g -std=c11
VENDOR_CXXFLAGS := -O2 -g -std=c++17

LIBUSB_CFLAGS   := $(shell $(PKG_CONFIG) --cflags libusb-1.0)
LIBUSB_LIBS     := $(shell $(PKG_CONFIG) --libs libusb-1.0)
LIBEVDEV_CFLAGS := $(shell $(PKG_CONFIG) --cflags libevdev)
LIBEVDEV_LIBS   := $(shell $(PKG_CONFIG) --libs libevdev)
GLFW_CFLAGS     := $(shell $(PKG_CONFIG) --cflags glfw3)
GLFW_LIBS       := $(shell $(PKG_CONFIG) --libs glfw3)

CJSON_CFLAGS := -Ivendor/cjson
CJSON_OBJS   := $(BUILD)/cjson.o

CIMGUI_DIR      := vendor/cimgui
CIMGUI_CFLAGS   := -I$(CIMGUI_DIR) -I$(CIMGUI_DIR)/imgui \
                   -I$(CIMGUI_DIR)/imgui/backends $(GLFW_CFLAGS) \
                   -DGLFW_INCLUDE_NONE
UI_CFLAGS       := -I$(CIMGUI_DIR) -I$(CIMGUI_DIR)/imgui \
                   -I$(CIMGUI_DIR)/imgui/backends $(GLFW_CFLAGS) \
                   -DGLFW_INCLUDE_NONE

CIMGUI_SRCS := \
	$(CIMGUI_DIR)/cimgui.cpp \
	$(CIMGUI_DIR)/imgui/imgui.cpp \
	$(CIMGUI_DIR)/imgui/imgui_draw.cpp \
	$(CIMGUI_DIR)/imgui/imgui_tables.cpp \
	$(CIMGUI_DIR)/imgui/imgui_widgets.cpp \
	$(CIMGUI_DIR)/imgui/imgui_demo.cpp \
	$(CIMGUI_DIR)/imgui/backends/imgui_impl_glfw.cpp \
	$(CIMGUI_DIR)/imgui/backends/imgui_impl_opengl3.cpp
CIMGUI_OBJS := $(patsubst $(CIMGUI_DIR)/%.cpp,$(BUILD)/cimgui/%.o,$(CIMGUI_SRCS))

CORE_OBJS := $(BUILD)/naga.o $(BUILD)/naga_keys.o $(BUILD)/naga_ctrls.o

REMAP_OBJS := $(BUILD)/remap_main.o $(BUILD)/remap_map.o \
              $(BUILD)/remap_plan.o $(BUILD)/remap_run.o \
              $(BUILD)/remap_arena.o $(BUILD)/remap_config.o

TOOLS := naga-probe evtap bench-remap

all: naga-mapper naga-remap naga-ui

$(BUILD):
	mkdir -p $(BUILD)

# ---- vendored libraries ---------------------------------------------------

$(BUILD)/cjson.o: vendor/cjson/cJSON.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(VENDOR_CFLAGS) $(CJSON_CFLAGS) -c $< -o $@

$(BUILD)/cimgui/%.o: $(CIMGUI_DIR)/%.cpp | $(BUILD)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(VENDOR_CXXFLAGS) $(CIMGUI_CFLAGS) -c $< -o $@

# ---- device core ----------------------------------------------------------

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIBUSB_CFLAGS) -c $< -o $@

# ---- CLI ------------------------------------------------------------------

$(BUILD)/mapper_main.o: src/mapper/main.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

naga-mapper: $(CORE_OBJS) $(BUILD)/mapper_main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBUSB_LIBS)

# ---- naga-remap -----------------------------------------------------------

$(BUILD)/remap_arena.o: src/remap/arena.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/remap_config.o: src/remap/config.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/remap_map.o: src/remap/map.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CJSON_CFLAGS) -c $< -o $@

$(BUILD)/remap_plan.o: src/remap/plan.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/remap_run.o: src/remap/run.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIBEVDEV_CFLAGS) -c $< -o $@

$(BUILD)/remap_main.o: src/remap/main.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CJSON_CFLAGS) -c $< -o $@

naga-remap: $(CORE_OBJS) $(REMAP_OBJS) $(CJSON_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBUSB_LIBS) $(LIBEVDEV_LIBS) -lm

# ---- GUI ------------------------------------------------------------------

$(BUILD)/ui_main.o: src/ui/main.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UI_CFLAGS) -c $< -o $@

$(BUILD)/ui_state.o: src/ui/ui_state.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UI_CFLAGS) -c $< -o $@

$(BUILD)/ui_layout.o: src/ui/ui_layout.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UI_CFLAGS) -c $< -o $@

$(BUILD)/ui_keys.o: src/ui/ui_keys.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UI_CFLAGS) -c $< -o $@

$(BUILD)/cbridge.o: src/ui/cbridge.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(UI_CFLAGS) -c $< -o $@

naga-ui: $(CORE_OBJS) $(BUILD)/ui_main.o $(BUILD)/ui_state.o \
         $(BUILD)/ui_layout.o $(BUILD)/ui_keys.o $(BUILD)/cbridge.o \
         $(BUILD)/remap_config.o $(CIMGUI_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(GLFW_LIBS) $(LIBUSB_LIBS) -lm -lpthread -ldl

# ---- tests ----------------------------------------------------------------

$(BUILD)/test_keys: tests/test_keys.c $(BUILD)/naga_keys.o $(BUILD)/naga_ctrls.o
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^

$(BUILD)/test_remap: tests/test_remap.c $(BUILD)/remap_map.o \
                     $(BUILD)/remap_plan.o $(BUILD)/remap_arena.o \
                     $(CJSON_OBJS) $(CORE_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LIBUSB_LIBS) -lm

$(BUILD)/test_ui_queue: tests/test_ui_queue.c $(BUILD)/ui_state.o \
                        $(BUILD)/remap_config.o $(CORE_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LIBUSB_LIBS)

$(BUILD)/test_ui_keys: tests/test_ui_keys.c $(BUILD)/ui_keys.o \
                       $(BUILD)/naga_keys.o $(BUILD)/naga_ctrls.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UI_CFLAGS) -o $@ $^ $(GLFW_LIBS)

test: $(BUILD)/test_keys $(BUILD)/test_remap $(BUILD)/test_ui_queue \
      $(BUILD)/test_ui_keys
	./$(BUILD)/test_keys
	./$(BUILD)/test_remap
	./$(BUILD)/test_ui_queue
	./$(BUILD)/test_ui_keys

# ---- research tools -------------------------------------------------------

tools: $(TOOLS)

naga-probe: tools/naga-probe.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIBUSB_CFLAGS) -o $@ $< $(LIBUSB_LIBS)

evtap: tools/evtap.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

bench-remap: tools/bench-remap.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIBEVDEV_CFLAGS) -o $@ $< $(LIBEVDEV_LIBS)

# ---- install --------------------------------------------------------------

install: naga-mapper naga-remap naga-ui
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(UDEVDIR)
	install -m755 naga-mapper $(DESTDIR)$(PREFIX)/bin/
	install -m755 naga-remap $(DESTDIR)$(PREFIX)/bin/
	install -m755 naga-ui $(DESTDIR)$(PREFIX)/bin/
	install -m644 udev/99-naga.rules $(DESTDIR)$(UDEVDIR)/99-naga.rules

clean:
	rm -rf $(BUILD) naga-mapper naga-ui naga-remap $(TOOLS)

.PHONY: all clean install test tools naga-ui

-include $(wildcard $(BUILD)/*.d) $(wildcard $(BUILD)/cimgui/*.d) $(wildcard $(BUILD)/cimgui/imgui/*.d) $(wildcard $(BUILD)/cimgui/imgui/backends/*.d)
