# Sway Touch Gesture Map

The default-off Home agent prototype uses native Ask (also Enter in the
prompt), scrolling/selectable replies, and separate Cancel action / Run action
controls. Confirmations refer to the exact displayed proposal. Moving from
the prompt to another agent control ends text-entry ownership while retaining
navigation focus. Native content/adapter checks pass; full Home, current
keyboard-provider and landscape activation are not yet accepted. See
`ctlsthome/AGENT-PANEL.md`.

Quick Settings owns keyboard input after opening, including bar/keyboard and
direct detail-page opens. Escape returns from a detail page, then dismisses
the shade. A partial swipe preview does not take keys from the active app;
hiding releases focus. This does not mark non-text navigation as a keyboard
text-entry request.

## Gamepad companion

Touch supports simultaneous controls and analog drags; a pointer can click
buttons and drag direction/stick controls. The working overlay never takes keyboard
focus away from the game. Hide pad and Low/High opacity are visible controls;
shell Home dismisses the pad over its control socket without closing the app
or requiring the optional launcher. The controller script's `opacity` and
`toggle` commands can be bound in a user's Sway dotfile. Cancelled touches and
rotation release held controller state. See `ctlstpad/README.md` for geometry,
theme paths, partial input-region behavior and outstanding accessibility work.

If virtual-controller setup fails, the error panel is modal: it takes keyboard
input and blocks taps to the underlying game/bar until dismissed. Native Close
and Try again buttons support pointer/touch and Tab/Enter; Close is focused
first, so Return dismisses without retrying. Escape, platform Back, Alt+Left
and shell Home also dismiss. Retry is explicit and never elevates privileges.
Successful recovery returns keyboard ownership to the game and restores the
ordinary control-only input region. The error body scrolls independently of
the fixed action row on short screens.

## Modern passive overlays

Home editing excludes the gesture daemon's bottom-edge task scrubber and overview.
An upward bottom swipe exits editing and the widget picker through the existing
Home path; the privacy screen still blocks Home navigation. The VM regression
checks fast and slow swipes using one persistent synthetic touchscreen.
While the widget picker is open, launcher cell/badge fallback hit tests are
disabled: pressing Close or a choice cannot grab an app behind the picker.
Home also collapses a previously revealed task dock through the existing smooth
hide path, whether invoked by the keyboard/workspace manager or bottom gesture.
It does not leave the full task pill covering Home after navigation.

Action fan and window-drop feedback remain input-transparent; the gesture
daemon owns selection and execution. The fan adds readable selection captions
without moving its targets. Drop targets show full workspace names and a
separate Close window action. At narrow widths the visible destination count
shrinks to fit (four workspaces plus New at 320px); daemon hit testing and
visual feedback use the same range. See [WINDOW-MANAGEMENT.md](WINDOW-MANAGEMENT.md).

These changes do not create keyboard-interactive menus or run backend actions
from the renderer. Normal configurable Sway commands remain the non-touch
routes. Native/VM passive-input tests do not replace real window-drag testing.

## Files and privacy screen

Swipe upward anywhere on the privacy screen, including over Continue, to move
the whole sheet. A short/cancelled drag settles back; a committed upward swipe,
the native Continue button and Return/Space share the animated dismissal path.
Horizontal/downward motion does not dismiss. GTK reduced-motion preference
skips settling animations. It is not an authenticated lock: dragging deliberately
reveals the session below. Its visible marker clears after Wayland input release.

Files uses native list selection and Tab/Enter, a native Places menu for
Home/Downloads/Pictures (Escape closes the menu first), and Up or
Alt+Up/Alt+Left for parent navigation. In multi-select mode, Select explicitly
returns the chosen files. Save replacement uses a same-surface popover with
Cancel first; Escape dismisses the popover before a second Escape cancels Files.
Refresh folder beside the path and Ctrl+R/F5 retry read-only listings. Cancel
and folder navigation stay available during loading; incomplete listings
cannot be committed. Feedback scrolls inside the content area, leaving fixed
actions reachable on short screens.


This is the interaction contract for CTLST Sway Touch. Gesture changes must
update this file, `ctlst-gestured/ctlst-gestured.c`, and the VM gesture probes
together.

## Desktop Compatibility

The portable top status bar is part of the core session. Tap/click the down chevron or
network for Quick Settings; click a task icon to activate it, middle-click to
close it, and click the clock for the configured terminal. Actions and styling
are editable in `~/.config/ctlst/bar.jsonc` and `bar-style.css`.

