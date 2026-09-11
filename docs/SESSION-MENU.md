# Session and recovery menu

The modern UI menu remains a lightweight Fuzzel client. Its first page offers
Exit menu, Lock screen, More options, and installed power actions. More options
contains Close focused app, Reload configuration, Log out and Suspend where
available. The Pixel integration keeps its separate touchscreen-recovery,
compositor-restart and desktop-selection actions.

Back returns to the first page. Closing an app, logging out, suspending,
restarting or powering off requires a second explicit selection. Cancel is
always first; Escape cancels confirmation and returns to the current page.
The command is selected from a fixed allowlist, never evaluated as shell text.
Power privileges and backend policy are unchanged.

## Editable appearance

By default the menu uses the generated palette with Noto Sans 14, 28 character
width, five visible rows, 44 pixel row height and 12 pixel selection radius.
Long secondary lists scroll. Navigation is by pointer/touch or Up/Down/Return;
the Pixel Fuzzel configuration also binds volume and power keys.

For full appearance control, copy the generated Fuzzel configuration:

```sh
cp "$XDG_RUNTIME_DIR/ctlst-shell/generated/fuzzel.ini" \
   "${XDG_CONFIG_HOME:-$HOME/.config}/ctlst/session-menu.ini"
```

Edit the ordinary INI file, for example:

```ini
[main]
font=Noto Sans:size=14
width=28
lines=5
line-height=44
```

When that file exists, the menu does not override its geometry or typography.
Keep the copied colors/key-bindings sections, or customize them too. The file
is read on each menu opening and is never overwritten by a theme update;
its colors are now user-owned. Remove/rename it to resume generated styling.
This is a whole-file Fuzzel override, not a new layered session INI scope.

Pixel integration uses `~/.config/sway-touch/session-menu.ini` instead,
copied from its adjacent `fuzzel.ini`.

`session-menu --list-actions` and `--resolve "Action"` remain read-only
diagnostic interfaces. The full flat action list is not the first UI page.
Tests use inert providers for every power command; the VM UI probe opens
Reboot confirmation and cancels it, never reboots the guest.
