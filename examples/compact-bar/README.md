# Compact status-bar example

This directory provides a complete Waybar configuration matching CTLST's
default compact layout: clock/status controls and a Recent apps button.
The bar is 44 logical pixels high; Recent apps has an 80×44 minimum target.

A fresh installation already uses this layout. Use the example as a starting
point for a user override; it does not require a new helper or native rebuild.

## Install an override

1. Back up `~/.config/ctlst/bar.jsonc` and `bar-style.css` if present.
   Respect `XDG_CONFIG_HOME` when using a non-default configuration directory.
2. Review and copy this directory's `bar.jsonc` into that directory. It is a
   complete configuration, so merge custom modules you want to preserve.
3. Append the example CSS to existing user CSS, or copy it if no file exists.
4. Run `ctlst-session reload`.

Tap/click Recent apps or press `Super+O` to open Overview. Its native controls,
wheel and Left/Right/Enter navigate tasks; Escape/Done dismisses without closing
applications. Overview Close targets the entire selected workspace group.

## Revert and customize

Restore the backed-up files and reload. If there was no prior override, move
the copied JSONC aside and remove only the CSS rules added from this example.
Home layout and application data are unaffected.

For per-window buttons and middle-click close, use the optional `wlr/taskbar`
configuration described in [BAR.md](../../BAR.md). Test custom modules and fonts
at the target output size; per-window lists can overflow a narrow bar.