Without optional phone helpers, Quick Settings disables rotation and its sound
profile controls. Tap/click Sound to see the unavailable-provider explanation;
when a sound provider is installed, the same tap cycles profiles and reports
the helper's actual result.

| Gesture | Action |
| --- | --- |
| Tap in an ordinary Linux application | Application-native primary click. Thunar is configured for single-tap activation. |
| Stationary hold for 550ms in an ordinary Linux application | Move Sway's virtual pointer to the contact, emit `button3`, and pulse the haptic motor. This provides a system-wide context click for GTK, Qt, terminals, and browsers. |

Movement beyond the normal touch slop or a second finger cancels the hold. Shell
surfaces, compositor bars, the keyboard, edge gestures, games, Waydroid, and the
camera retain their native touch behavior. App-id exclusions are case-insensitive
globs loaded at gesture start from
`~/.config/ctlst/right-click-exclude`; editing the file requires no rebuild
or daemon restart.

## Edge Ownership

Quick Settings fills the output through the bottom edge when open; it does not
reserve an exposed dock strip. Swipe up, click/tap the handle or Close, or use
Escape to dismiss it (Escape first leaves a detail page).

Quick Settings content swipes scroll while more content remains below. An upward
swipe started at the bottom (or when all content fits) dismisses the sheet;
the swipe that reaches the bottom only scrolls. Brightness drags never dismiss.
Swipe upward on the header/handle, tap Close, or use the bottom Home
action to dismiss. Wi-Fi, Bluetooth and Sound have visible detail arrows in
addition to their existing hold actions. Native button activation supports
Tab/Enter/Space; these routes share the same control callbacks.

| Start region | Motion | Action |
| --- | --- | --- |
| Full-width top bar | Down | Open Quick Settings with a **progressive 1:1 pull**: one logical pixel of finger travel moves the retained panel one logical pixel. Release past the configured 96-logical-pixel threshold to settle fully open. This never changes fullscreen state. |
| Upper 40% of content (Home only) | Down | Same progressive quick-settings pull as the top bar, but only while workspace 1/Home is focused and `ctlsthome` is visible. The full-width start zone runs from the top through the configurable `CTLST_SHADE_HOME_PULL_START_DEPTH`; every other workspace keeps the narrow top-bar-only target. |
| Lower-middle content region | Up | Progressively reveal App Drawer only on Home or an app workspace containing zero windows. The start zone is the center 80% of screen width, from 50% of screen height down to the configured bottom inset (96 logical pixels / roughly half an inch by default). |
| Physical bottom edge / Waybar | Up (one finger) | **Primary overview pull** (Android-style). Progressive preview, then commit or recovery — see below. This region never opens App Drawer. |
| Bottom center strip (dash + a few extra pixels) | Horizontal scrub | Step through workspaces with haptic detents; dock reveals after intent. |
| Bottom home dash | Press | Slow task-dock inflate starts (still click-through while collapsed). |
| Bottom home dash | Hold ≥ 200ms | Commit the **task dock** to the rotary task pill. Release collapses it; scrub while held to switch tasks. |
| Bottom home dash | Tap (< hold) | Pass through to the app under the dash — does **not** go Home; inflate collapses. |
| Bottom home dash | Quick flick up | Go Home with haptic. |
| Bottom home dash | Slow pull up | Progressive multitasking overview (same speed split as the bottom edge). |

With only Home and one app workspace, the expanded dock shows one stable
Home button and one app button, instead of duplicating a carousel neighbor.
With Home alone it shows one centered button. The pill fits these controls
and its unused sides remain click-through. Larger task sets retain the
selected-center carousel; Home/focus, scrubbing and keyboard routes are unchanged.

Pointer parity: wheel or trackpad scrolling over the collapsed bottom charm
steps one task per detent through the same wrapping workspace list used by the
touch scrub. The selected task pill reveals briefly and then collapses. Only a
small target around the charm accepts pointer input; the rest of the layer
surface remains application-owned.

Hovering the charm reveals the clickable Home/task pill. Scroll is captured by
the dock window so it continues to work over task buttons during expansion.
User thresholds live in `~/.config/ctlst/gestures.conf`; Sway keyboard routes
are listed in `docs/INPUT-PARITY.md` and can be overridden in `sway.conf`.

### Bottom-edge progressive overview (primary)

