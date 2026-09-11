# Workspace overview

`ctlstoverview` is a resident C/GTK4 layer-shell carousel. Each card represents
one Sway workspace, including its tiled and floating windows. Home and shell
surfaces are excluded.

## Navigation

Open with `Super+O`, the status bar's Recent apps button or a slow bottom-edge pull.

- Drag horizontally, use the wheel or press Left/Right to select a workspace.
- Tap/click the centered preview or press Enter on the canvas to open it.
- Native Previous/Open/Close/Next controls support Tab/Enter/Space.
- Done, Escape or a downward pull dismisses Overview without closing applications.
- Close, the card's close control or an upward card swipe closes the entire
  selected workspace group. Release past 110 logical pixels commits a 180ms
  exit animation; shorter pulls cancel.

Partial previews do not capture keyboard focus or accept card activation.
Committed Overview owns the keyboard until hidden. Controls pause during close
animations to prevent actions against a moving target. Opening a workspace
preserves its existing tiled/floating arrangement.

Landscape outputs at least 640 logical pixels wide place navigation in the
header; portrait uses the footer. Cards fit previews without cropping.
Empty/loading/error states retain Done/Escape.

## Rendering and asynchronous refresh

GTK snapshot nodes transform retained card textures. Cairo rasterizes static
headers and fitted previews once at output scale. The default renderer is
OpenGL; `GSK_RENDERER` can select a diagnostic fallback.

Only visible cards are baked. Textures more than two carousel positions away
are released; refresh, theme, size/scale, hide and shutdown invalidate caches.
Cold texture baking occurs on the UI thread.

Workspace discovery, desktop lookup and image decoding run in a single
background task. Requests coalesce and carry generation/cancellation state.
The main thread adopts results and owns GTK/Pango/GSK objects. Late results
cannot replace an active drag/close or a newer view.

Loading disables stale card actions. Failed/malformed discovery displays
**Couldn't load apps** and **Reopen to retry**; a valid empty result displays
the empty state. Close and reopen to retry.

## Preview cache

`scripts/workspace-card-cache` captures last-seen workspace images separately
from the renderer. Workspace/window/output events schedule a debounced capture.
After capture, the helper rechecks workspace identity, geometry, membership and
event generation before replacing the previous image.

`$XDG_RUNTIME_DIR/ctlstoverview.capture` records an active/idle generation from
the first partial pull through closing. On unmap, Overview sends
`ctlst-overview-hidden` through Sway's tick channel so capture can resume.
Malformed capture state blocks capture until reinitialized.

These files are runtime state, not editable preferences. Update Overview and
the cache helper together. Cached images may contain sensitive content;
this coordination is not a secure capture boundary.

Previews are not live mirrors. Opening before the 300ms debounce/capture
completes can display the previous image, and inactive workspaces retain their
last capture until revisited. Overview does not visit other workspaces or wait
for screenshot I/O to open.

## Application names

Headings use GIO desktop-entry display names, XDG directory precedence and
localized names. Unregistered IDs use a readable suffix fallback. Distinct
app IDs remain distinct even when they share a display name.

To override a name, copy the complete desktop entry under its original filename
into `$XDG_DATA_HOME/applications/` (normally `~/.local/share/applications/`).
Edit `Name=` or its locale-specific field; `X-GNOME-FullName=` takes precedence
when present. Keep other fields intact. Reopen Overview, or restart it if GIO
has not detected the change.

## Reduced motion

Overview follows GTK's effective `gtk-enable-animations` preference and, when
available, `gtk-interface-reduced-motion`. Reduced motion completes settling,
keyboard/wheel selection and open/hide transitions on the next frame. Direct
finger/pointer tracking and cancellation remain active.

For a standalone GTK setup, merge this into `~/.config/gtk-4.0/settings.ini`:

```ini
[Settings]
gtk-enable-animations=false
```

Restart Overview or the session after editing. A desktop settings service can
override the file; configure its animation preference when applicable.
This setting affects Overview's animation handling, not GPU selection.

## Development

Build with `make` in this directory or `make core` from the repository root.
Run `make check` and the [installed-session test](../docs/CLEAN-ROOM-VM.md).
Test grouped close scope, floating members, stale refresh cancellation,
theme/resize, reduced motion and all input routes.

`$XDG_RUNTIME_DIR/ctlstoverview.fps` reports snapshot submission timing, not
optical display FPS. See [validation results](../release/VALIDATION.md) for
tested environments and hardware limitations.
