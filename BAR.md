# Status bar and application identity

Waybar remains an upstream dependency, not a CTLST fork. CTLST owns its
session supervisor, configuration and shared theme styling. Edit
`~/.config/ctlst/bar.jsonc` or `bar-style.css` (respecting XDG_CONFIG_HOME),
then run `ctlst-session reload`. The bar supervisor restarts its Waybar child
to adopt these settings; user-authored files are not rewritten.

The modern-UI branch's starter bar is 44px high, with clock/status controls and
one **Recent apps** target opening the existing native Overview. Its width no
longer grows with the number of open windows. The selected theme remains in
charge of colors, and user bar files remain authoritative during updates.
Tap/click Recent apps or use `Super+O`; Escape/Done dismiss without closing
anything. Overview's Close action closes a whole workspace group, not one
arbitrary window. The bottom dock and gestures remain available.

## Optional per-window icons

The normal `wlr/taskbar` block remains in the packaged JSONC for people who
prefer direct window buttons. Back up your existing user bar file, if any.
Copy the complete packaged `bar.jsonc` (normally
`/usr/share/ctlst-shell/defaults/bar.jsonc`) to `~/.config/ctlst/bar.jsonc`, or
merge into your current user file, and set:

```json
"modules-center": ["wlr/taskbar"]
```

Keep its existing `wlr/taskbar` configuration. Run `ctlst-session reload`.
To return, select `["custom/recent"]` and reload. Existing users with their own
bar JSONC are not silently migrated; no native rebuild is needed. The icon row
can still overflow with many windows, so it is a preference rather than the
bounded default. Its configured height may optionally be reduced to 30px.

These optional task icons identify mapped applications across workspaces and outputs,
including fully covered floating windows (`all-outputs: true`).
Click activates a window; middle-click closes it, as configured in the normal
Waybar `wlr/taskbar` block. The dock/Overview remain the cross-workspace task
switchers and retain their keyboard and touch navigation. Grouped windows can
have separate bar icons. The bar chevron opens Quick Settings; right-click
opens Settings. All bindings remain editable in the bar dotfile.

An application's Wayland app ID should match its desktop filename, with an
`Icon=` name found in the normal icon theme (or an absolute icon filename).
CTLST does not replace users' icon themes, copy third-party artwork or invent
a new icon registry.

## Why the starter layout is bounded

The optional global icon row has no bounded overflow behavior. A native Arch
VM audit measured an actual 529px-wide Waybar on a 480px-wide output with 12
mapped windows: status controls were clipped. At 20 windows the requested bar
width was 785px, wider even than the 640px test output. That finding is why the
starter now uses one stable switcher target. It is not evidence that apps closed
or that taskbar visibility should return to output filtering. The original
two-app icon tests did not cover this condition.

[`examples/compact-bar`](examples/compact-bar/README.md) retains a copy-and-edit
reference for the bounded layout. The Pixel's separate two-bar profile is
unchanged by this portable default decision; no existing phone or user dotfile
is rewritten. The battery's internal right margin is 2px because the settings
chevron, not the battery, is now the outer control. The previous 14px edge
margin made even the compact prototype 331px wide on a 320px screen with full
status labels. Removing that excess spacing preserves text and touch targets.

Upstream's [taskbar options](https://github.com/Alexays/Waybar/blob/0.15.0/man/waybar-wlr-taskbar.5.scd)
do not document a count limit or scrolling viewport. Its
[group drawer](https://github.com/Alexays/Waybar/blob/0.15.0/src/group.cpp)
reveals a box; it does not supply an overflow scroller. The experiment reuses
our existing native switcher rather than adding a dependency or maintaining
a Waybar fork. Both layouts remain ordinary editable Waybar JSONC/CSS.

## Custom XDG application directories

Waybar 0.15's [desktop lookup](https://github.com/Alexays/Waybar/blob/0.15.0/src/util/icon_loader.cpp)
searches the traditional `~/.local/share` and XDG_DATA_DIRS, but omits a custom
XDG_DATA_HOME. This produced missing-image placeholders in the fresh staged
VM even though GTK could load the SVGs and the drawer/Overview resolved them.

The supervisor now prepends an absolute XDG_DATA_HOME to **the Waybar child's**
XDG_DATA_DIRS, retaining the configured directories afterward. With no system
list, it uses `/usr/local/share:/usr/share`. An unset, empty or relative
XDG_DATA_HOME leaves the environment unchanged. No files are created, HOME is
not remapped, and other session processes do not inherit this compatibility
change. Restart/reload the bar after changing its environment.

This is a narrow compatibility workaround, not a complete repair of upstream
lookup precedence: Waybar still checks the traditional home directory first.
A conflicting desktop entry there can shadow a custom-location entry with
the same name; resolving that fully belongs upstream. No patched Waybar code
or new runtime dependency is bundled.

## Verification

`tests/test_waybar_environment.py` checks child-only environment behavior.
`vm/clean-room/session-journey-ui.py --bar-icons-only` checks the actual staged
optional icon-row bar in both palettes using a private user override. It groups its two owned
demo companions so both are visible, checks source-icon colors in bar pixels,
then clicks each icon and verifies the matching window receives focus without
closing either app. The original launcher fails the rendered-icon regression.
The default/full journey instead checks Recent apps through real pointer
activation while preserving window identities, workspace membership and geometry;
`--bar-recent-only` runs that focused check for separate/tiled/overlapping groups.
These are disposable VM tests, not physical touch or GPU performance evidence.

`vm/clean-room/bar-crowding-ui.py` adds private-XDG, inert Foot windows and
captures actual bar frame width, status-label bounds and source-color icon
spans at 320/480/640px. `--bounded-default` asserts the selected packaged default
is used, bounds remain on screen, and Recent apps works with all input routes.
`--compact-dir` tests a user override instead. `--status-stress` substitutes
inert full-status labels with matching CSS for the VM's missing hardware;
`--height 320` tests short screens. This verifies geometry, not battery/network
provider functionality. Diagnostic mode can still record an older icon row's
overflow; it does not declare a visually broken bar to pass. Bounded mode
checks actual pointer/touch/keyboard open/dismiss, retaining every owned window.
The windows are tiled in one workspace for this bar test; earlier session
journeys cover cross-workspace app activation and floating groups.
Arbitrary user CSS/font scaling, extra modules and physical input/GPU acceptance
remain separate; this is not a guarantee that every customization fits 320px.
