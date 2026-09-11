# Input parity contract

Notes uses Home's release-only launch interaction: tap/click opens the chosen
external editor; editing/dragging/resizing the card never opens it. Keyboard
users can search Notes in the drawer or run `ctlst-home-notes --edit`. There is
no text-entry field inside the preview; save/exit keys belong to the editor.
See `docs/NOTES.md` for configuration and missing-editor behavior.

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

## Weather companion navigation

Weather's native Location & units button opens its Settings page. Touch and
pointer use Back/Close; Tab/Shift+Tab traverses native controls, Enter/Space
activates buttons, and Enter in the location entry submits. Escape or Alt+Left
returns to the forecast; Escape there closes Weather. Both pages scroll.
Opening Settings focuses Back rather than invoking the on-screen keyboard.
The disposable VM probe verifies pointer/touch Back and keyboard routes with
an inert provider; physical typing and provider acceptance remain separate.

Quick Settings covers the bottom edge as well as the rest of the output.
Primary controls reflow into balanced one-, two- or four-column rows, with
brightness next. Native Tab/Return, touch and pointer retain distinct toggle
and detail targets through resize; `shade-primary-ui.py` checks both palettes
and larger text without invoking hardware providers.
Secondary actions now share this reflow and retain their full labels. The same
matrix verifies native focus-driven scrolling to Settings at each size and
its pointer/touch/Return activation using disconnected provider callbacks.
Dismiss with an upward swipe, the clickable handle/Close control, or Escape;
an exposed dock strip is not required for recovery.

CTLST is touch-first and targets complete input parity; the remaining gaps are
listed below. Core navigation must not require a touchscreen. Pointer actions
should be discoverable on the surface they affect;
keyboard actions live in the CTLST Sway session and are user-overridable from
`~/.config/ctlst/sway.conf`.

The keyboard bindings below are installed generic defaults. Pointer/touch
affordances are tracked here; a row is complete only after its functional probe
passes. Keyboard bindings are ordinary Sway directives, not compiled shortcuts.

| Shell action | Touch | Mouse or trackpad | Keyboard default |
| --- | --- | --- | --- |
| Home | quick upward flick from bottom | click Home in expanded dock | `Super+Home` |
| Next/previous task | horizontal scrub on bottom charm | wheel/trackpad scroll over bottom charm | `Super+Tab` / `Super+Shift+Tab` |
| Task dock | hold bottom charm | hover the charm, then click Home or a task | `Super+Tab` reveals while switching |
| Overview | slow pull up from bottom or tap Recent apps | click Recent apps in the starter top bar | `Super+O` |
| App drawer | pull up in Home content | click Apps on Home | `Super+D` |
| Quick settings | pull down from top or tap bar Menu | click status-bar Menu or network | `Super+A` |
| System back | swipe inward from either side | pointer Back control in CTLST apps/overlays | `Super+BackSpace` |
| Close focused app | action fan or overview close | middle-click task or click Close | `Super+Shift+Q` |
| Toggle keyboard | three-finger down or action fan | click keyboard action | `Super+K` |
| Toggle fullscreen | four-finger down or titlebar action | double-click titlebar | `Super+F` |
| Toggle floating | four-finger up or titlebar corner | click titlebar corner | `Super+Shift+Space` |

## Pointer ownership at the bottom charm

The portable starter top bar now has a stable Recent apps target instead of an
unbounded per-window icon row. It opens the existing workspace-group Overview;
Escape/Done dismiss without closing or rearranging apps. It does not take
keyboard focus itself. The old icon row remains an editable bar.jsonc choice,
with per-window activation/middle-click close and its known crowding limit.
Existing user bar files and the Pixel's separate profile are not migrated.

To receive wheel events, the collapsed charm must own a small bounded Wayland
input region. It must never claim the entire dock surface. The target should be
larger than the painted dash for accessibility, remain centered at the physical
bottom edge, and disappear while lock or a protected full-screen surface owns
input.

Small task sets do not duplicate destinations: Home alone is centered; Home
plus one app workspace uses stable Home/app positions with a selected highlight.
The expanded input region follows the narrower measured capsule, leaving its
unused sides click-through. Its minimum width remains at least the collapsed
96px pointer target, so expansion cannot eject a pointer hovering near that
target's edge. Larger sets retain selected-center rotation. No binding changes.

