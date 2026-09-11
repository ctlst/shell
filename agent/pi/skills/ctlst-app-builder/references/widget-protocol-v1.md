# CTLST Widget Protocol v1

## Contents

- Transport and lifecycle
- Host messages
- Widget frames
- Scene nodes
- Input behavior
- Descriptor
- Resource and safety limits
- Example exchange

## Transport and lifecycle

An external Home widget is a long-lived helper without a Wayland surface.
Home launches the descriptor's `exec` and fixed `args`, writes one JSON object
per line to stdin, and reads one JSON object per line from stdout. UTF-8 NDJSON
is the only protocol transport. Logs belong on stderr.

Every object contains `"ctlst_widget": 1`. Unknown message types and unknown
fields must be ignored for forward compatibility.

Sequence:

1. Home starts the helper and sends `hello`.
2. The helper emits a complete `frame` immediately.
3. Home sends `configure` whenever allocation or orientation changes.
4. Home sends pointer and visibility messages as applicable.
5. Home sends `shutdown`, then closes stdin. The helper exits promptly on
   either event or EOF.

Frames replace the previous scene atomically. There are no partial updates in
v1. Home retains and redraws the last valid frame; helpers must not repaint
continuously when nothing changed.

## Host messages

### `hello` and `configure`

```json
{"ctlst_widget":1,"type":"hello","id":"tetris","width":224,"height":244,"orientation":"portrait"}
```

`configure` has the same geometry fields. Width and height are logical pixels.
Orientation is `portrait` or `landscape`. Render in a stable logical `view`;
Home aspect-fits it into the allocation.

### `visibility`

```json
{"ctlst_widget":1,"type":"visibility","visible":false}
```

Pause gameplay, animation, network refresh, and timers while hidden. Preserve
state unless the widget's product behavior explicitly says otherwise.

### `pointer`

```json
{"ctlst_widget":1,"type":"pointer","phase":"end","x":120.0,"y":80.0,"nx":0.54,"ny":0.33,"view_x":52.0,"view_y":31.0,"inside":true}
```

`phase` is `begin`, `move`, `end`, or `cancel`. `x`/`y` are logical pixels in
the current allocation; normalized `nx`/`ny` use that allocation. `view_x` and
`view_y` are mapped through Home's aspect-fit transform into the declared view,
and `inside` reports whether that mapped point is in the scene. Use view
coordinates for hit testing. Treat `end` as the commit event. Begin and move may
only update pressed/drag previews. A move beyond the control or a cancel must
not activate it.

### `shutdown`

Stop background work and exit successfully. EOF is equivalent.

## Widget frames

```json
{
  "ctlst_widget": 1,
  "type": "frame",
  "seq": 12,
  "view": {"width": 100, "height": 120, "background": "#252535"},
  "nodes": [
    {"type":"text","x":50,"y":8,"text":"TETRIS","size":11,
     "weight":"bold","align":"center","fill":"#ffffff"},
    {"type":"rect","x":10,"y":24,"width":12,"height":12,
     "radius":2,"fill":"#42a5f5"}
  ]
}
```

- `seq` is an optional monotonically increasing integer for diagnostics.
- `view.width` and `view.height` are positive logical scene units, maximum
  4096 each.
- `view.background` is a CSS color understood by GTK.
- `nodes` are painted in array order; later nodes are above earlier nodes.
- Send a complete frame. Do not depend on state from an earlier frame.

## Scene nodes

All coordinates use the declared view. Home ignores unknown node types.

### Rectangle

```json
{"type":"rect","x":4,"y":5,"width":20,"height":10,"radius":3,"fill":"#6f24cf"}
```

### Circle

```json
{"type":"circle","x":20,"y":20,"radius":8,"fill":"rgba(255,255,255,.8)"}
```

### Line

```json
{"type":"line","x":4,"y":5,"x2":20,"y2":30,"line_width":2,"stroke":"#ffffff"}
```

### Text

```json
{"type":"text","x":50,"y":8,"text":"Score 1200","size":10,"weight":"bold","align":"center","fill":"#ffffff"}
```

`align` is `left`, `center`, or `right`; `weight` is `normal` or `bold`.
Text is clipped to the widget allocation. Keep strings concise.

### Shared theme colors

Color fields (`view.background`, `fill`, and `stroke`) may use a literal
`#RRGGBB` or `#RRGGBBAA` value, or one of these stable CTLST theme tokens:

