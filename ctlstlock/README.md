# CTLST privacy screen

A C/GTK4 layer-shell privacy/swipe gate, not an authenticated lock. It does not
protect a session against an attacker and must not be advertised as doing so.
The existing idle/power controller owns when it appears.

Swipe upward anywhere, including over Continue: the whole opaque sheet and its
button follow your finger. Release after 150 logical pixels (or 28% of a short
viewport) to finish the slide in 220ms; a short/cancelled drag settles back in
180ms. Horizontal/downward motion does not dismiss. Rotation cancels a gesture
in the previous coordinate system. Click/tap Continue or Return/Space still work.
GTK's `gtk-enable-animations=false` preference skips settling animations; direct
finger tracking remains. No background is revealed at rest, but dragging the
privacy cover deliberately reveals the session below. It is not a secure lock.
The full-window input region remains owned until dismissal. The window releases
its Wayland role before the visible marker clears and gestures restart. The
Continue button is a native accessible control; clock/status also have an
accessible description. Drag motion retains Pango layouts and uses GTK snapshot
nodes, with no per-frame Cairo surface. OpenGL is the default; an explicit
`GSK_RENDERER=cairo` override remains available for diagnostics.

The shared theme's text, muted, accent, selected-text and lock-background roles
control appearance; theme reload sends the existing T socket command. Background
alpha is deliberately forced opaque to cover other applications. Missing lock
gradient endpoints inherit the shell background. The standard theme renderer
repairs unreadable selected-text/accent combinations after accent overrides.

Build with `make` (GTK4 and gtk4-layer-shell, warnings as errors).
`tests/lock-render-test.c` includes the actual renderer and verifies retained
text, UTF-8, opacity and portrait/landscape/narrow geometry inside a disposable
Wayland session. This does not establish hardware GPU performance.

The native `vm/clean-room/lock-swipe-ui.py` driver exercises the real application with
touch (including a button-origin drag), snap-back, horizontal rejection, pointer
and keyboard, checking actual revealed-background pixels in portrait/landscape.
This is a software-rendered VM test, not physical GPU performance measurement.

Known limits: battery status still names `qcom-battery`; NetworkManager status
reads are synchronous. Missing providers show unavailable/disconnected status.
This UI redesign does not make those integrations hardware-independent.