A wheel detent advances exactly one task in the same wrapping order used by
touch scrubbing. Smooth trackpad deltas accumulate to one detent, rapid events
are rate-limited, and the pill briefly reveals the resulting selection before
collapsing. Threshold and rate limit are in the `[dock]` section of
`~/.config/ctlst/gestures.conf`; pointer direction is controlled by
`input.conf`'s natural scrolling option. Gesture/dock thresholds apply at login.

Drawer search supports type-to-search, Enter to launch the current query's first result, and
Escape or a visible Close button to dismiss. Native grid/button activation
is not intercepted by search; Clear search restores the list from no matches.
Enter refreshes pending search text immediately; a query with no matches never
launches a stale result while waiting for GTK's search debounce.
Edit exposes Add/Remove from Home and Hide/Show in compact app rows with a
heading count; long press retains the original Home/favorite path.
Hidden entries are visible/searchable while editing and offer
Show. The same native actions support touch, pointer and Tab/Enter; editing
never launches an app through row/search activation. Normal mode restores the
icon grid and hides those entries again. See [DRAWER.md](../DRAWER.md).
Committed Overview adds Left/Right and wheel selection, Enter to open from
the canvas, native Previous/Open/Close/Next controls, and Done/Escape dismissal.
Tab moves between its buttons; preview-only pulls do not capture the keyboard.
Wide landscape layouts place the same controls in the header; portrait keeps
the footer. Card close, native Close and upward swipe share the exit animation,
and preview-only cards ignore clicks.
The lock surface also accepts Return/Space to dismiss; it
is a swipe/privacy surface, not an authenticated security lock.
The top bar's task icons activate applications on click and close them on
middle-click; clock opens the configured terminal. Bar actions and layout are
native editable `bar.jsonc`, with `bar-style.css` overrides.
The starter bar keeps mapped windows across workspaces/outputs, including
covered floating windows, using `wlr/taskbar`'s `all-outputs: true`. Set it to
false in your complete user `bar.jsonc` and reload to prefer output filtering.
This does not change the dock/Overview's workspace-group controls.
The opt-in `examples/compact-bar` layout instead has a 44px-high Recent apps
target: click/tap opens the same Overview as `Super+O`; Escape/Done dismisses.
It intentionally has no per-window middle-click close in the top bar. The
workspace switcher and dock keep their existing input routes and close scope.
This example is not the installed default and does not change gestures.

Quick Settings marks rotation unavailable when its optional helper is missing.
Tapping/clicking Sound without its provider opens an explanation with disabled
profile controls. When installed, profile changes report their actual outcome
and ignore overlapping requests while one is pending.

Home's launcher remove badges occupy cell corners with separate artwork space.
Widget remove controls similarly use a small mark inside a larger transparent
target, with specific accessible names. Pointer/touch releases outside the
target or after edit mode ends cancel removal. The resize arrow is smaller
visually but preserves the existing drag region. Direct keyboard widget
removal remains incomplete.
Touch/pointer hit testing follows the same GTK allocation transform as drawing;
the normal icon position returns on exit. This is not new keyboard removal:
direct launcher/edit keyboard navigation remains incomplete.

Home editing uses a scaled live page with full-size controls below it. Touch
and pointer drops in the control area cancel the move; the last row remains
usable. Done, `Super+Home`, or an upward bottom-edge swipe restores the normal
full-size page. The Home action also returns to the first page. Editing blocks
the charm workspace scrubber and overview so its bottom swipe cannot become
multitasking. The persistent-touch VM probe covers fast and slow Home swipes
with the picker open. The scale is
part of GTK allocation, so hit testing follows the visible preview.

The widget picker remains unscaled within a bounded overlay; its choices scroll
while title/Close remain visible. Pointer/touch use the native controls. This
does not add direct keyboard widget creation; Home still exits edit mode.
The open picker suppresses underlying launcher cell/badge fallback hit tests,
so Close and choices remain pointer/touch targets even over the icon strip.
Action/drop overlays remain passive visual feedback, not keyboard menus; see
[window management](../WINDOW-MANAGEMENT.md) for target ownership and limits.

Remaining gaps: direct keyboard focus/navigation of Home launcher icons, Home
page keyboard/wheel navigation, external-widget keyboard interaction and the
overview's dock pointer affordance. Keyboard-toggle actions require a compatible
keyboard companion; the generic core does not supply one. These are not covered
by the passing drawer/task-switching probes.

