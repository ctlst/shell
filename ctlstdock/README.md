# Task dock

`ctlstdock` is a C/GTK4 task switcher using gtk4-layer-shell. It creates a
184×56 logical-pixel surface at the bottom center with no exclusive zone.
Unused regions remain click-through.

## Navigation

At rest, only a gesture dash is visible. Hover reveals the dock; clicking Home
or a task requests navigation through the workspace manager. Wheel/trackpad
scrolling over the collapsed 96×24 input target steps through the same wrapping
task order as touch scrubbing.

Home alone produces one centered control. Home plus one app uses stable
Home/app positions. With larger task sets, the selected destination is centered
between its neighbors in a three-slot carousel. Up to 64 app workspaces are
represented. Grouped windows display app identities and a count.

The painted capsule fits its controls with a 96px minimum width.
Returning Home schedules collapse after 420ms. Expanded dock geometry does not
reserve additional space in Home.

## Control interfaces

Workspace discovery uses `$XDG_RUNTIME_DIR/ctlst-workspaces.sock`:

```json
{"command":"list"}
{"command":"home"}
{"command":"focus","workspace":"2"}
```

Gesture feedback uses `$XDG_RUNTIME_DIR/ctlst-dock.sock`:

| Message | Effect |
| --- | --- |
| `S` | Start reveal |
| `P 0` through `P 1000` | Set reveal progress |
| `E` | Complete reveal and enable interaction |
| `H` | Schedule collapse |

Hold-driven reveal remains below the interactive threshold until `E`, allowing
short touches to pass through. The gesture daemon owns scrub selection; the
workspace manager owns navigation.

## Rendering and configuration

The dash-to-pill animation uses retained GTK nodes. Static fallback icons may
use cached Cairo rendering. OpenGL is the default; `GSK_RENDERER` overrides it
for diagnostics.

Theme colors come from `CTLST_GENERATED_DIR/theme.env`, normally
`$XDG_RUNTIME_DIR/ctlst-shell/generated/theme.env`. The standalone fallback is
`$XDG_CONFIG_HOME/ctlst/theme.env`. Use the shared theme pipeline to reload.
Wheel thresholds are configured in `gestures.conf`; see [input controls](../docs/INPUT-PARITY.md).

Build with `make` in this directory or `make core` from the root. Test small and
large task sets, input-region bounds, task ordering, collapse and pointer/touch/
keyboard navigation. [VM validation](../docs/CLEAN-ROOM-VM.md) covers installed
task navigation; physical responsiveness requires device testing.
