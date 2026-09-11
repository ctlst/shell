CC ?= cc
CFLAGS ?= -O2
PKG_MODULES := gtk4 gtk4-layer-shell-0 json-c gio-unix-2.0 cairo
OUTPUT ?= ctlstdock

CFLAGS += -std=c11 -Wall -Wextra -Werror
CPPFLAGS += $(shell pkg-config --cflags $(PKG_MODULES))
LDLIBS += $(shell pkg-config --libs $(PKG_MODULES)) -lm

.PHONY: all clean

all: $(OUTPUT)

$(OUTPUT): ctlstdock.c ../ctlst-runtime.h
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(OUTPUT)