## Files and privacy screen

The privacy screen follows an upward drag from anywhere, including Continue,
then slides away on release; short/cancelled drags settle back. Horizontal and
downward drags do not dismiss; rotation cancels the current gesture. Continue
and Return/Space remain alternate routes. It is not an authenticated lock.
The visible marker clears only after Wayland input release.

Files uses native list selection and Tab/Enter, a native Places menu for
Home/Downloads/Pictures (Escape closes the menu first), and Up or
Alt+Up/Alt+Left for parent navigation. In multi-select mode, Select explicitly
returns the chosen files. Save replacement uses a same-surface popover with
Cancel first; Escape dismisses the popover before a second Escape cancels Files.
Refresh folder beside the path and Ctrl+R/F5 retry read-only listings. Cancel
and folder navigation stay available during loading; incomplete listings
cannot be committed. Feedback scrolls inside the content area, leaving fixed
actions reachable on short screens.


Session menus keep Back/Cancel first on secondary/confirmation pages.
Disruptive actions require explicit confirmation; normal keys and pointer
selection use Fuzzel. The Pixel integration also retains its volume/power-key
bindings. Its hardware behavior still requires phone validation.

Settings opens a category index. Tap/click a category to open its lazily built
detail page; Back, Escape or Alt+Left returns to the index. Escape on the index
closes Settings. Settings detail Back restores focus
and scroll position for the originating category after the transition. Its
selectors support pointer/touch opening and Space on the focused control;
Escape dismisses the selector before leaving the page.
Settings' Display & sound page keeps
Back/Close available during helper reads/applies; Refresh device settings is a
normal touch/pointer/keyboard button and never retries a write automatically.
Appearance browsing does not apply a theme. Use its explicit Apply theme button
to save/reload; Refresh themes reads only and preserves an unapplied choice.
Both actions use native touch/pointer/keyboard activation and remain reachable
by scrolling in short landscape.
Wallpaper keeps Apply in a fixed bottom action area. Collection choices and
Choose from files stage only; pointer/touch/keyboard Apply explicitly saves or
imports. Wheel/touch scroll reveals Refresh and other details. Back/Escape is
handled at the window even when a pending operation cleared control focus;
native selector popups still consume Escape first.
Quick Settings exposes
Wi-Fi/Bluetooth/Sound detail arrows
with ordinary keyboard-focusable buttons. Its content viewport scrolls by
touch or wheel. An upward swipe started at the content's bottom dismisses
(also when all content fits); a swipe that reaches bottom only scrolls.
Brightness drags remain slider-owned. Header/handle, Close and Escape remain
available independently of scroll position. The isolated native
`shade-dismiss-ui.py` regression covers touch dismissal, scroll retention,
brightness dragging, pointer Close and keyboard Escape.
Messages groups file/photo attachment choices in a native Add attachment
popover, reachable by pointer/touch and Tab/Enter; Escape closes the popover.
The thread More menu is also a native popover: Back/Escape closes it before
leaving the thread. Delete opens a confirmation with Cancel initially focused,
so Return cancels; explicit Delete confirms. Pointer/touch Cancel dismisses it
without a request. After confirmation, Close only hides the pending request UI.
The composer grows to its bounded height, then scrolls; Return inserts a newline
and Ctrl+Return sends. Ctrl+Home/End reveals the start/end cursor. Send and Add
attachment stay bottom-aligned with the growing field; pointer/touch targets
are retained. The keyboard-free fixture tests typing and geometry; the separate
`messages-composer-keyboard-ui.py` uses the actual Arch companion keyboard with
inert actions. In exceptionally short windows, wheel/touch scrolling in the
outer page gutter reaches the header, composer and status, while native Tab
reveals focused controls. Resize reveals focus without undoing user scrolling.
This is VM input-method evidence, not physical touch/GPU or provider acceptance.

Messages thread bodies retain native pointer selection and keyboard Select all /
Copy, with underlined native links. Wheel and kinetic touch scroll the history;
row containers add no empty focus/activation target. Time and delivery captions
wrap outside the bubble. No provider action or send/delete shortcut changes.

## Phone dialpad editing

