# Components and package contents

The `ctlst-shell` install target contains the shell's source-built programs,
configuration, integration scripts, documentation and artwork. External
applications and libraries are installed by the operating system.

## Source layout

| Component | Language / format | Location |
| --- | --- | --- |
| Home, pages, built-in widgets and widget host | C / GTK4 | `ctlsthome/` |
| App drawer | Python / GTK4 | `scripts/ctlstdrawer`, `scripts/ctlstdrawer-ui` |
| Settings | Python / GTK4 | `scripts/ctlst-settings` |
| Dock/charm | C / GTK4 | `ctlstdock/` |
| Workspace overview | C / GTK4 | `ctlstoverview/` |
| Quick Settings | C / GTK4 | `ctlstshade/` |
| Privacy screen | C / GTK4 | `ctlstlock/` |
| Gesture routing | C / libinput | `ctlst-gestured/` |
| Action feedback | C / GTK4 | `ctlstaction/` |
| Session, app launching, workspace and memory helpers | Python + shell | `session/`, `scripts/` |
| Sway configuration and defaults | Sway config, INI, JSON, YAML | `sway/`, `config/defaults/` |
| Themes and artwork | CSS, configuration, SVG, PNG | `themes/`, `wallpapers/`, root SVG files |
| Notes | Python + YAML | `scripts/ctlst-home-notes`, `widgets/notes.yaml` |
| Widget tools and examples | Python, YAML, NDJSON documentation | `agent/pi/skills/ctlst-app-builder/`, `examples/` |
| Build and validation | Make, shell, Python, C | `Makefile`, `dependencies/`, `tests/`, `vm/` |

The widget-tools directory contains scaffolding, validation and protocol
documentation, not an AI agent runtime. Processes are installed together as
one core package; `packages/components.toml` describes that package.

## Install layout

- `/usr/bin/`: native surfaces and public session, Settings, Notes and widget commands.
- `/usr/libexec/ctlst-shell/`: internal helpers and widget tools.
- `/usr/share/ctlst-shell/`: defaults, Sway configuration, themes, wallpapers,
  installed widget documentation and examples.
- `/usr/share/ctlst/widgets/`: packaged widget descriptors.
- `/usr/share/applications/` and `/usr/share/icons/`: desktop entries and icons.
- `/usr/share/wayland-sessions/`: CTLST login entry.
- `/usr/share/licenses/ctlst-shell/`: license and attribution notices.

The install target does not write user home directories or `/etc/sway/config`.
Use `make install DESTDIR=...` to inspect the payload before installing.

## External components

Sway, GTK, Waybar, Mako, libinput, fonts and other
[dependencies](../docs/DEPENDENCIES.md) retain their upstream identities and
licenses. CTLST supplies integration and styling, not copies of those programs.

Phone/SMS applications, file managers, editors, keyboards and game controllers
are not included. Kernels, firmware, modem/audio/camera integration and device
profiles are also outside this package. Configure compatible providers
separately; see [beta limitations](../BETA.md).

Memory helpers use kernel cgroups and systemd scopes. Defaults impose no CTLST
memory cap; pressure eviction and freezing are not implemented.
The privacy screen provides no authentication.
