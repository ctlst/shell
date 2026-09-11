# Build and try the core beta

Start with a disposable Arch Linux ARM VM or a backed-up device that already
runs upstream Sway correctly. CTLST does not provide a kernel, modem, firmware,
display manager or on-screen keyboard. The reference phone is Pixel 3a XL on
its existing Arch setup; other devices are experimental. See BETA.md and
SECURITY.md first. Keep SSH/TTY access and your existing desktop session.

## Build and inspect

Python 3.11+ is required by source tests. Install `python-pytest` separately to
run them. Runtime/build dependencies come from the manifests, not bundled copies:

```sh
./scripts/install-dependencies build
make -j2 core
make check
make install DESTDIR="$PWD/.build/stage"
```

The dependency command uses a full Arch upgrade and asks for confirmation.
Review the transaction. The core build uses warnings as errors. Inspect
`.build/stage` before installation; it must contain only `/usr` files and no
home directory, `/etc/sway/config`, keyboard fork or optional phone app.

This candidate has no supported AUR recipe or package manager uninstall yet.
The staging command does **not** install onto your live system. For development
testing on a disposable VM, after checking for existing CTLST files:

```sh
sudo cp -a .build/stage/. /
ctlst-session config check
```

Do not use that copying command as an upgrade/uninstall policy on an important
system: it has no package database or rollback transaction. Retain the staged
file list and a VM snapshot. Proper distro packaging is a remaining release gate.

## Start and customize

An existing compatible display manager should discover
`/usr/share/wayland-sessions/ctlst.desktop` as **CTLST Shell**. Selecting it
starts upstream Sway with CTLST's own generated configuration. The installed
entry is tested; graphical GDM selection itself is not a certified beta gate.
For an appropriate local graphical/TTY session, `ctlst-session start` is the
direct entry point; do not start another compositor inside your current one.

Your ordinary `~/.config/sway/config` remains yours. CTLST overrides live under
`~/.config/ctlst/`, for example `sway.conf`, `home.yaml`, `theme.conf`,
`theme-style.css`, `bar.jsonc`, `gestures.conf` and `applications.conf`.

```sh
ctlst-session config paths
ctlst-session config edit theme
ctlst-session config effective theme
ctlst-session config check
ctlst-session reload
```

`config edit` creates a default only when your file does not exist. `reload`
applies supported theme/Sway changes; some input/layout settings require logging
out and selecting CTLST again. A plain text editor works too. See
docs/ARCHITECTURE.md, docs/INPUT-PARITY.md, docs/NOTES.md and docs/WIDGETS.md.

Phone and Messages are unassigned by default; Files uses your external handler.
Supply the apps/providers you want. Never assume the Pixel's keyboard, sensors,
audio or power services exist on a fresh installation.
