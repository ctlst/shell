# Testing in a disposable VM

Use a disposable Arch ARM VM and a fresh Unix user with no CTLST configuration.
Keep the test environment separate from daily-use systems and device images.
Establish a working upstream Sway baseline before installing CTLST, and record
the hash of `/etc/sway/config`.

## Build and stage

Follow [INSTALL.md](../INSTALL.md). Build with warnings as errors, stage into an
empty directory, and inspect the paths and runtime dependencies before copying
files into the guest. Retain a VM snapshot and SSH/TTY access independent of CTLST.

## Installed-session test

`vm/clean-room/functional.py` tests startup, the status bar, drawer, task
switching, Shade, the privacy screen, Settings, wallpaper/theme changes,
dotfiles, memory limits and logout/login persistence against installed files.

Additional test dependencies include `wtype`, `wlrctl`, `grim`, GTK accessibility,
seatd and the virtual touchscreen fixture. Compile the fixture in the guest:

```sh
mkdir -p .build
cc -O2 -Wall -Wextra -Werror vm/clean-room/touch-inject.c -o .build/touch-inject
sudo install -D -m 0755 .build/touch-inject /tmp/ctlst-functional/touch-inject
```

Use an expendable user with input/seat access and a valid `XDG_RUNTIME_DIR`.
The driver calls the root-owned `/tmp/ctlst-functional/touch-inject` through
sudo; grant only that fixture the required permissions in the disposable guest.
Before granting access, verify that the binary and its containing directory are
root-owned and not writable by the test user, and that neither is a symlink.
If password-based sudo is used, provide `CTLST_CLEAN_VM_PASSWORD` through the
test environment. Do not commit credentials or grant broad passwordless access.

From that user's session, with no other Sway process running:

```sh
dbus-run-session -- python3 vm/clean-room/functional.py --disposable-vm
```

The required flag acknowledges that the driver writes test configuration,
creates test files, launches/closes fixture windows and restarts its session.
Results and screenshots are written to `~/ctlst-functional-results/`.
Remove temporary input privileges after testing.

## Native fixtures

`vm/clean-room/native-isolated.py --disposable-vm BINARY...` runs compiled
GTK fixtures in a separate headless Sway session.
`vm/clean-room/lock-swipe-ui.py --help` lists requirements for the production
privacy-screen input test.

Native geometry and virtual-input results do not establish physical GPU
performance, sensor behavior or keyboard-provider compatibility. A fresh user
in an existing VM does not validate a fresh OS image or package upgrade/removal.
Record these distinctions in [validation results](../release/VALIDATION.md).
