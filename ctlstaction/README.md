# Action feedback surface

`ctlstaction` is a C/GTK4 layer-shell surface for the gesture daemon's action
fan. It displays Keyboard, Gamepad and Close window targets. Optional actions
require their corresponding providers.

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
