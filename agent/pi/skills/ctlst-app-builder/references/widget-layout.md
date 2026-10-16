# Widget typography and layout

Read this when creating or changing a Home widget's scene. These are defaults
for information cards; a clock, game or explicitly requested display design
may use a different hierarchy after checking its actual rendered bounds.

## Work in the allocation, not screen pixels

Home's `hello` and every `configure` provide the widget's **logical** width and
height. For information cards, set `view.width/height` to those values and
recompute positions on configure. Keep type sizes stable as the card grows;
use the extra room for spacing, columns or more detail. Do not multiply by the
phone's display scale or derive font sizes as a percentage of the card width.

A fixed view is useful for a game board or illustration, but Home scales
**everything**, including text, by:

```
scene_scale = min(allocation_width / view_width,
                  allocation_height / view_height)
effective_font_px = node_size * scene_scale
```

For example, size 15 in a 100×100 view becomes 60 logical pixels in a 400×400
card. That is why a harmless-looking JSON font size can become a billboard.
Do not copy a normalized game canvas into a data card without compensating.
The editor may further scale the entire Home preview; do not counteract it.

## Use a small type hierarchy

Suggested effective logical pixel sizes, before the Home edit transform:

| Role | Starting size |
| --- | --- |
| Source, timestamp, secondary metadata | 12–13 |
| Title, field label, normal content | 14–16 |
| Main numeric value | 22–28 |

Use at most three sizes in a compact card. Prefer normal text and one bold
emphasis. Values above 32px need a deliberate display role, not automatic
scaling. Keep important content at least 12px; if it cannot fit, remove less
important content or use a compact layout instead of shrinking everything.
Compare the result to Home's built-in Weather and At a Glance text.

## Text positions and bounds

- `y` is the **top of the Pango layout**, not its baseline or vertical center.
  Reserve the full line height below it (start with roughly 1.3× font size,
  then verify with the actual font). Center using the measured line height.
- `align` only controls horizontal anchoring. Right-aligned text extends left
  from `x`; centered text extends on both sides. Neither reserves a column.
- Protocol v1 has no text-box width, automatic wrapping, ellipsis, or clipping
  to a row's background rectangle. Unknown fields such as `max_width` do not
  add those features. The helper must choose/truncate/wrap strings itself.
- Allocate nonoverlapping text columns and an 8–12px gap. Measure with Pango
  `sans` at the emitted absolute size/weight where available; treat character
  counts only as a conservative fallback, then inspect a real rendering.
- Give the card 12–16px inner padding where space permits; use 8px in compact
  cards. Keep the title, values and footer inside that padding. Rounded corners
  and clipping are not a substitute for fitting the contents.
- Do not put two independent labels on one header row unless their combined
  widths plus the gap fit. Stack, shorten or omit the secondary label instead.

## Resize by changing the composition

Define a compact form at the minimum useful allocation and a roomier form at
the default span. In short landscape cards, use side-by-side values and omit
secondary metadata as needed. Keep the primary value and its meaning visible.
Choose descriptor minimum spans that actually support this compact form.
Use shared `@ctlst_*` colors; retain at least 44×44 logical-pixel touch targets
for interactive controls. Bound remote strings and show an explicit missing
or stale-data state rather than manufacturing a value to fill the design.

## Finish with inspected screenshots

Protocol validation proves the message format, **not visual fit**. Before
calling a widget finished, capture and inspect screenshots of actual Home at its minimum,
normal and short landscape allocations, with the longest expected title,
price/value and error/status text. Look specifically for colliding headers,
values below their row, clipped footers and fonts much larger than native
Home labels. Check the whole composition too: balanced padding and visual
weight, even row/column gaps, aligned icon/value pairs, and no unexplained empty
region. A widget does not need to repeat its picker name as a visible heading.
If removing a heading or secondary content, redistribute the remaining content
instead of leaving a vacant row. Check both populated and empty states where
they change the composition. Fix the geometry and recapture the affected case
until the screenshot is balanced, not merely free of clipping.

For a typography-only change, use the protocol validator and focused captures;
do not add a broad test suite. If a live rendering is unavailable, report that
limitation instead of claiming the layout is visually verified.
