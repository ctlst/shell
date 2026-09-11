CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror
CPPFLAGS += $(shell pkg-config --cflags gtk4 gtk4-layer-shell-0 json-c epoxy glib-2.0)
LDLIBS += $(shell pkg-config --libs gtk4 gtk4-layer-shell-0 json-c epoxy glib-2.0) -lm

OUTPUT ?= ctlsthome

all: $(OUTPUT)

HOME_SRCS = ctlsthome.c home-config.c home-pager.c home-rotation.c home-widget-desc.c home-widget-runtime.c
HOME_HDRS = home-config.h home-pager.h home-rotation.h home-widget-desc.h home-widget-runtime.h home-edit-frame.h home-picker-host.h home-allocation-host.h home-launcher-host.h

$(OUTPUT): $(HOME_SRCS) $(HOME_HDRS) ../ctlst-runtime.h
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(HOME_SRCS) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(OUTPUT)

.PHONY: all clean
