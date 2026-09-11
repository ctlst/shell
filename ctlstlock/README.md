# Privacy screen

`ctlstlock` is a C/GTK4 layer-shell privacy cover controlled by the session's
idle/power integration. **It does not authenticate users or provide a secure
session lock.** Dragging the cover reveals the session beneath it.

## Input and animation

Swipe upward anywhere, including over Continue. The complete sheet follows the
finger. Release after 150 logical pixels, or 28% of a shorter viewport, to
finish the slide in 220ms. A short or cancelled drag settles back in 180ms.
Horizontal/downward gestures do not dismiss; rotation cancels an active drag.

Continue, Return and Space provide pointer/keyboard alternatives.
GTK's `gtk-enable-animations=false` preference skips settling animations;
direct finger tracking remains active.

The full-window input region is retained until dismissal. The window releases
its Wayland role before clearing the visible marker and restarting gestures.
Continue is a native accessible control; clock/status text has an accessible
description.

## Rendering and theme

The sheet uses retained Pango layouts and GTK snapshot nodes, without a
per-frame Cairo surface. OpenGL is the default; `GSK_RENDERER=cairo` is available
for diagnostics.

Shared text, muted, accent, selected-text and lock-background roles control
appearance. The `T` socket command reloads the theme. Background alpha is forced
opaque at rest; missing gradient endpoints inherit the shell background.

## Development and limitations

Build with `make` in this directory or `make core` from the repository root.
The included [renderer fixture](../tests/lock-render-test.c) checks text,
opacity and geometry. The [input driver](../vm/clean-room/lock-swipe-ui.py)
tests portrait/landscape swipe, cancellation, pointer and keyboard behavior
in a disposable VM.

Battery status currently names `qcom-battery` and NetworkManager reads are
synchronous. Missing providers display unavailable/disconnected status.
Software-rendered VM tests do not measure physical GPU performance.
See [security](../SECURITY.md) before using this surface.
