# Platform support and beta status

CTLST Shell is a developer beta for Sway-based Linux sessions.

| Target | Status |
| --- | --- |
| Arch Linux ARM on Pixel 3a XL | Reference device; hardware services are configured separately. |
| Arch Linux ARM, AArch64 VM | Native build and installed-session functional tests pass. |
| Alpine/postmarketOS | Dependency manifests provided; current release validation is incomplete. |
| Other Linux phones and tablets | Experimental; requires working Sway, input and device services. |
| Hyprland and Ubuntu Touch sessions | Not validated. |

## Component status

| Component | Available functionality | Limitations |
| --- | --- | --- |
| Home | App grid, pages, editing, widget movement/resizing and responsive rotation layout | Direct keyboard editing and removal are incomplete. |
| Drawer | App discovery, search, launch, Home placement and Hide/Show | Large catalogs and broader assistive navigation need additional testing. |
| Dock and Overview | Task switching, workspace-group close and cached previews | Previews are last-seen screenshots, not live per-window textures. |
| Quick Settings | Controls, provider feedback and gesture dismissal | Device actions require compatible services and permissions. |
| Settings | Themes, accents, wallpaper import, preferences and diagnostics | Some configuration changes require a new session. |
| Session | Separate login entry, Sway startup, task coordination and scoped cleanup | Graphical display-manager selection and package lifecycle testing are incomplete. |
| Themes | Layered configuration and user CSS | User theme catalogs and some custom-rendered geometry are not configurable. |
| Notes | Local text preview, visible refresh and external-editor launch | An editor must be installed separately; notes are unencrypted. |
| External widgets | NDJSON helpers, scaffolding, validation and registration | Helpers run as the user; protocol v1 has no keyboard-event channel. |
| Clock and calendar | Clock and month display | Launching an app requires an external handler. |
| At a Glance | Calendar status display | No event provider; displays “Calendar not connected.” |
| Memory policy | Explicit cgroup limits for supported launches | Pressure eviction, freezing and compositor restarts are not implemented. |
| Privacy screen | Swipe, pointer and keyboard dismissal | No authentication or session-lock security boundary. |
| Embedded agent panel | Disabled-by-default experimental frontend | No supported backend or complete input/layout integration. |

## External applications and services

This package does not include phone/SMS applications, a file manager, a text
editor, an on-screen keyboard, a kernel or firmware. Phone and messaging roles
are unassigned by default. Choose providers in
`~/.config/ctlst/applications.conf` and configure device services separately.

Automatic rotation requires sensor integration; a responsive layout alone
does not enable it. Sound, power, radio and brightness controls also depend on
the device's system services and permissions. Generic keyboard integration and
a fully touch-only installation workflow are not yet validated.

See [dependencies](docs/DEPENDENCIES.md) and [security](SECURITY.md).

## Validation

Revision `127676f` passes 100 source checks with two explicit skips, a native
Arch ARM build with warnings as errors, and 20 installed-session functional
checkpoints. Coverage includes wallpaper import, theme changes, dotfile reload
and logout/login persistence under a fresh VM user.

This is not a fresh OS-image, package-manager upgrade/removal, hardware
performance or universal device compatibility test.
[Validation details](release/VALIDATION.md) describe the environment and scope.
