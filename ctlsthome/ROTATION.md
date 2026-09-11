# Ordinary Home rotation

Home keeps its live scene visible while upstream Sway transforms the output.
The cached screenshot renderer, black guard, zoom/overshoot and delayed
prepare/commit sequence have been removed. Older helpers can still send O/C
messages: O is acknowledged immediately and C requests a layout refresh.

The widget cluster remains upright and icons transpose into the side strip.
Every window-size change recomputes layout, including changes that keep the
same aspect. Icon preferences are capped by available cell dimensions.

Reflow now runs through real GTK allocation callbacks. GTK4 widgets do not
have `width`/`height` GObject properties, so the former `notify::width` and
`notify::height` connections never fired and orientation could lag until the
one-second clock refresh. A viewport host updates orientation before allocating
the root; the retained pager synchronizes strip proportions, icon sizes and
compact content before allocating its children. Both dimensions come from the
actual pager allocation, not its sibling's previous height. No black cover,
snapshot transition or polling timer is introduced. The CSS recovery path
resynchronizes its requests after the viewport is allocated.

The accelerated pager takes its height from the current allocation, not a
minimum-size request copied from the previous viewport. That lets it shrink
before deferred layout synchronization after a mode/theme change. Page widths,
the retained page gutter and input transforms are unchanged; the CSS recovery
path retains its explicit height request. This fixes the stale-height case,
not every content-minimum constraint in short/custom widget layouts.

When a card switches between full and compact content, Home now invalidates
its descendant size requests before allocating the grid. Previously Weather's
card padding switched immediately while its icon and temperature retained
full-size cached measurements for the first landscape frame. Invalidating the
page-strip parent alone did not fix this. The subtree update runs only when the compact
class changes, not on every frame or clock tick. Theme CSS, density thresholds,
normal card sizes, saved grid positions and pager input transforms are unchanged.

The first-painted-frame regression includes populated compact labels, launcher
icons and a short idle between resizes to settle GTK caches. Warnings are fatal,
and content bounds are checked at after-paint without triggering measurement.
The pre-fix build reproduces the 254px minimum inside a 238px viewport; both
updated source variants pass the same transition.

Short widget slots hide secondary Weather and At a Glance details; the clock
uses its compact representation when space is insufficient. The calendar uses
the compact date if the month cannot fit. These are presentation changes, not
changes to saved widget positions or data.

Compact Weather also uses a 28px icon, 24px temperature and 4px vertical
padding; it retains the condition and temperature. Compact At a Glance omits
its section heading and device-status row, retaining the event and supporting
text with a smaller decorative marker. Full cards regain their original
presentation when the slot grows. The existing `widgets.compact_height_px`
threshold controls this (default 96 logical pixels).

These sizes are ordinary CSS defaults under `.home-widget-compact`, including
`.home-weather-icon`, `.home-weather-temp` and `.home-glance-dot`. Weather's
image uses CSS sizing instead of a fixed GTK pixel-size override, so themes
can customize both sizes. Use the integration's normal theme CSS and reload
path; a palette or size override does not require recompiling Home.

Edit normal dotfiles at `~/.config/ctlst/home.yaml`:

```yaml
calendar:
  month_min_height_px: 160
widgets:
  compact_height_px: 96
```

Values are logical pixels. Calendar's valid range is 40–600; the general compact
threshold is 40–300. Existing row/column thresholds still apply. Reload Home
configuration with its T control message (or restart Home). Larger cards regain
their extra detail automatically. External widgets retain the protocol's
configure/size notifications and must adapt their own content.

Legacy `rotation.transition` and timing/scaling keys remain parse-compatible
but no longer animate Home; `spring-snap` is a deprecated alias for ordinary
rotation. User configuration and saved layouts are not overwritten.

## 2026-09-06 verification

- Native AArch64 Arch build with `-Wall -Wextra -Werror`.
- Matching phone/VM glibc 2.43 and GTK 4.22.4, gtk4-layer-shell 1.3.0.
- Repeated normal / 90 / 270 / 180 / normal rotations at 1080×2160, scale
  2.25, in the isolated Arch VM and on the actual Arch Pixel 3a XL.
- Real phone screenshots inspected: widget content and app icons remain within
  the portrait and landscape display. Home stayed alive with the same PID.
- Phone home.yaml and home-layout.json compare byte-for-byte with pre-update
  backups. Original normal orientation and unlocked autorotation restored.
- Only the phone's Home executable and set-output-transform helper were
  replaced, atomically, after backing up both. No reboot, compositor restart,
  package-manager upgrade, kernel/boot change, or unrelated app deployment.
- This is a reversible development hotfix to the current writable Arch system,
  not an immutable system release or pacman package version update.
- The older Alpine `vm/ctlst-vm test` was attempted but failed at the desktop's
  interactive sudo authentication. No Alpine binaries were used; the native
  Arch build and orientation checks above are the relevant executed gate.
- Physical hand-rotation feel and every external/custom widget size still need
  user exercise; framebuffer checks do not certify every animation frame.

Phone backup directory:
`~/.local/state/ctlst-home-backups/20260906-normal-rotation/`.

Deployed Home SHA-256:
`11014e332b763d8d4280abd7530bb6e2844c70a490e9c04e63d149b2c4f3294a`.

Deployed transform helper SHA-256:
`ff70a898acc8d6d9353d0f0bba224f8eb80005c923720aee27cfaf634d1ca570`.

Private screenshots, build/test logs and the VM probe are ignored local
artifacts under the Pixel checkout's `.build/rotation-review/`. They are not
public release assets.