The portable starter bar also exposes **Recent apps** as a stable 44px-high
tap/click target for this same Overview. `Super+O` is the keyboard equivalent;
Done/Escape returns without closing apps. Users choosing a custom icon-row bar
retain their own bar actions. This does not change bottom-edge gesture timing.

Touch must begin within `SHELL_BOTTOM_BAR_HEIGHT` (portrait 54 / landscape 50) of
the physical bottom edge. On the center workspace selector, horizontal intent
keeps the haptic scrub while clear vertical intent becomes the overview pull.

The App Drawer boundary is configured in
`~/.config/ctlst/gestures.conf` with
`[drawer] pull_bottom_inset_px`. Its default of 96 logical pixels leaves the
physical-bottom Home/overview band untouched while allowing drawer pulls to
start about half an inch from the bottom and higher within the lower half.

Apps and Quick Settings deliberately separate tracking from commitment. Their
default `*_TRACKING_GAIN=1.0` values keep motion 1:1 under the finger; they do
not require pulling the sheet across the display. Releasing Apps after 72
logical pixels or Quick Settings after 96 logical pixels commits and settles
the remaining distance with the component's short frame-clock ease-out. Tune
tracking and commitment independently in `~/.config/ctlst/gestures.conf`
(applied at the next login):

```ini
[drawer]
tracking_gain = 1.0
commit_distance_px = 72

[shade]
tracking_gain = 1.0
commit_distance_px = 96
home_pull_start_fraction = 0.40
```

Speed splits Home vs multitasking on release:

1. Clear upward intent past **10%** of screen height
   (`OVERVIEW_PULL_INTENT_FRACTION`) claims a dedicated
   `GESTURE_OVERVIEW_PULL` mode. Click-through is blocked for the rest of the
   sequence.
2. While moving, the daemon sends `P<0..1>` to `ctlstoverview`. Progress reaches
   `1.0` at about **45%** of screen height (`OVERVIEW_PULL_FULL_FRACTION`).
   Preview progress is remapped from the 10%-45% pull band, so nothing appears
   until the intent threshold is crossed.
3. **Quick flick** (whole gesture ≤ `BOTTOM_PULL_HOME_FLICK_MAX_US` ≈ 360ms,
   or overall ≥ 2 screen-heights/sec, and past `BOTTOM_PULL_ACTION`): hide
   preview and go Home with haptic — even past the overview commit distance.
4. **Slow release ≥ 22%** of screen height (`OVERVIEW_PULL_COMMIT_FRACTION`):
   send `S`, mark overview visible.
5. **Slow release below 22%**: hide preview only; no Home.

### Visible controls and keyboard parity

Committed Overview also has native Previous/Open/Close/Next and Done controls.
Left/Right or the wheel moves the selection; Enter on the canvas opens it,
and Escape/Done dismisses. Preview pulls do not take keyboard ownership.

On wide landscape outputs Overview moves Previous/Open/Close/Next beside the
heading, leaving more height for previews. The card's close target, native
Close button and committed upward swipe share the same exit animation;
preview-only cards cannot be clicked open or closed.

Drawer search has a visible empty state and Clear search action. Enter while
searching refreshes the current query and opens its first result (no match
launches nothing), even before the search debounce fires. When a grid item or button has focus, GTK
handles its own activation. Close and Escape dismiss without launching.

Drawer Edit uses compact app rows with Add/Remove from Home and Hide/Show
actions. Hidden apps remain searchable in Edit so Show can restore them.
Touch, pointer and Tab/Enter use the same native buttons; row/search activation
does not launch apps while editing. Done restores the ordinary icon grid.

The session menu starts with common actions; More options contains recovery
tools. Back returns to the first page. Disruptive actions require a second,
explicit selection, with Cancel first; Escape from confirmation is safe.

### Surfaces that block overview open

Never start progressive overview while any of these owns the surface: lock,
keyboard, drawer, shade, dialer, pad, session menu, action orbit, or overview
already committed. The bottom edge dismisses those surfaces and goes Home when
they are up; it never closes an application.

Full-screen ordinary apps **do** allow this explicit bottom-edge overview pull.

### Bottom Home behavior

Widget edit controls use a small circular remove mark inside a larger
transparent target. Releasing outside that target cancels removal; leaving
edit mode or unmapping the button also cancels it. The smaller diagonal resize
mark keeps the existing corner drag region. Both marks scale with the live
edit preview; neither changes normal widget layout.

