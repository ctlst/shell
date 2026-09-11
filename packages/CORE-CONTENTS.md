# What CTLST Shell actually contains

The core package contains CTLST-authored shell code, configuration, integration
scripts, documentation and project-created artwork. It is not a repackaged
Sway distribution. Dependency applications and libraries retain their upstream
identity and are installed by the operating system, not copied into this package.

## Repository inventory

One source repository and one core build/install target. Separate processes do
not imply separately installed packages.

| Included component | Language / format | Source location |
| --- | --- | --- |
| Home launcher, pages, built-in widgets and widget host | C / GTK4 | `ctlsthome/` |
| App drawer | Python / GTK4 | `scripts/ctlstdrawer*` |
| Settings | Python / GTK4 | `scripts/ctlst-settings` |
| Dock/charm | C / GTK4 | `ctlstdock/` |
| Multitasking overview | C / GTK4 | `ctlstoverview/` |
| Quick Settings shade | C / GTK4 | `ctlstshade/` |
| Swipe-away privacy cover (not a secure lock) | C / GTK4 | `ctlstlock/` |
| Gesture daemon | C / libinput | `ctlst-gestured/` |
| Action surface | C / GTK4 | `ctlstaction/` |
| Login entry, startup, app/task helpers and memory limits | Python + POSIX shell | `session/`, `scripts/` |
| CTLST Sway configuration, defaults and integration | Sway config, INI, JSON, YAML | `sway/`, `config/defaults/` |
| Themes, Waybar/Mako styling, original icons and wallpapers | CSS, configuration, SVG, PNG | `themes/`, `wallpapers/`, root SVGs |
| Notes widget and external-editor launcher | Python + YAML | `scripts/ctlst-home-notes`, `widgets/notes.yaml` |
| No-recompile widget tools, protocol and examples | Python + NDJSON/YAML documentation | `agent/pi/skills/ctlst-app-builder/`, `examples/` |
| Builds, core dependency manifests, tests and VM fixtures | Make, shell, Python, C | `Makefile`, `dependencies/`, `tests/`, `vm/` |
| License, attribution, setup, customization and release evidence | Markdown/text | root documents, `docs/`, `release/` |

The widget-tools path is historical naming, not a bundled agent runtime.
Fixtures are test source, not prebuilt executables or phone/VM images. The
component map declares only the package actually present in this repository.

## Original CTLST work in core

- Home/launcher, drawer, dock/charm, overview, Quick Settings, action surface,
  privacy/swipe surface, and their task/workspace coordination.
- `ctlst-gestured`: CTLST's native gesture router, separate from the retained
  historical lisgd patch. It uses upstream libinput; CTLST did not create libinput.
- CTLST's Sway session configuration, startup/config resolver, Waybar styling
  and integration, notification-history integration, app launching and settings.
- Configurable memory/lifecycle integration using existing kernel cgroups and
  systemd scopes. CTLST did not create those mechanisms. Generic limits are
  zero; portable pressure eviction/freezing is not implemented.
- Theme definitions, project-created wallpaper/icon assets, widget host/tools,
  working plain-text Notes widget/editor launcher, Pocket Note authoring example
  and provider-neutral instructions. Text editors are external dependencies;
  Notes does not bundle or claim ownership of them.

These are project-owned implementations according to the current source and
provenance audit, not a claim that CTLST invented the underlying APIs or that
every historical experimental file was authored from scratch.

## Upstream requirements, never bundled or renamed

Sway, Waybar, GTK/gtk4-layer-shell, libinput, Python, Foot, Mako, PipeWire,
portal backends, system fonts/icon themes and other declared dependencies come
from the system. Runtime manifests in `dependencies/` are authoritative.
Symbols Nerd Font replaces the formerly bundled renamed MDI font subset.
No font binary, keyboard binary, upstream program source, shared library,
firmware or kernel belongs in the CTLST core payload.

## Separate packages / repositories

- Stock wvkbd or another compatible on-screen keyboard is an external provider.
  Stock wvkbd integration must be tested before declaring it a working phone
  requirement. It is not silently substituted for the Pixel fork today.
- The modified wvkbd build remains a separately attributed upstream derivative,
  with its own source/patches/notices, never part of core.
- CTLST-authored dialer/messages/files/gamepad applications are companion
  packages, not implicit shell contents.
- The Pixel repository owns the complete supported Arch/Pixel 3a XL system:
  device profiles, kernel, power/audio/modem/camera integration and deployment.

The private development extraction retains historical/optional files and their
notices. The core-only source snapshot excludes those files and starts a new
Git root; it must never acquire the private extraction history. Source review
and installation acceptance are separate gates. See `BETA.md` and the snapshot's
`release/VALIDATION.md` for what has and has not been accepted.
