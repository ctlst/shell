# Quick Settings and notifications

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

## Development

Build with `make` in this directory or `make core` from the repository root.
Run `make check` and the [installed-session tests](../docs/CLEAN-ROOM-VM.md).
For layout changes, test narrow and short outputs, larger text, scrolling,
toggle/detail separation and brightness gesture ownership. Validate actual
radio/backlight behavior only on an explicitly configured test device.
