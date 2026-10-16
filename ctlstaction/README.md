# Action feedback surface

`ctlstaction` is a C/GTK4 layer-shell surface for the gesture daemon's action
fan. It displays Keyboard, Gamepad and Close window targets. Optional actions
require their corresponding providers.

Close first asks the captured window to shut down. If it remains after three
seconds, a themed Fuzzel menu offers **Keep open** (default) or **Force quit
(unsaved work lost)**. A window may be waiting for a save prompt rather than
hung. There is no automatic force-quit on timeout. Confirmation terminates the
captured app process, potentially closing its other windows, not the newly
focused app. Focus changes, target exit, privacy lock and menu timeout cancel.
Core shell and resident telephony services are excluded from force-quit.

Timing is read on each close from `~/.config/ctlst/close.conf`:

```ini
[close]
grace_seconds = 3
confirmation_seconds = 45
```

Allowed ranges are 1–15 and 5–120 seconds. Touch/mouse select menu rows;
arrows/Return select and Escape cancels. Return on the initial selection keeps
the app open. The shared keyboard/session-menu close helper uses this same path.
Escalation requires Linux pidfd support; normal close remains available without
it. Process owner, start identity and executable are rechecked before signaling.

The gesture daemon selects targets and executes actions. This surface is
input-transparent and does not request keyboard focus. Its selected ring and
caption provide feedback, not additional hit regions.

Rendering uses retained GTK rounded nodes and cached Pango captions.
OpenGL is the default; an explicit `GSK_RENDERER` override is respected.
The icons are CTLST-drawn primitives.

## Theme and control

`ACTION_*` roles use the shared theme pipeline. Missing roles inherit semantic
shell colors; explicitly configured values take precedence. The `R` control
message or reopening the surface reloads the palette.

## Development

Build with `make` in this directory, or `make core` from the repository root.
GTK4 and gtk4-layer-shell are required; warnings are treated as errors.

Verify target alignment with the gesture daemon, cancellation, caption bounds
and input pass-through at the supported output sizes. Hardware providers and
physical input require separate tests. See [gestures](../GESTUREMAP.md) and
[VM testing](../docs/CLEAN-ROOM-VM.md).