Launcher remove badges sit at the cell's top-right, with artwork fitted into
separate remaining space while editing. Native picking and the coordinate
fallback use those same transformed badge bounds; tapping the artwork is not
a remove action. Badge size remains CSS-editable. This changes the edit target
placement, not the saved grid or normal icon position.

Home edit mode scales the live page above a separate toolbar and page-control
area. Touch and pointer drags use the preview's transformed coordinates; the
last row stays usable, including the landscape side-strip mapping. Dropping
below the preview cancels the move and does not remove a favorite. Done,
`Super+Home`, or an upward swipe from the physical bottom edge exits editing.
The bottom swipe uses Home (not generic Back), returns to the first page, and
does not open overview while editing. Edit drags are captured before
app buttons; normal-mode taps keep the release-to-launch behavior.

Hide keyboard, session menu, overview, Quick Settings, App Drawer, and
gamepad; leave fullscreen; then switch to Home. Application windows remain
running. Closing is explicit through the quick-action menu or an overview card.

## Navigation

| Start region | Motion | Action |
| --- | --- | --- |
| Bottom center strip | Horizontal scrub | Workspace scrub with detents. |
| Home content, including icons/widgets | Horizontal swipe | Drag between Home pages. Movement past tap slop cancels pending edit-mode hold; release never launches the swipe's starting target. |
| Bottom home dash | Hold ≥ 200ms | Task dock reveal (task pill). |
| Bottom home dash | Tap (< hold) | Pass through to the app under the dash. |
| Bottom home dash / bottom edge | Quick flick up | Go Home with haptic. |
| Bottom home dash / bottom edge | Slow pull up (≥ 22%) | Multitasking overview. |
| Application content | Horizontal swipe | Pass through to the application; do not switch workspaces. |
| Either physical side edge | Swipe inward | **System Back.** Hide the keyboard first, dismiss shell overlays, pop CTLST app navigation, or send standard `Alt+Left` to the focused Linux app. |
| Quick action button | Hold and fan outward | Select gamepad, keyboard, or close with haptic selection. |
| Drawer pull handle | Down | Close App Drawer. |
| Anywhere on the notification shade | Up | Close the shade after clear upward vertical intent. |
| Multitasking overview (committed) | Down | Dismiss overview (pull-down close). Horizontal still scrubs cards; an upward card drag tracks 1:1, then commits past 110 logical pixels or eases back to center over 180ms. |

Application scrolling keeps priority over shell gestures outside the explicit
regions above. If any application window exists on the current app workspace,
the lower-middle pull remains application-owned so sliders and scrolling cannot
open Drawer. A gesture claimed by the shell blocks click-through until that
touch sequence is released.

## Windows

| Start region | Motion | Action |
| --- | --- | --- |
| Titlebar | Double tap | Enter fullscreen. |
| Either titlebar corner | Tap | Toggle a compact floating window or return it to tiling. |
| Tiled titlebar | Hold until the haptic click, then drag | Fluidly rearrange the window inside its tiled workspace. The guide shows up to five existing app-workspace charms across the top plus a **+** charm. Release on a workspace to move there or on **+** to separate the pane into a new task. |
| Floating titlebar | Hold briefly, then drag in any direction | Move the card. Release on a top workspace charm to move there, on **+** for a new task, or on a slim side snap rail to return it to tiling in the adjacent task. |
| Titlebar | Quick horizontal flick | Move and follow the window to the adjacent app workspace. With only one app task, the flick creates a new task. Split workspaces detach that window onto a new task. |
| Titlebar | Long horizontal drag (~28% of screen) | Same task transfer even after the hold that arms tiled rearrange / floating move. Shorter held drags keep rearrange / reposition. |

Native Sway tiled-titlebar dragging is normally disabled so it cannot rearrange
a split before the touch daemon claims a quick flick. Holding the titlebar arms
native tiled dragging for that touch only and confirms activation with a haptic
click. The held-drag guide keeps Home out of the destination list, centers up
to five existing app-workspace charms and a **+** charm above the window, uses
slim side snap rails, and keeps the destructive close pill at the bottom.
Dropping on a workspace follows the exact moved window there; **+** creates a
new app task. Tap a titlebar corner first when the window itself should become
a movable floating card.

## Multi-Touch

| Fingers | Motion | Action |
| --- | --- | --- |
| Three | Horizontal | Switch workspace. |
| Three | Up | **Compatibility alias** for open window overview (primary is one-finger bottom pull). |
| Three | Down | Toggle keyboard. |
| Four | Horizontal | Move the focused window to the adjacent workspace. |
| Four | Up | Toggle floating. |
| Four | Down | Toggle fullscreen. |

