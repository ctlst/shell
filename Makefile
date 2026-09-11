BUILD_DIR ?= .build
PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
LIBEXECDIR ?= $(PREFIX)/libexec/ctlst-shell
DATADIR ?= $(PREFIX)/share/ctlst-shell
SESSIONDIR ?= $(PREFIX)/share/wayland-sessions
LICENSEDIR ?= $(PREFIX)/share/licenses/ctlst-shell
CORE_BIN_DIR := $(abspath $(BUILD_DIR)/bin)
CORE_COMPONENTS := ctlst-gestured ctlstaction ctlstdock ctlsthome ctlstlock ctlstoverview ctlstshade
CORE_BINARIES := $(addprefix $(CORE_BIN_DIR)/,$(CORE_COMPONENTS))
CORE_LIBEXEC := \
	ctlst-control \
	ctlst-component \
	app-run \
	capture-session-env \
	clamp-floating \
	close-active \
	ctlst-memory-status \
	ctlst-preferences \
	ctlst-settings \
	ctlst-theme \
	ctlst-theme-render \
	ctlst-wallpaper \
	ctlst-workspaced \
	ctlstdrawer \
	ctlstdrawer-ui \
	ctlsthome-daemon \
	ctlstlock \
	ctlstshade \
	dock-status \
	floating-window-guard \
	notification-history \
	screen-idle \
	screen-idle-inhibit \
	screenshot \
	session-menu \
	terminal-shell \
	toggle-float-safe \
	waybar-daemon \
	window-drop-overlay \
	workspace-button \
	workspace-card-cache \
	workspace-step \
	workspace-transients-close

.PHONY: all core install check clean $(CORE_BINARIES)

all: core

core: $(CORE_BINARIES)

$(CORE_BIN_DIR)/ctlst-gestured:
	$(MAKE) -C ctlst-gestured OUTPUT=$@

$(CORE_BIN_DIR)/ctlstaction:
	$(MAKE) -C ctlstaction OUTPUT=$@

$(CORE_BIN_DIR)/ctlstdock:
	$(MAKE) -C ctlstdock OUTPUT=$@

$(CORE_BIN_DIR)/ctlsthome:
	$(MAKE) -C ctlsthome OUTPUT=$@

$(CORE_BIN_DIR)/ctlstlock:
	$(MAKE) -C ctlstlock OUTPUT=$@

$(CORE_BIN_DIR)/ctlstoverview:
	$(MAKE) -C ctlstoverview OUTPUT=$@

$(CORE_BIN_DIR)/ctlstshade:
	$(MAKE) -C ctlstshade OUTPUT=$@

install: core install-widget-tools
	mkdir -p "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(LIBEXECDIR)"
	mkdir -p "$(DESTDIR)$(DATADIR)/defaults" "$(DESTDIR)$(DATADIR)/sway"
	mkdir -p "$(DESTDIR)$(DATADIR)/themes" "$(DESTDIR)$(DATADIR)/wallpapers"
	mkdir -p "$(DESTDIR)$(SESSIONDIR)" "$(DESTDIR)$(LICENSEDIR)"
	install -m 0755 $(CORE_BINARIES) "$(DESTDIR)$(BINDIR)/"
	install -m 0755 session/ctlst-session "$(DESTDIR)$(BINDIR)/ctlst-session"
	install -m 0644 scripts/ctlst_config.py "$(DESTDIR)$(LIBEXECDIR)/"
	install -m 0755 session/ctlst-settings "$(DESTDIR)$(BINDIR)/ctlst-settings"
	mkdir -p "$(DESTDIR)$(PREFIX)/share/applications" "$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps"
	install -m 0644 dev.ctlst.Settings.desktop "$(DESTDIR)$(PREFIX)/share/applications/"
	install -m 0644 dev.ctlst.Settings.svg "$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/"
	install -m 0644 dev.ctlst.Apps.svg "$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/"
	for script in $(CORE_LIBEXEC); do \
		install -m 0755 "scripts/$$script" "$(DESTDIR)$(LIBEXECDIR)/$$script"; \
	done
	install -m 0644 config/defaults/* "$(DESTDIR)$(DATADIR)/defaults/"
	install -m 0644 sway/ctlst.conf "$(DESTDIR)$(DATADIR)/sway/ctlst.conf"
	cp -R themes/. "$(DESTDIR)$(DATADIR)/themes/"
	cp -R wallpapers/. "$(DESTDIR)$(DATADIR)/wallpapers/"
	install -m 0644 session/ctlst.desktop "$(DESTDIR)$(SESSIONDIR)/ctlst.desktop"
	install -m 0644 LICENSE NOTICE THIRD_PARTY.md "$(DESTDIR)$(LICENSEDIR)/"
	install -m 0644 packages/CORE-CONTENTS.md "$(DESTDIR)$(LICENSEDIR)/"

.PHONY: install-widget-tools
install-widget-tools:
	mkdir -p "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(LIBEXECDIR)/widget-tools"
	mkdir -p "$(DESTDIR)$(PREFIX)/share/ctlst/widgets" "$(DESTDIR)$(PREFIX)/share/applications"
	install -m 0755 scripts/ctlst-home-notes "$(DESTDIR)$(BINDIR)/"
	install -m 0644 widgets/notes.yaml "$(DESTDIR)$(PREFIX)/share/ctlst/widgets/"
	install -m 0644 dev.ctlst.Notes.desktop "$(DESTDIR)$(PREFIX)/share/applications/"
	mkdir -p "$(DESTDIR)$(DATADIR)/docs" "$(DESTDIR)$(DATADIR)/examples/pocket-note"
	install -m 0755 session/ctlst-widget "$(DESTDIR)$(BINDIR)/"
	install -m 0755 scripts/ctlst-widget-reload "$(DESTDIR)$(LIBEXECDIR)/"
	for tool in ctlst-widget new_widget.py validate_widget.py; do \
		install -m 0755 "agent/pi/skills/ctlst-app-builder/scripts/$$tool" "$(DESTDIR)$(LIBEXECDIR)/widget-tools/"; \
	done
	install -m 0644 docs/WIDGETS.md "$(DESTDIR)$(DATADIR)/docs/"
	install -m 0644 docs/NOTES.md "$(DESTDIR)$(DATADIR)/docs/"
	install -m 0644 agent/pi/skills/ctlst-app-builder/references/widget-protocol-v1.md "$(DESTDIR)$(DATADIR)/docs/"
	install -m 0644 examples/pocket-note/pocket-note.yaml "$(DESTDIR)$(DATADIR)/examples/pocket-note/"
	install -m 0755 examples/pocket-note/ctlst-widget-pocket-note "$(DESTDIR)$(DATADIR)/examples/pocket-note/"

check:
	python3 -m pytest -q tests
	./session/ctlst-session config check

clean:
	find "$(abspath $(BUILD_DIR))" -type f -delete 2>/dev/null || true
	find "$(abspath $(BUILD_DIR))" -depth -type d -empty -delete 2>/dev/null || true
