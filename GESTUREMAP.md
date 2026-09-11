# Gesture reference

`ctlst-gestured` routes touch input to shell navigation and window-management
actions. Its passive feedback surfaces do not execute actions themselves.
See [input controls](docs/INPUT-PARITY.md) for pointer/keyboard equivalents.

## Configuration

Edit `~/.config/ctlst/gestures.conf` and reenter the session:

```ini
[drawer]
pull_bottom_inset_px = 96
tracking_gain = 1.0
commit_distance_px = 72

[shade]
tracking_gain = 1.0
commit_distance_px = 96
home_pull_start_fraction = 0.40

[dock]
scroll_detent = 0.8
scroll_rate_limit_ms = 90
```

Distances are logical pixels; the Home pull fraction is relative to output
height. Tracking gain 1.0 follows input one-to-one. Commitment is separate from
tracking, so a sheet can settle open without dragging it across the full screen.
Output/touch/haptic selection and natural scrolling are configured in
`input.conf`. Hardware feedback depends on an available provider.

## Start regions

| Region | Motion | Action |
| --- | --- | --- |
| Top bar | Down | Progressive Quick Settings pull |
| Upper 40% of visible Home content | Down | Quick Settings pull; expanded region applies only on Home |
| Lower-middle content on Home or a windowless workspace | Up | Progressive app drawer pull |
| Physical bottom edge | Quick upward flick | Home |
| Physical bottom edge | Slow upward pull | Overview preview and commit |
| Bottom charm | Horizontal scrub | Step through tasks |
| Bottom charm | Hold at least 200ms | Reveal task dock |
| Bottom charm | Short tap | Pass through; does not navigate Home |
| Home content, including apps/widgets | Horizontal | Home page drag |
| Either physical side edge | Inward | Back |
| Ordinary application content | Horizontal/vertical | Application input outside explicit shell regions |

Drawer pulls begin in the center 80% of output width, below half the output
height and above `pull_bottom_inset_px`. If an application window occupies the
workspace, that region remains application-owned.

## Bottom-edge Home and Overview

The bottom-edge region uses the session's bottom-bar geometry. Horizontal
intent over the charm remains task scrubbing; vertical intent selects the
Home/Overview path.

1. Upward travel past 10% of output height establishes Overview intent and
   blocks click-through for the remaining sequence.
2. Preview progress spans roughly 10–45% of output height.
3. A qualifying quick flick goes Home, even after passing the Overview commit
   distance. The default time threshold is approximately 360ms; velocity can
   also qualify the flick.
4. A slow release at or beyond 22% of output height commits Overview.
5. A shorter slow release retracts the preview without opening it.

A quick Home action dismisses applicable overlays, leaves fullscreen and
navigates Home without closing application windows. The privacy cover blocks
ordinary Home navigation. Editing/picker state routes the bottom swipe to Home
rather than Overview and returns to the first Home page.

Protected shell/provider surfaces block a new Overview gesture. An already
claimed gesture retains ownership until release. Fullscreen applications can
use the explicit bottom-edge pull.

## Drawer, Shade and Overview dismissal

- Drawer handle: downward pull closes the drawer.
- Shade: upward content drag closes when the viewport was already at its bottom
  or all content fits. Otherwise it scrolls. Brightness retains its own drag.
- Shade header/handle, Close and Escape offer independent dismissal; Escape
  first leaves a detail page.
- Overview: downward pull dismisses; horizontal drag selects cards.
- Overview card: an upward drag past 110 logical pixels commits group close
  through a 180ms exit animation. A shorter drag cancels.
- Privacy cover: upward drag from anywhere follows the finger; a short gesture
  settles back. Continue, Return and Space also dismiss. It is not a secure lock.

Partial Drawer/Shade/Overview previews do not claim keyboard focus.
Committed surfaces release focus when hidden.

## Home editing

Long-press enters editing. The live page scales above the toolbar; GTK
allocation transforms align rendering, picking and drag coordinates.
Dropping outside the preview cancels the move. The picker blocks hit tests
against underlying items.

App/widget remove controls act only on release within the target. Leaving the
target or ending edit mode cancels removal. Done or a Home action exits editing.
See [Home layout](ctlsthome/HOME-V2.md).

## Window gestures

| Region | Gesture | Action |
| --- | --- | --- |
| Titlebar | Double tap | Fullscreen |
| Titlebar corner | Tap | Toggle floating |
| Tiled titlebar | Hold, then drag | Rearrange or choose a workspace destination |
| Floating titlebar | Hold, then drag | Move or choose a workspace destination |
| Titlebar | Quick horizontal flick | Move/follow into an adjacent or new app task |
| Titlebar | Long horizontal drag, about 28% of output width | Task transfer |

The move overlay shows nearby workspace targets, New, side destinations and
a separate Close window target. The daemon and overlay share target geometry.
Home is not a move destination. See [window management](WINDOW-MANAGEMENT.md).

## Multi-touch and action fan

| Fingers | Motion | Action |
| --- | --- | --- |
| Three | Horizontal | Switch workspace |
| Three | Up | Open Overview |
| Three | Down | Toggle keyboard |
| Four | Horizontal | Move focused window to adjacent workspace |
| Four | Up | Toggle floating |
| Four | Down | Toggle fullscreen |

The action fan offers Keyboard, Gamepad and Close window. Keyboard and Gamepad
need compatible external providers. Selection feedback is passive; release
executes through the gesture daemon.

## Context click

A stationary hold of approximately 550ms in eligible application content
emits a right-click through Sway's virtual pointer. Movement beyond touch slop
or a second finger cancels it. Shell surfaces, bars, keyboards and excluded
application IDs retain their native behavior.

Case-insensitive exclusion globs are read from
`~/.config/ctlst/right-click-exclude` at gesture start, without a daemon restart.

## Ownership and testing

Active gestures retain ownership through release. Explicit edge regions take
priority over application content. Home paging and vertical drawer pulls have
separate intent regions; changing a tentative drawer pull into horizontal
motion retracts it. Other touches remain application-owned.

When changing thresholds or routing, update this reference and
[INPUT-PARITY.md](docs/INPUT-PARITY.md), add cancellation tests and run the
[installed-session checks](docs/CLEAN-ROOM-VM.md). Test provider-specific actions
on configured hardware separately.
