# Touch, pointer and keyboard controls

Keyboard bindings are installed by [sway/ctlst.conf](../sway/ctlst.conf).
Override them in `~/.config/ctlst/sway.conf` and run `ctlst-session reload`.
Gesture thresholds are configured separately in `gestures.conf` and apply at
the next login. `Super` refers to the configured Sway modifier (default Mod4).

## Session navigation

| Action | Touch | Pointer | Keyboard |
| --- | --- | --- | --- |
| Home | Quick upward flick from bottom | Home in expanded dock | `Super+Home` |
| Next/previous task | Horizontal scrub on bottom charm | Wheel/trackpad over charm | `Super+Tab` / `Super+Shift+Tab` |
| Reveal task dock | Hold charm | Hover charm | Task-switch shortcuts reveal it |
| Overview | Slow bottom pull or Recent apps | Recent apps in bar | `Super+O` |
| App drawer | Upward pull in Home content | Apps on Home | `Super+D` |
| Quick Settings | Top pull or bar chevron | Chevron/network control | `Super+A` |
| Back | Inward side-edge swipe | Surface-specific Back control | `Super+BackSpace` |
| Close focused window | Action fan | Surface Close; optional taskbar middle-click | `Super+Shift+Q` |
| Keyboard toggle | Three-finger down or action fan | Keyboard action | `Super+K` |
| Fullscreen | Four-finger down or titlebar double-tap | Titlebar double-click | `Super+F` |
| Floating | Four-finger up or titlebar corner | Titlebar corner | `Super+Shift+Space` |
| Terminal | Bar clock | Bar clock | `Super+Return` |
| Session menu | Configured menu action | Configured menu action | `Super+Shift+E` |
| Privacy cover | Configured idle/power action | Session menu | `Super+Escape` |

Keyboard, haptic and device actions require compatible external providers.
The privacy cover has no authentication. See [gesture details](../GESTUREMAP.md)
for start regions, thresholds, priority and cancellation.

## Home and widgets

Horizontal drags page through Home. Long-press enters editing; the scaled live
preview leaves full-size controls below it. Drops outside the preview cancel.
Done, `Super+Home` or a bottom-edge Home swipe exits editing; Home returns to
the first page. The widget picker receives input before underlying apps.

Remove controls commit on release within their target. Leaving the target,
unmapping it or exiting edit mode cancels the action. Removal affects Home
placement, not installed application files.

Notes opens its external editor on tap/click. Keyboard users can launch Notes
from the drawer or run `ctlst-home-notes --edit`. The preview has no text entry;
save/exit shortcuts belong to the selected editor.

**Incomplete routes:** direct keyboard Home icon navigation/editing, Home page
keyboard/wheel navigation and widget-picker keyboard interaction. External
widget protocol v1 has no keyboard event channel. Use native companion
applications for keyboard-heavy widget workflows.

## App drawer

Typing filters the app catalog. Enter in search refreshes the current query
and launches its first result; no matches launches nothing. Focused native
controls use their own activation. Escape/Close dismisses.

Edit offers Add/Remove from Home and Hide/Show via touch, pointer and Tab/Enter.
Hidden apps remain searchable there. Row/search activation never launches an
app in Edit. See [DRAWER.md](../DRAWER.md).

## Dock and Overview

The collapsed dock owns only a centered 96×24 pointer region. Wheel deltas
accumulate into task steps and briefly reveal the selected destination.
Configure detent/rate thresholds under `[dock]` in `gestures.conf`.
Unused dock surface regions remain click-through.

The default bar uses one Recent apps target. An optional per-window taskbar
has separate activation/middle-click-close behavior; see [BAR.md](../BAR.md).

Committed Overview accepts horizontal drag, wheel or Left/Right selection;
Enter opens the selected workspace. Tab traverses Previous/Open/Close/Next and
Done. Downward drag, Done or Escape dismisses. Close and a committed upward
card swipe close the entire workspace group, including floating members.

Partial previews do not own keyboard focus. Loading disables stale task
actions but retains dismissal. Failed discovery displays a retry instruction.
Reduced-motion preferences affect settling, not direct input tracking.
See [Overview](../ctlstoverview/README.md).

## Quick Settings

Quick Settings owns keyboard input once opened. Escape leaves a detail page,
then dismisses the shade. Partial pull previews do not take keys.

Wi-Fi/Bluetooth/Sound toggle and detail controls are distinct native buttons.
Touch, pointer and Tab/Enter activate the same callbacks. Brightness supports
native arrow-key adjustment and owns its drag gesture.

An upward content swipe scrolls while more content remains below. A new swipe
started at the bottom, or when all content fits, dismisses. Header/handle,
Close and Escape remain available independently of scroll position.
See [Quick Settings](../ctlstshade/README.md).

## Settings and session menu

Settings Back, Escape, platform Back and Alt+Left return to the category index;
Escape at the index closes it. Selector popups consume Escape first. Returning
to the index restores category focus and scroll position.

Theme/wallpaper choices are drafts until Apply. Refresh is read-only.
Wallpaper Apply stays below the scrolling page. Back/Close remain available
during helper operations; a timeout does not necessarily cancel accepted work.

The Fuzzel session menu supports pointer/touch and Up/Down/Return.
Disruptive actions require confirmation with Cancel first.
See [Settings](../SETTINGS.md) and [session menu](SESSION-MENU.md).

## Privacy screen and passive overlays

Swipe upward anywhere on the privacy screen to move it away. Short/cancelled
drags settle back; horizontal/downward motion does not dismiss. Continue,
Return and Space are equivalent dismissal routes. Rotation cancels an active
gesture. See [privacy screen](../ctlstlock/README.md).

The action fan and window-drop overlay are passive feedback. The gesture daemon
owns selection/execution; use the corresponding Sway commands for keyboard
operation. See [window movement](../WINDOW-MANAGEMENT.md).

## Contributor requirements

Input changes must retain a shared action backend across touch, pointer and
keyboard routes. Add focused tests, test cancellation, and document remaining
gaps. [VM testing](CLEAN-ROOM-VM.md) covers simulated input; physical keyboard
providers, assistive technology and device timing require separate validation.
