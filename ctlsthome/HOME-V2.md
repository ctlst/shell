# Home layout and widget host

`ctlsthome` is the C/GTK4 launcher and widget host. Its implementation is split
between `ctlsthome.c`, configuration, paging, rotation, descriptor and widget
runtime modules. Build from the repository root with `make core`.

## Layout

Positions are stored in portrait-logical grid coordinates. The default grid
has five columns and seven rows:

- Every page supports widgets and apps throughout the available grid.
- The favorites tray stays at the bottom in portrait and becomes a vertical
  strip on the right in landscape. Icons remain upright.
- Saved positions are retained across rotation and entry into edit mode.

The tray is part of the scaled edit preview. Drag apps between it and Home
pages to change membership and order; an explicitly empty tray stays empty.
On a new layout, the portable default includes the system's default HTTPS
browser when one exists. It does not seed missing phone applications. Layout
changes are saved in the existing user Home layout; they do not overwrite
application-role configuration. Editing uses a compact Wallpaper/Widgets/page/
Done row; direct keyboard layout editing remains incomplete.

At a Glance shows battery, audio output, Wi-Fi, cellular signal, RAM and CPU
with matching icons, reflowing by available size. Missing providers show an
unavailable state. Calendar stays a month grid and refreshes after local-date
changes, including time synchronization after startup. Optional EDS event-day
markers and Photos/Now Playing widgets are described in
[extra widgets](../docs/EXTRA-WIDGETS.md).

Home suppresses clock/status/widget redraws during paging and while hidden,
then applies the newest pending update. Helper writes are bounded and
asynchronous. Image thumbnails are loaded when accepting a scene at rest,
not from the draw callback. This does not guarantee a particular frame rate.

Page dragging keeps the newest touch offset and applies it once per GTK frame.
It does not debounce or ease finger tracking. Paging translates the retained
GTK/GSK allocation and reuses unchanged child layout. Picking, edit guides, drop
targets and paint share the same transform throughout motion.
Release cancels queued touch motion and settles from the last painted offset.
Hiding Home cancels pending motion. No timer remains running for a settled pager.

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

`Super+PageUp`/`Super+PageDown` select the previous/next page while Home is visible,
including in edit mode. These use the animated pager, do not wrap or create
pages, and ignore requests during a drag, picker or ongoing transition. The
`ctlst-shortcut page-prev` / `page-next` helpers send `[` / `]` on the private
Home datagram socket. Rebind them in `~/.config/ctlst/sway.conf`; see
[input controls](../docs/INPUT-PARITY.md).

Plain arrows move a visible selector among icons and widgets on the current
page. Enter/Space uses the same activation as touch; text entries and nested
widget controls keep their own keys. Keyboard layout editing is still limited.

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


### Swipe performance checks

Use [home-pager-frame-test.c](../tests/home-pager-frame-test.c) in a disposable
Wayland session to check input bursts, child layout/paint reuse, release
continuity, destination picking, cancellation and edit coordinates. It includes
the production Home source; link the other Home modules and the dependencies
listed in the component Makefile. Keep private screenshots outside Git.

Use a frame-clock update for direct manipulation, not a trailing debounce timer.
Defer expensive content refreshes until the gesture settles and apply only the
newest pending state. Keep retained content and stop frame callbacks at rest.
Validate rendered bounds and tap/drop coordinates after movement and rotation;
measure real hardware separately from software-rendered VM correctness tests.