## Gesture Priority

1. An active shell gesture keeps ownership until release.
2. Explicit top and bottom edge gestures beat application input.
3. One-finger bottom-edge overview pull claims before immediate Home navigation
   when overview open is allowed; Home remains the fallback for blocked surfaces
   and mid-length releases under the commit threshold.
4. The lower-middle App Drawer pull requires upward vertical intent and a Home
   or windowless workspace. Its vertical intent cone does not overlap Home's
   horizontal paging cone; if a tentative pull becomes horizontal, the drawer
   preview retracts and stays closed for that touch sequence. The expanded
   upper quick-settings pull requires
   downward vertical intent, workspace 1, and a visible `ctlsthome` surface;
   every other workspace only grants Quick Settings the narrow top bar.
5. Workspace scrub only begins on the bottom workspace selector; horizontal
   one-finger motion elsewhere never changes workspaces.
6. All remaining touches stay with the focused application.

Settings uses app-local native selectors and kinetic page scrolling. Selector
menus accept touch/pointer and keyboard Space; Escape closes the menu first.
The detail Back button and Escape/Back/Alt+Left return to the index, restore
category focus and bring that category into view after the page transition.
Display & sound keeps Back/Close available during device reads and changes.
Refresh device settings uses the same native touch/pointer/keyboard button
behavior and retries reads only, never a failed write.
Appearance selection is a draft: use Apply theme to save/reload, or Refresh
themes to read state again. Both are native touch/pointer/keyboard buttons;
Back/Close remain available during the theme helper's work.
Wallpaper selection and Choose from files stage a preview; Apply wallpaper
in the fixed bottom action area saves/imports. Refresh wallpapers reads only.
Touch/wheel scroll the collection and details without moving Apply offscreen.
Back/Escape remains available even if a pending action cleared control focus.

Messages' conversation More menu is app-local. Touch/pointer opens its anchored
popover; wheel/touch scroll exposes overflow actions in short landscape views.
Back/Escape dismisses it before navigating the thread. Delete opens an explicit
confirmation with Cancel focused first (Return cancels); confirmed requests
continue if the menu is closed. The global Home action remains non-destructive.

Messages' composer grows with the draft to a bounded height, then scrolls.
Return inserts a newline; Ctrl+Return sends explicitly. Ctrl+Home/End moves to
the draft's beginning/end and reveals the cursor. Send and Add attachment stay
bottom-aligned as the text field grows; their pointer/touch targets are retained.

When the keyboard leaves an exceptionally short window, wheel or touch-scroll
the outer page gutter to reach the header, composer and draft status controls.
Native Tab reveals focused controls. Resizing reveals the focused control (or
the complete composer when it fits) without reversing deliberate user scrolling.

## Quick Settings responsive controls

The primary group retains distinct toggle/detail buttons during 1/2/4-column
reflow. Touch or pointer on an arrow opens details without toggling; Tab/Return
does the same. Existing hold-to-open details and content scrolling remain.
Brightness immediately follows the primary controls. See `ctlstshade/README.md`
and the inert `shade-primary-ui.py` VM input matrix.

Secondary actions also reflow, retaining complete wrapping labels. Tab reaches
Settings and scrolls it into view when needed; pointer/touch/Return activate
the same native button. This does not change the action's provider or command.

## Memory settings input

Refresh memory status is read-only and leaves Back/Close available while loading.
Session tools are separate: portable Reload shell configuration reloads styling
and dotfiles, not app memory. Pixel Restart Sway first opens a warning with
Cancel focused; Return cancels and Escape dismisses without leaving the page.
Touch or pointer must explicitly choose Restart to request the session action.

Messages thread text remains selectable with pointer and keyboard Select all /
Copy. Links use native activation and underlines; history scrolls with touch or
wheel. Time/delivery captions are separate from bubbles and wrap in the same
bounded row. Containers add no empty focus/activation stop; message text keeps
its own native focus and selection. No action sends or deletes implicitly.

## Phone dialpad editing

Keyboard in Phone's header swaps the built-in keys for the configured system
input method; Dialpad swaps back. Ctrl+Shift+K is the keyboard toggle.
Escape/Back/Alt+Left returns from system input to the dialpad. Selection and
number text remain intact. Set [Input] system-keyboard=true in
`~/.config/ctlst/phone.conf` and restart Phone to prefer system input at launch.
Extreme keyboard-open heights use native outer scrolling to reach the header,
Call and tabs. No call is placed by a mode switch.

