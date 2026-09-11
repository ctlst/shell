# Core beta VM validation

Use a disposable Arch ARM VM with unrelated state and a new Unix user whose
CTLST configuration is absent. Never reset or reuse the Pixel VM or a phone
image to make a generic install appear to work. First establish an upstream
Sway baseline; hash /etc/sway/config and preserve it throughout installation.

Build this source with warnings as errors and stage `make install` into an
empty directory. Audit the staged paths and dependencies before copying it
into the disposable guest. Keep access to the VM independently of CTLST.
See INSTALL.md for the source-install boundary.

`vm/clean-room/functional.py` exercises the installed session, bar, drawer,
workspace switching, shade, privacy cover, theme/wallpaper, dotfiles and session
reentry. It requires real virtual input, wtype/wlrctl, GTK accessibility,
seatd and the built touch-inject fixture. These are test harness dependencies,
not extra core providers. Run only in a disposable guest with no active Sway.
Invoke it as `python3 vm/clean-room/functional.py --disposable-vm`. The required
flag confirms an expendable test user and guest; do not use your normal account.
The fixture writes test configuration and restores only its own changes.

The independent native-isolated.py runner is for bounded C fixtures, not the
whole shell portability result. Passing native geometry alone does not prove
physical input, GPU performance, sensors, suspend or keyboard compatibility.

Current exact results and outstanding gates are in ../release/VALIDATION.md.
Historical pmOS results are not acceptance of this beta; its current lane is
blocked on sudo authentication. A fresh user in an existing VM is also not a
new OS image or a package install/upgrade/uninstall test.