```text
@ctlst_bg             @ctlst_panel          @ctlst_panel_alt
@ctlst_raised         @ctlst_overlay        @ctlst_text
@ctlst_muted          @ctlst_line           @ctlst_line_soft
@ctlst_accent         @ctlst_accent_soft    @ctlst_warm
@ctlst_danger         @ctlst_danger_active  @ctlst_selected_text
```

Portable Home resolves tokens from the session-generated `theme.env` under
`$XDG_RUNTIME_DIR/ctlst-shell/generated/` and redraws a
running widget when that file changes. A missing token falls back to a safe
built-in color. Unknown `@ctlst_*` values are rejected by the validator so a
widget cannot silently depend on a private theme variable.

## Input behavior

Descriptors choose one interaction mode:

- `pointer`: receive pointer phases; suitable for controls and mini games.
- `launch`: a release-only tap launches the descriptor's desktop ID.
- `none`: display-only.

A pointer widget should derive hit regions from its own view geometry. Visual
pressed state may follow begin/move, but mutations such as placing a block,
sending data, deleting, or launching occur only on end inside the same target.

Global top/bottom shell gestures remain shell-owned. Home may claim horizontal
page drags that begin over a widget and cancel its pointer sequence. Do not implement shell
navigation inside a widget.

## Descriptor

Descriptors live in `~/.config/ctlst/widgets/*.yaml` for user widgets or
`/usr/share/ctlst/widgets/*.yaml` for system widgets. The legacy
`~/.local/share/sway-touch/widgets/` directory is also scanned. Discovery is
at Home startup; use `ctlst-widget reload` after registration in the portable
session. Example files under `/usr/share/ctlst-shell/examples` are not
automatically registered.

```yaml
id: tetris
name: Tetris
kind: exec
protocol: ctlst-widget-1
interaction: pointer
min_columns: 3
min_rows: 3
default_columns: 3
default_rows: 3
exec: ctlst-widget-tetris
args: []
desktop: dev.ctlst.Tetris.desktop
```

IDs match `[a-z0-9][a-z0-9_-]*`. `exec` is an absolute executable path or is
resolved through PATH without a shell. The registrar uses an absolute installed
path. Arguments are fixed descriptor strings; never put shell syntax in them.
The desktop field is required only for `interaction: launch` or a companion
full app.

Home instantiates all discovered external widgets when `widgets.enabled` is
omitted from `~/.config/ctlst/home.yaml`. When present, it is an allow-list; an
explicit empty list disables external widgets. The widget must fit within the
available Home grid. If no unoccupied region can fit it, Home hides it instead of
covering another card.

## Resource and safety limits

- Maximum stdout line: 256 KiB.
- Maximum painted nodes: 512 per frame.
- Maximum view dimension: 4096.
- Target at most 30 changed frames per second; prefer event-driven frames.
- Never read touch devices, speak Sway IPC, position windows, or synthesize
  input from a widget helper.
- Never place secrets, credentials, or unbounded remote content in frames.
- Validate and bound all network data before rendering.

## Developer CLI

`ctlst-widget` is the installed entry point for on-device agents:

```sh
ctlst-widget scaffold weather-clock --name "Weather Clock" --output ./build
ctlst-widget validate ./build/weather-clock/weather-clock.yaml
ctlst-widget register ./build/weather-clock/weather-clock.yaml
ctlst-widget list
ctlst-widget remove weather-clock
```

`register` validates the helper before installation, copies it atomically to
`~/.local/bin/ctlst-widget-ID`, installs the descriptor atomically at the
one-level path Home scans, and updates the enabled list. Existing files are
never replaced unless `--force` is explicit. `install` is an alias for
`register`; use `--helper PATH` for a helper that is not next to its
descriptor. The command accepts `--config-dir` and `--bin-dir` on register,
list, and remove for isolated tests or a staging root.

Validation executes the helper with the user's privileges: it is not a
sandbox or security audit. Register only reviewed/trusted code. See the
installed `WIDGETS.md` for the portable workflow and current limitations.

## Example exchange

```text
Home → {"ctlst_widget":1,"type":"hello",...}
Widget → {"ctlst_widget":1,"type":"frame",...}
Home → {"ctlst_widget":1,"type":"pointer","phase":"begin",...}
Home → {"ctlst_widget":1,"type":"pointer","phase":"end",...}
Widget → {"ctlst_widget":1,"type":"frame",...}
Home → {"ctlst_widget":1,"type":"visibility","visible":false}
Home → {"ctlst_widget":1,"type":"shutdown"}
```