Phone keypad input uses native selection/caret semantics. Touch and pointer
hold-zero insert one `+` without a second zero on release; scrolling cancels
the hold. Physical-keyboard users type `+` directly. In-call DTMF has no plus
gesture. Keyboard in the header replaces the built-in keys with the configured
system input method; Dialpad restores them. Ctrl+Shift+K switches either way.
Escape/Back/Alt+Left returns from system input to the dialpad without changing
the number. The local entry's compose context leaves native selection, paste
and physical typing available in dialpad mode. Startup preference is editable
in `~/.config/ctlst/phone.conf`; see the Phone README for precedence/restart.
Extreme heights retain native outer scrolling and focused-control navigation.
Tap Backspace deletes the selection/previous digit; a native hold clears the
number, and a released short click cannot schedule a later clear. Keyboard
Select all/Backspace is the equivalent. Both holds use GTK's configurable
long-press timing; see the Phone README.

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

Messages' recipient page uses the existing window and native phone-purpose
entry focus. Continue/Enter opens the composer without sending. Cancel
and Escape/Back/Alt+Left/exported Back return to the retained list/search and
restore originating focus. Keyboard-open allocations put entry/Cancel/Continue
on one row where their measured widths fit, or entry above the actions on a
narrow screen. The same controls retain focus while explanatory chrome yields
space; native scrolling remains for extreme custom sizes. No timer pulls focus
back. `messages-keyboard-ui.py` tests a separately built current Arch wvkbd
provider with actual pointer/touch typing and Cancel/reopen/Continue; it does
not certify all composing, Phone, other providers or physical input.

## Phone call controls

Ordinary dialpad keys scroll with touch or wheel when the window is short;
number editing and the explicit Call/More row stay outside that viewport.
Tab/Shift+Tab moves through the native keys and actions, scrolling a focused
key into view. Enter activates the focused control; entering a digit does not
call. Extreme theme sizes retain the whole-page overflow fallback. Rotation
keeps the same controls and does not initiate telephony actions.

Recents has two distinct native button targets per row. The main identity opens
Call details; only the separately labeled Call control requests dialing. Tab and
Shift+Tab navigate the controls; Return activates the focused one. The container
row does not add its own activation or focus stop. Back/Escape/Alt+Left returns to
the retained list and origin. Complete timing is readable inside details rather
than depending on hover tooltips. Short lists and long lists use the same kinetic
viewport, including focus-driven scrolling to off-screen controls.

The number-detail sheet provides fixed touch/pointer Back and keyboard
Escape/Back/Alt+Left. Opening it focuses Back, not Call. Dismissal restores the
retained Keypad/Recents context and originating focus (or the Recents tab if
the row was retired). Native Tab navigation reaches actions inside the scroller.

Phone's native in-call action group wraps with available width and text size.
Tab/Enter and pointer/touch reach the same Keypad reveal action. Its existing
scroll viewport exposes controls in short windows; state-hidden controls also
hide their layout wrappers. Layout changes do not perform telephony actions.

## Test rule

Each row needs at least a source-level contract test and a clean-VM smoke probe.
Pointer and keyboard routes must call the same task/overlay authority as touch;
they may not grow separate workspace models.

## Memory settings input

Refresh memory status is read-only and leaves Back/Close available while loading.
Session tools are separate: portable Reload shell configuration reloads styling
and dotfiles, not app memory. Pixel Restart Sway first opens a warning with
Cancel focused; Return cancels and Escape dismisses without leaving the page.
Touch or pointer must explicitly choose Restart to request the session action.

## Overview motion

Rotation/resize now requests an asynchronous, debounced preview refresh. This
does not delay touch, pointer or keyboard opening; a visible Overview retains
the previous app image until it is dismissed, rather than capturing its chrome.

The same task card includes tiled and floating workspace members. Touch/Enter
opens the existing arrangement; pointer/native Close targets the complete
group, and a short upward drag cancels. No extra floating-only action is needed.

App discovery and image decoding now run outside GTK's input thread. While
loading, Done/Escape still dismisses and stale task actions are disabled.
Failed discovery says Couldn't load apps; close and reopen with any existing
opening route to retry. A cancelled GTK drag releases pending refresh without
closing a task, and late results never replace active drag/close targets.

Overview respects GTK's reduced-motion preference for post-release settling
and keyboard/wheel steps. Touch and pointer drags still track directly, and
short upward pulls still cancel. Input bindings and close scope are unchanged.
See `ctlstoverview/README.md` for the GTK dotfile and restart behavior.
