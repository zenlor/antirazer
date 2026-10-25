# antirazer — build naga-mapper (CLI) and naga-ui (SDL3 + cimgui)
#
#   make              build the CLI; also the GUI when CIMGUI is set
#   make naga-ui      build the GUI only
#   make test         run the key-parsing unit tests
#   make install      install into $(DESTDIR)$(PREFIX)
#
# Inside `nix develop` everything is preconfigured. Outside it, point CIMGUI
# at a cimgui install prefix (a static lib/libcimgui.a plus include/), e.g.
#   make CIMGUI=/usr/local

CIMGUI     ?=
PREFIX     ?= /usr/local
UDEVDIR    ?= $(PREFIX)/lib/udev/rules.d
BUILD      ?= build

CC         ?= cc
CXX        ?= c++
PKG_CONFIG ?= pkg-config

CPPFLAGS   += -D_DEFAULT_SOURCE -Isrc
CFLAGS     ?= -O2 -g
CFLAGS     += -std=c11 -Wall -Wextra -Wpedantic
CXXFLAGS   ?= -O2 -g
CXXFLAGS   += -std=c++17 -Wall -Wextra

LIBUSB_CFLAGS := $(shell $(PKG_CONFIG) --cflags libusb-1.0)
LIBUSB_LIBS   := $(shell $(PKG_CONFIG) --libs libusb-1.0)

ifneq ($(CIMGUI),)
UI_CPPFLAGS := \
	-I$(CIMGUI)/include -I$(CIMGUI)/include/imgui \
	-I$(CIMGUI)/include/imgui/backends \
	$(shell $(PKG_CONFIG) --cflags sdl3)
UI_LIBS := $(CIMGUI)/lib/libcimgui.a $(shell $(PKG_CONFIG) --libs sdl3) $(LIBUSB_LIBS)
endif

# Device core, shared by both frontends.
CORE_OBJS := $(BUILD)/naga.o $(BUILD)/naga_keys.o $(BUILD)/naga_ctrls.o

TOOLS := naga-probe evtap

all: naga-mapper $(if $(CIMGUI),naga-ui)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIBUSB_CFLAGS) -c $< -o $@

# ---- CLI ------------------------------------------------------------------

$(BUILD)/mapper_main.o: src/mapper/main.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

naga-mapper: $(CORE_OBJS) $(BUILD)/mapper_main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBUSB_LIBS)

# ---- GUI ------------------------------------------------------------------

ifneq ($(CIMGUI),)

$(BUILD)/ui_main.o: src/ui/main.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UI_CPPFLAGS) -c $< -o $@

$(BUILD)/ui_state.o: src/ui/ui_state.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UI_CPPFLAGS) -c $< -o $@

$(BUILD)/ui_layout.o: src/ui/ui_layout.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UI_CPPFLAGS) -c $< -o $@

$(BUILD)/cbridge.o: src/ui/cbridge.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(UI_CPPFLAGS) -c $< -o $@

naga-ui: $(CORE_OBJS) $(BUILD)/ui_main.o $(BUILD)/ui_state.o \
         $(BUILD)/ui_layout.o $(BUILD)/cbridge.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(UI_LIBS)

else

naga-ui:
	@echo "naga-ui: CIMGUI is not set." >&2
	@echo "  run inside 'nix develop', or pass CIMGUI=<cimgui prefix>." >&2
	@exit 1

endif

# ---- tests ----------------------------------------------------------------

$(BUILD)/test_keys: tests/test_keys.c $(BUILD)/naga_keys.o $(BUILD)/naga_ctrls.o
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^

test: $(BUILD)/test_keys
	./$(BUILD)/test_keys

# ---- research tools -------------------------------------------------------

tools: $(TOOLS)

naga-probe: tools/naga-probe.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIBUSB_CFLAGS) -o $@ $< $(LIBUSB_LIBS)

evtap: tools/evtap.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

# ---- install --------------------------------------------------------------

install: naga-mapper $(if $(CIMGUI),naga-ui)
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(UDEVDIR)
	install -m755 naga-mapper $(DESTDIR)$(PREFIX)/bin/
	$(if $(CIMGUI),install -m755 naga-ui $(DESTDIR)$(PREFIX)/bin/)
	install -m644 udev/99-naga.rules $(DESTDIR)$(UDEVDIR)/99-naga.rules

clean:
	rm -rf $(BUILD) naga-mapper naga-ui $(TOOLS)

.PHONY: all clean install test tools naga-ui
