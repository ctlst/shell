# Compact status-bar reference

A copy-and-edit reference for the portable CTLST starter layout: clock/status controls stay in
place while one **Recent apps** target opens the existing native Overview.
The bar is 44 logical pixels high, and the new target is at least 80×44px.
It uses the selected theme's colors and ordinary Waybar JSONC/CSS. No helper,
polling loop, native rebuild, compositor patch or extra daemon is added.

This layout is now the modern-UI branch's default. Existing user bar files are
not migrated or overwritten. Optional per-window activation/middle-click close
remains available through the normal `wlr/taskbar` block, as described in
[`BAR.md`](../../BAR.md). The bounded default moves task navigation to the dock and
Overview. Overview groups workspaces; its Close action closes the whole selected
workspace, not one arbitrary window. Apps and windows are not removed or
rearranged when opening or dismissing it.

## Try it without losing your configuration

This example targets the portable session, not the Pixel's legacy two-bar
configuration. It is a checkout reference; the same layout is supplied through
the packaged default JSONC and CSS, so a fresh install needs no copied example.

1. Back up your existing `~/.config/ctlst/bar.jsonc` and `bar-style.css`, if
   present. Respect `XDG_CONFIG_HOME` if you have changed it.
2. Review this directory's `bar.jsonc` and copy it to that user directory.
   It is a complete replacement configuration; merge any custom status modules
   you want to retain before using it.
3. Append this directory's `bar-style.css` rules to your existing user CSS,
   or copy the file if you have none. Do not overwrite unrelated CSS rules.
4. Run `ctlst-session reload`. No rebuild or logout is required.

Click or tap Recent apps to open Overview. `Super+O` provides the equivalent
keyboard route. Once open, use the native controls, wheel, Left/Right and Enter;
Escape/Done dismiss without closing an app. Existing bottom-edge touch gestures
and the dock remain available. The status bar itself does not take keyboard
focus. Quick Settings and notification routes are unchanged.

To undo, restore your backed-up files and reload. If no user bar file existed,
move the example aside to restore the packaged default (now also compact); remove only the CSS
rules you added. Neither applying nor undoing this example changes Home layout,
themes, application data, or the standard Sway session.

## Validation and limits

`vm/clean-room/bar-crowding-ui.py` measures the real Waybar frame, status label
bounds and rendered fixture icons with 1–20 owned inert windows, at 320/480/640
logical widths. With `--compact-dir` and `--touch-inject`, it also checks pointer,
keyboard and touch opening/dismissal, unchanged app IDs, and the 44px target.
Each run gets private XDG directories and restores its owned session state.

Diagnostic mode can record old icon-row overflow rather than claiming that
a running Waybar process is visual acceptance. `--bounded-default` tests the
actual packaged layout without a copied example; compact mode tests an override.
Both assert bounded
geometry. These are software-rendered VM checks, not phone GPU/input, arbitrary
font scaling, localization, or all possible status-provider output acceptance.
