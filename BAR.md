# Status bar

CTLST uses upstream Waybar with a session supervisor, packaged JSONC and shared
theme styling. Edit `~/.config/ctlst/bar.jsonc` or `bar-style.css`, respecting
`XDG_CONFIG_HOME`, then run `ctlst-session reload`. The supervisor restarts its
Waybar child; user files are preserved.

## Default layout

The bar is 44 logical pixels high and contains clock/status controls plus one
**Recent apps** target. This fixed-width task entry avoids growth with the
number of application windows.

- Recent apps opens the workspace Overview; `Super+O` is its keyboard equivalent.
- The chevron or network control opens Quick Settings.
- Right-clicking the chevron opens Settings.
- Clicking the clock launches the configured terminal.
- The notification indicator opens notification history.

Overview's Close action closes the selected workspace group. Opening or
dismissing Overview does not close or rearrange applications.

## Optional per-window icons

To use Waybar's `wlr/taskbar`, copy the complete packaged
`/usr/share/ctlst-shell/defaults/bar.jsonc` into your CTLST user directory
or merge it with an existing override. Back up existing files first.

Set:

```json
"modules-center": ["wlr/taskbar"]
```

Retain the corresponding `wlr/taskbar` block and reload the session.
Restore `["custom/recent"]` to use the default task entry.

The configured taskbar includes mapped windows across workspaces/outputs,
including covered floating windows. Click activates; middle-click closes a
window. This differs from Overview's workspace-group close action.
Per-window icons can overflow narrow outputs with many windows.

[examples/compact-bar](examples/compact-bar/README.md) provides a complete
copy-and-edit reference for the default layout.

## Application identity and XDG lookup

Use a Wayland app ID matching the desktop filename and an `Icon=` available
through the system icon theme or an absolute path.

For Waybar 0.15, the supervisor prepends an absolute `XDG_DATA_HOME` to the
Waybar child's `XDG_DATA_DIRS`. Configured directories follow it; the system
fallback is `/usr/local/share:/usr/share`. Empty, unset or relative
`XDG_DATA_HOME` values leave the environment unchanged. Other session processes
are unaffected.

Waybar can still prioritize `~/.local/share` over a custom data home, so a
conflicting desktop entry there can shadow the custom entry. Restart/reload
the bar after changing its environment.

## Development

Run `make check` and the [installed-session test](docs/CLEAN-ROOM-VM.md).
For bar changes, check actual bounds and action targets with crowded task sets,
long status labels, custom fonts and short/narrow outputs. A running Waybar
process alone does not establish that its controls fit the display.
