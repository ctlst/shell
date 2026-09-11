# Home layout and widget host

`ctlsthome` is the C/GTK4 launcher and widget host. Its implementation is split
between `ctlsthome.c`, configuration, paging, rotation, descriptor and widget
runtime modules. Build from the repository root with `make core`.

## Layout

Positions are stored in portrait-logical grid coordinates. The default grid
has five columns and seven rows:

- On the first page, the top four rows hold widgets and the bottom three hold apps.
- Later pages support page-local widgets and apps in available grid cells.
- In landscape, the first-page widget cluster stays upright and the app strip
  transposes to the side. Input coordinates follow the rendered transform.
- Saved positions are retained across rotation and entry into edit mode.

Home remains visible while Sway changes output orientation. It reflows content
without a black cover or frozen-frame transition. Sensor integration is separate;
see [rotation](ROTATION.md).

## Configuration

Edit `~/.config/ctlst/home.yaml`, respecting `XDG_CONFIG_HOME`.
The session supplies ordered configuration layers; standalone Home falls back
to the user file. Missing values use compiled defaults.

The parser supports the documented scalar sections and widget ID lists, not
arbitrary YAML features. Packaged defaults are in
[config/defaults/home.yaml](../config/defaults/home.yaml); accepted keys and
ranges are defined in [home-config.c](home-config.c).

```yaml
grid:
  columns: 5
  rows: 7
  spacing_px: 10
  cluster_rows: 4
edit:
  inset_px: 12
  controls_gap_px: 12
page:
  snap_fraction: 0.5
  fling_velocity: 0.35
  animation_ms: 220
  edge_px: 48
  edge_dwell_ms: 200
  max_pages: 8
rotation:
  transition: none
launcher:
  icon_scale: 1.0
  icon_min_px: 56
  icon_max_px: 56
calendar:
  month_min_height_px: 160
widgets:
  compact_height_px: 96
```

Pixel measurements are logical display pixels; durations are milliseconds.
Icon sizes are capped by their available cell. Send Home its `T` control message
or restart Home to reload native layout configuration. Reentering the CTLST
session also applies changes.

Arrangement is stored in `~/.config/ctlst/home-layout.json`, including pages,
positions, spans and visibility. App membership remains in
`~/.config/ctlst/preferences.json`. Back up both before editing them manually;
do not overwrite user layouts when updating packaged defaults.

## Paging

Horizontal drags follow the finger. Release distance, fling velocity and
`page.animation_ms` determine the destination and settling animation.
Edit-mode edge dwell uses `page.edge_px` and `page.edge_dwell_ms`.

The default `CTLST_HOME_PAGER_MOTION=gsk` implementation retains the page strip
and updates GTK allocation transforms so rendering and picking share coordinates.
The page gutter extends the stride to the output width. `css` is a diagnostic
fallback. Paging cancels pending tap/hold actions once motion exceeds tap slop.

## Editing

Long-press an app, widget or empty Home area to enter editing. The live page
scales down above a full-size toolbar and page controls. Dragging below the
preview cancels a move; it does not remove the item.

- **Add widget** opens a scrolling picker and places the selection on the active page.
- **Wallpaper** opens Settings.
- **Done**, `Super+Home` or an upward bottom-edge swipe exits editing.
  The Home action also returns to the first page.
- Add/remove page controls respect `page.max_pages` and retain at least one page.
- App and widget remove controls remove items from Home, not their installed files.
- Widget corner handles resize within the available grid.

The picker receives input ahead of items underneath it. Remove actions commit
only on release inside their target and cancel when editing ends or the control
is unmapped. Direct keyboard navigation and removal in the Home editor are
incomplete; use the drawer's native edit actions for app placement/removal.

## Styling edit controls

Override styles in `~/.config/ctlst/theme-style.css` and run
`ctlst-session reload`:

| Selector | Default / purpose |
| --- | --- |
| `window#ctlsthome .home-edit-controls` | 28px bottom padding clears the expanded dock |
| `button.home-widget-remove` | 44×44 transparent target |
| `button.home-widget-remove > label` | 22×22 painted remove mark |
| `.home-widget-handle` | 24×24 resize marker |
| `.home-launcher-remove-badge` | 22×22 app remove mark |

These dimensions precede edit-preview scaling. Keep touch targets large enough
when adjusting the painted marks. CSS does not redefine the compiled resize
hit region. Landscape toolbar/page controls share a row when their measured
widths fit, otherwise they stack.

## External widgets

Home loads packaged and user descriptors. Reserved built-in IDs are `clock`,
`calendar`, `weather` and `glance`. External helpers use `ctlst-widget-1`:
newline-delimited JSON over stdin/stdout, rendered by Home without creating
another Wayland surface.

Use [widget development](../docs/WIDGETS.md) for search paths, registration,
schema and lifecycle requirements. Built-ins run inside Home; helpers execute
with the user's permissions and are not sandboxed.

Omitting `widgets.enabled` allows discovered external widgets; an explicit empty
list disables them. `widgets.hidden` hides selected IDs. Preserve other widget
IDs when changing these lists. New descriptors require a Home restart, for
example `ctlst-widget reload` from inside the session.

[Notes](../docs/NOTES.md) is a packaged external widget. At a Glance has no
calendar event provider. The [embedded agent panel](AGENT-PANEL.md) is experimental
and disabled by default.

## Validation

Run `make check` for source and fixture tests. The included
[home-page-edit-test.c](../tests/home-page-edit-test.c) exercises later-page
allocation and picking in a disposable Wayland session. Test layout changes
with portrait/landscape allocation, page changes, editing and applicable inputs.
See [VM testing](../docs/CLEAN-ROOM-VM.md) and [validation results](../release/VALIDATION.md).
