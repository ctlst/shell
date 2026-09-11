# Validation results — 2026-09-11

Tested revision: `127676f`. Native/runtime code matches `1c8419e`; the later
revision updates documentation, dependency profiles and test infrastructure.

## Environment

| Item | Version / configuration |
| --- | --- |
| OS and architecture | Arch Linux ARM, AArch64 |
| Sway | 1:1.12-4 |
| GTK4 | 1:4.22.4-1 |
| gtk4-layer-shell | 1.3.0-1 |
| Python | 3.14.7-1 |
| Waybar | 0.15.0-3 |
| Rendering | Headless Sway, Pixman/Cairo |
| Input | Virtual touchscreen, pointer and keyboard |
| User state | Fresh Unix user in an existing disposable VM |

## Results

- `make check`: **100 passed, two skipped**, 13.43 seconds.
  The skips cover the excluded historical font and device-specific transform helper.
  Session configuration syntax validation passed.
- Native build: all seven C components pass `-Wall -Wextra -Werror`.
- Install staging: 118 files beneath `/usr`; no home directory,
  `/etc/sway/config` or bundled fonts in the payload.
- Runtime libraries: no unresolved dependencies.
- Artifact comparison: all seven rebuilt binaries are byte-identical to those
  used by the installed-session test.
- Secret scanning: no detected secrets in the reviewed working tree or its
  two-commit history. A scanner cannot guarantee the absence of sensitive data.
- Installed-session test: **20 checkpoints passed**, covering startup, visible
  bar/wallpaper, drawer launches, task navigation, pointer wheel, touch Home,
  edit-mode exit, unavailable-provider feedback, Overview/Shade/Privacy input,
  Settings startup, wallpaper import, theme changes, dotfile reload, actual
  kernel cgroup limits and logout/login persistence.
- Separate privacy-screen input test: portrait/landscape swipe, short-swipe
  cancellation, button-origin drag, horizontal rejection, pointer and keyboard
  dismissal passed.

The existing `/etc/sway/config` was unchanged. Its SHA-256 was
`ef7cd1591d8cf8db7ced418abdc1d156d72a3b2ea3f15a148183c818e751d3c9`.

## Reproduction

Follow [INSTALL.md](../INSTALL.md) for dependency installation, compilation and
staging. [CLEAN-ROOM-VM.md](../docs/CLEAN-ROOM-VM.md) describes the disposable
guest and input-fixture requirements.

The functional driver waits for GTK's SENSITIVE/SHOWING states and actual
wallpaper-save/chooser completion. This avoids relying on unsupported ENABLED
state reporting or on an already-selected wallpaper ID as an Apply completion
signal.

## Coverage limits

These results cover source installation under a fresh user, not a fresh OS
image or package-manager transaction. Graphical GDM selection, package
upgrade/removal, stock keyboard integration, sensor rotation, suspend,
accessibility and physical GPU/input behavior require additional validation.

Alpine/postmarketOS validation is incomplete. Other devices are experimental.
See [BETA.md](../BETA.md) for supported functionality and limitations.
