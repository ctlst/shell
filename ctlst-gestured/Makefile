CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror
CPPFLAGS += $(shell pkg-config --cflags json-c libinput)
LDLIBS += $(shell pkg-config --libs json-c libinput) -lm

OUTPUT ?= ctlst-gestured

all: $(OUTPUT)

$(OUTPUT): ctlst-gestured.c
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(OUTPUT)

.PHONY: all clean