Phone's ordinary keypad replaces selected number text or inserts at the caret.
Hold zero with touch/pointer for `+`; a short release remains zero and moving
into a scroll cancels the hold. Keyboard users type `+` in the native number
field. In-call DTMF has no plus hold. Call still needs explicit activation.
Tap Backspace deletes the selection/previous digit; hold clears the number.
Keyboard Select all/Backspace clears without a hold. Both holds use GTK's
configurable timing, documented in the Phone README; no delayed app clear
timer remains after a released click.

## Messages recipient flow

An unreadable saved draft exposes Not loaded details and Retry load before any
edit. The same touch/pointer/keyboard routes below apply, but Retry load reads
only: it never saves or sends. Back from an untouched failed-read composer
does not overwrite recovered text with an empty placeholder. Typing explicitly
starts a new draft and returns to ordinary autosave/Unsaved recovery behavior.

A failed text-draft update exposes Unsaved details and Retry save in the
composer. Touch/pointer and native keyboard activation use the same controls;
Ctrl+Tab leaves multiline editing, Tab selects an action, and Enter activates
it. Retry saves only and never sends. Back/Escape closes Unsaved details before
navigating the thread; submission uncertainty remains a separate warning.

Messages New conversation uses an in-window recipient page. Cancel and the
normal Back routes (Escape, platform Back, Alt+Left, exported Back action)
return to the retained list/search. Enter in the phone-number entry invokes
Continue, which opens the composer but does not send. Native Tab reaches the
form actions. When the keyboard reduces the window, the retained recipient,
Cancel and Continue reflow together, keeping typing focus and their touch
targets. Explanatory chrome yields space first; extreme custom sizes keep a
native whole-page scroll fallback. Keyboard resize never sends or cancels.

## Phone call controls

Ordinary dialpad keys scroll with touch or wheel when the window is short;
number editing and the explicit Call/More row stay outside that viewport.
Tab/Shift+Tab moves through the native keys and actions, scrolling a focused
key into view. Enter activates the focused control; entering a digit does not
call. Extreme theme sizes retain the whole-page overflow fallback. Rotation
keeps the same controls and does not initiate telephony actions.

Recents rows have separate Details and Call button targets. Touch/pointer on
the main identity opens Call details; only the adjacent labeled Call button
dials. Tab/Shift+Tab navigate those native controls and Enter activates the
focused action. The list-row wrapper does not add another focus/activation
target. Full timing is readable in the scrolling detail sheet without hover;
Back restores the originating row and retained list.

Number details keep a fixed Back button above a scrolling action group.
Touch/pointer Back, Escape, platform Back and Alt+Left return to the retained
Keypad/Recents context and restore originating focus when available. Back gets
initial focus rather than Call; Tab reaches the explicit actions.

Phone's in-call controls can wrap when text needs more space. Tab reaches the
native buttons and Enter activates Keypad; pointer/touch use the same reveal
action. Touch/wheel scrolling remains the fallback for short call pages.
Changing layout never requests a call or an audio-route change.

## Overview motion

The optional `examples/compact-bar` dotfiles add a top-bar Recent apps target
for tap/click, using the existing Overview action (`Super+O` by keyboard).
They do not alter edge-gesture ownership or default bindings. Opening and
Escape/Done dismissal never close an app; Overview Close still owns the whole
selected workspace. The default top-bar icon row remains unchanged.

Output rotation/resize schedules the existing background preview capture.
Opening Overview remains immediate; its last app image is retained while the
surface is visible, without capturing Overview itself or delaying a gesture.

Overview cards represent the whole workspace: tiled, floating or mixed.
Touch/Enter opens it without retiling; explicit Close includes every member,
while a short upward pull cancels without changing the group. The native
collector and preview cache both recognize Sway's floating application leaves.

App discovery and image decoding now run outside GTK's input thread. While
loading, Done/Escape still dismisses and stale task actions are disabled.
Failed discovery says Couldn't load apps; close and reopen with any existing
opening route to retry. A cancelled GTK drag releases pending refresh without
closing a task, and late results never replace active drag/close targets.

Overview respects GTK's reduced-motion preference for post-release settling
and keyboard/wheel steps. Touch and pointer drags still track directly, and
short upward pulls still cancel. Input bindings and close scope are unchanged.
See `ctlstoverview/README.md` for the GTK dotfile and restart behavior.
