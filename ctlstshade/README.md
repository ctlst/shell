# Quick Settings and notifications

Bluetooth pairing uses an application-local BlueZ Agent1 with on-screen PIN,
passkey and comparison prompts. Only requests from the current BlueZ bus owner
for the explicitly selected device are accepted; this agent never becomes the
system default. Type a displayed keyboard PIN on the Bluetooth keyboard and
press Enter. A keyboard reported paired but not bonded offers **Complete
keyboard pairing** instead of incorrectly claiming it is ready for input.

Trust and connection follow a successful Pair and a verified `Bonded=true`
property (a BlueZ version exposing that property is required). Classic keyboards
connect their HID profile explicitly; LE devices use normal profile connection.
No global bonding/security setting is relaxed. Cancel, Escape/back navigation,
closing the shade or the 120-second deadline cancels an outstanding pairing.
The code is not written to status files or logs. Existing devices are not
automatically forgotten; a stale bond may require an explicit Forget and retry.

Device dialogs and status labels wrap long names and errors. Hidden stack
pages no longer contribute their width or height to the visible shade.
`bluetooth-browser-test.c` plus `bluetooth-pairing-fixture.h` exercise native
private-bus pairing, sender/target rejection, bond verification, HID connection,
cancellation, and 320px minimum-width checks with the full Targus name.

Bluetooth discovery stays active while the Bluetooth list is visible and stops
when navigating away or hiding the shade. The shell owns its own BlueZ discovery
session; it does not globally stop scans belonging to other applications.
Opening the list starts discovery; SCAN retries after an error. Radio power,
pairing consent and device actions remain separate controls.

The device list uses the asynchronous BlueZ object manager, with no 16-device
cutoff or repeated command-line snapshots. Name, alias, connection and pairing
changes update existing buttons; unknown devices retain their address below
the fallback label. Long names wrap. No periodic scan runs while the page is
hidden. GTK/GIO supplies the client, so there is no new runtime dependency.

`tests/bluetooth-browser-test.c` (under the component tree's parent) runs the
actual list and discovery callbacks against a private mock BlueZ bus, without
accessing the host radio. It covers 24 devices, delayed names, stable row
identity, state updates, page exit, hiding during startup and error/retry.
Build with GTK4, gtk4-layer-shell, libm and warnings as errors; run in an
isolated Wayland VM session.

`ctlstshade` is a resident C/GTK4 layer-shell surface. It covers the full output
when open and provides system controls, details and notification actions.

## Input

- Pull down from the top edge, activate the status-bar control or press `Super+A`.
- Swipe up to dismiss when content fits or the viewport is already at its bottom.
  A swipe that reaches the bottom only scrolls; the next swipe can dismiss.
- Brightness drags remain slider-owned. Horizontal notification swipes remain row actions.
- Close and the header/handle provide pointer/touch dismissal.
- Escape returns from a detail page, then dismisses the shade.
- Wi-Fi, Bluetooth and Sound have separate toggle and detail buttons.
  Tab/Enter/Space activates the same controls; arrow keys adjust brightness.

Keyboard ownership begins when the shade opens and ends when it hides.
A partial pull preview does not take keys from the active application.

## Layout and styling

Primary controls reflow into balanced one-, two- or four-column rows based on
available width, fonts and CSS. Defaults fit two columns at 320 logical pixels
and four at 640. Brightness follows the primary group. Secondary controls wrap
their full labels, and short outputs scroll without replacing focused widgets.

Override `~/.config/ctlst/theme-style.css` and run `ctlst-session reload`:

- `window#ctlstshade .shade-primary > *` and `.shade-utilities > *`:
  120px content minimum and 4px margins.
- `.shade-control-tile` and `.shade-control-detail`: control styling.
- `.shade-slider`: brightness styling.

Device names and status labels wrap to avoid widening the output.
Unavailable providers display an unavailable state; the relevant controls do
not perform a successful-looking no-op.

Quick Settings keeps native kinetic scrolling on short screens without a
visible scrollbar. A filtered directory monitor reloads atomic replacements of
the generated theme file. Theme variants override component defaults, and
unchanged theme contents do not trigger another CSS reload.

## Development

Build with `make` in this directory or `make core` from the repository root.
Run `make check` and the [installed-session tests](../docs/CLEAN-ROOM-VM.md).
For layout changes, test narrow and short outputs, larger text, scrolling,
toggle/detail separation and brightness gesture ownership. Validate actual
radio/backlight behavior only on an explicitly configured test device.
