# What CTLST Shell actually contains

The core package contains CTLST-authored shell code, configuration, integration
scripts, documentation and project-created artwork. It is not a repackaged
Sway distribution. Dependency applications and libraries retain their upstream
identity and are installed by the operating system, not copied into this package.

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
