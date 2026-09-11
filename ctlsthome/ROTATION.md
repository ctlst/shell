# Home rotation and responsive content

Home remains visible while upstream Sway transforms the output. GTK allocation
callbacks recompute layout using the current width and height. There is no
black cover, frozen screenshot or separate Home rotation animation.

Positions remain in portrait-logical coordinates. The first-page widget cluster
stays upright; the app strip transposes to the side in landscape. Picking and
drag coordinates use the same allocation transforms as rendering.

## Compact widgets

Short widget slots reduce content density:

- Weather retains its condition and temperature with a smaller icon.
- At a Glance hides its heading/status row and retains its primary text.
- The clock uses a compact representation when space is limited.
- The calendar displays a compact date when the month grid cannot fit.

The normal presentation returns when the slot grows. Compact-class changes
invalidate descendant measurements before allocation, so the first frame uses
the new content size.

Configure thresholds in `~/.config/ctlst/home.yaml`:

```yaml
calendar:
  month_min_height_px: 160
widgets:
  compact_height_px: 96
```

Values are logical pixels. The calendar height accepts 40–600 and the general
compact threshold accepts 40–300. Row/column thresholds also affect calendar
density. Send Home `T` or restart it to reload.

Compact styling uses `.home-widget-compact`, `.home-weather-icon`,
`.home-weather-temp` and `.home-glance-dot`. User CSS can override these through
`~/.config/ctlst/theme-style.css` followed by `ctlst-session reload`.

External widgets receive configure/size notifications and must adapt their own
content. Extremely small custom layouts may require additional density rules.

## Compatibility and testing

Older `O`/`C` control messages remain supported: `O` is acknowledged immediately
and `C` requests a layout refresh. Legacy `rotation.transition` and timing/scaling
keys remain parse-compatible but do not animate Home; `spring-snap` is deprecated.

Test mode, transform and scale changes, including size changes with the same
aspect ratio. Verify content bounds, hit targets, saved layout preservation
and the first painted frame after resize. Layout tests do not validate a
device's rotation sensor or physical GPU performance.

See [Home configuration](HOME-V2.md), [VM testing](../docs/CLEAN-ROOM-VM.md)
and [platform support](../BETA.md).
