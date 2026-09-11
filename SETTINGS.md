# Settings

Settings is a Python/GTK4 application for shell appearance, device controls and
diagnostics. It uses OpenGL rendering by default, native kinetic scrolling and
asynchronous helper calls.

## Navigation

The category index groups Appearance and Wallpaper under Personalize,
Connections and Display & sound under Device, and Shell memory under Advanced.
Detail pages are created when opened.

Back, Escape, platform Back and Alt+Left return to the index. Escape at the
index closes Settings. Selector popups consume Escape before page navigation.
Returning to the index restores focus and scroll position for the category.

Touch, pointer and Tab/Enter activate the same controls. Long names ellipsize in
closed selectors and wrap in their popups; stored values are not truncated.
Back/Close remain available during operations.

## Appearance

Selecting a theme creates a draft. **Apply theme** saves and reloads it;
**Refresh themes** reads the catalog and preserves an unapplied selection.
The saved theme is shown separately from the draft. Missing/malformed catalogs
and unavailable names are reported without selecting an arbitrary replacement.

Catalog/current reads use ten-second timeouts; Apply allows thirty seconds
before readback. Success requires helper success and a matching saved selection.
It does not guarantee every process has repainted. Partial reloads and ambiguous
timeouts display recovery information; the UI does not automatically retry or
roll back.

## Wallpaper

Collection selection and **Choose from files** create a preview draft.
**Apply wallpaper**, fixed below the scrolling page, saves or imports it.
Refresh reads only. Pending image/placement choices survive refresh and page rebuilds.

Preview decoding runs outside GTK's main loop. A generation guard rejects stale
results; only the latest selection is queued. Thumbnails fit within 640×640
while preserving aspect ratio. This is not a limit on codec peak memory or a
sandbox for untrusted files.

Crop uses cover; Fit and Center show the complete image. The preview is not a
pixel-exact simulation of every output crop. Missing images clear the preview
and display an error.

Apply snapshots the image and placement, invokes the helper and reads state
back. Success requires matching target/mode values. Imported files become
managed entries independent of the original source path. Direct helper calls
use ten-second timeouts. Appearance and wallpaper Apply operations are mutually
exclusive.

The file picker is GTK's native image chooser; no CTLST file-manager companion
is required. Cancel does not stage a new file. Late replies from closed/rebuilt
views are ignored. GdkPixbuf's GI runtime is required for previews.

## Device controls

Display & sound queries screen timeout, sound profile and rotation lock on a
worker. Controls are disabled while the operation is pending. A generation guard
rejects updates for closed/rebuilt views.

After attempted writes, Settings reads device state again. Verified success
requires matching readback and helper success. Errors do not trigger automatic
rollback: the provider may have partially applied the request. Refresh retries
reads only. Unreadable or missing providers display **Unavailable**.

Each direct helper call has a ten-second timeout. Closing Settings does not
cancel accepted work; the application waits for its worker. A timeout does not
guarantee downstream services stopped. In-call sound status may not identify
the ringing profile, which is then shown as unavailable.

## Memory diagnostics

The memory page reports available/total RAM, swap, Sway PSS and RSS in MiB;
tooltips retain byte counts. Available RAM includes reclaimable pages.
PSS apportions shared pages; RSS counts them in full. Missing values display
Unavailable rather than zero.

Reads run on a worker with a ten-second timeout. Repeated refresh requests
coalesce, and results for closed pages are ignored. Refresh is read-only.
Warnings are advisory and do not tune memory or restart the compositor.

**Reload shell configuration** is a separate session action. It reloads
configuration/styling, not application memory. Its immediate status reports
that the action was requested, not that every component has acknowledged it.

## Styling

Edit `~/.config/ctlst/theme-style.css` and run `ctlst-session reload`.
The generated stylesheet is monitored for updates.

Useful selectors include `.settings-toolbar-title`, `.settings-title`,
`.settings-back`, `.settings-close`, `.settings-card`, `.settings-row`,
`.settings-category-icon` and `.settings-category-chevron`.
Defaults use an 18px detail title, 30px index title and at least 44px-high
Back/Close targets. Category icons come from the system icon theme.

See [configuration precedence](docs/ARCHITECTURE.md).
Run `make check` and the [installed-session test](docs/CLEAN-ROOM-VM.md)
when changing interactions. Physical GPU behavior, accessibility scaling and
device-provider operations require separate validation.
