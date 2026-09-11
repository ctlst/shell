CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror
CPPFLAGS += $(shell pkg-config --cflags gtk4 gtk4-layer-shell-0)
LDLIBS += $(shell pkg-config --libs gtk4 gtk4-layer-shell-0) -lm

OUTPUT ?= ctlstshade

all: $(OUTPUT)

$(OUTPUT): ctlstshade.c ../ctlst-runtime.h
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(OUTPUT)

.PHONY: all clean
