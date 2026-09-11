# Multitasking overview

Resident GTK4/layer-shell workspace carousel. One card represents a workspace,
including all its tiled and floating windows. Home and shell surfaces are excluded.

Sway reports floating application leaves as `floating_con`. Both Overview and
the automatic preview cache recognize those leaves, including when a floating
app has focus. Mixed/all-floating groups retain their full app identities,
window counts and owned close targets instead of silently dropping the floating
members. Opening the workspace does not retile it. This source follow-up adds
no renderer, animation, dependency, binding or setting.

## Rendering

The canvas is a GTK snapshot widget, not a GtkDrawingArea. Cairo rasterizes a
card's static header and fitted preview once at the output scale; GSK then
translates/scales the retained GdkTexture during gestures. The normal renderer
is OpenGL; an explicit GSK_RENDERER override is respected for software-only
VMs and diagnostics.

Only visible cards are baked, and textures beyond two carousel positions are
released. Refresh, theme reload, size/scale changes, hiding, and shutdown clear
the cache. Rotation retains the selected workspace and recalculates spacing.
Settling uses the GTK frame clock; live horizontal and vertical card drags
follow the input directly. Workspace discovery, desktop-name lookup and PNG
decoding run in a single background refresh task, not GTK's UI thread. The
main thread adopts the completed snapshot and retains ownership of GTK/Pango/
GSK objects. Cold texture baking still happens on the UI thread; this is not
a claim that every source of first-frame latency is eliminated.

Requests coalesce. A newer request cancels the previous query; hide/shutdown
cancel it and invalidate its generation. A late result cannot replace a newer
view or a card being dragged/dismissed. Gesture cancellation releases deferred
refresh without closing a task. The post-close refresh timer is tracked and
removed on hide/shutdown, so it cannot outlive the UI or resurrect a pre-close
snapshot. No new polling loop, daemon, dependency or configuration key is added.
This uses GLib's existing [background task support](https://docs.gtk.org/gio/class.Task.html).

While discovery is pending, the loading state and Done/Escape remain available;
stale cards cannot be opened or closed. Query failure or malformed output shows
**Couldn't load apps** with **Reopen to retry**, not a false **No open apps**.
Close and reopen Overview to retry. Valid empty results retain the normal
empty state. Cached images are still last-seen screenshots, not live windows.

The opaque backdrop and card chrome use the generated semantic panel/text
colors. Previews fit without cropping; compact headers show application names
and window counts instead of internal workspace identifiers. Screenshot capture
remains the separate private workspace-card-cache service.

Output changes (rotation, mode and scale) schedule that service's existing
debounced capture, alongside workspace/window events. Capture now verifies the
focused workspace's identity, geometry and window membership again after grim,
and rejects candidates if a relevant event arrived while the worker ran.
The previous image stays intact on rejection or capture failure; temporary
images are removed instead of publishing pixels under the wrong task.

Overview's internal `$XDG_RUNTIME_DIR/ctlstoverview.capture` records a private
active/idle generation. It is active before the first partial-pull frame and
remains active through the closing animation. The persistent generation lets
the cache detect even an open-and-close while grim is in flight; the committed
`ctlstoverview.visible` marker keeps its existing input-routing meaning.
Actual unmap emits `swaymsg -t send_tick ctlst-overview-hidden`, which the
existing cache subscribes to and debounces. This resumes capture after a
cancelled partial pull even when no workspace/keyboard-focus event occurs.
Unrelated tick payloads are ignored. No screenshot polling or new daemon.

The runtime file is implementation state, not an editable preference. A
malformed/unreadable state blocks capture; restarting Overview initializes a
new idle generation after a crash. Legacy Overview without the file retains
the old committed-marker guard, so update Overview and the helper together for
the full transition protection. This is a correctness guard, not a secure
capture or privacy boundary for arbitrary Wayland applications/overlays.

Previews remain last-seen images, not live mirrors. Opening Overview before the
300ms debounce/capture completes can still show the prior image, and inactive
workspaces keep their last capture until revisited. The switcher does not wait
for screenshot I/O, rotate old image pixels, or reveal another workspace to
refresh it. Restart the cache service/session after updating its script; there
is no new configuration key or provider requirement.

`tests/test_workspace_capture_races.py` exercises interrupted captures,
workspace/layout/member changes, event-generation roundtrips, malformed state,
shutdown and successful private replacement. Four regressions fail against the
old helper. `tests/overview-capture-state-test.c` verifies private unique native
state transitions. The portable `vm/clean-room/preview-rotation-ui.py` accepts
`--capture-races`: an explicitly armed test wrapper delays real grim until a
partial Overview is on screen, then checks rejection, closing protection and
automatic fresh capture after cancellation/rotation. No installed VM files or
real telephony providers are modified. This source follow-up is not deployed
to the phone; screenshots remain last-seen and physical frame timing needs QA.

The portable package's actual `sway/ctlst.conf` now starts that already-shipped
service through the component launcher. The legacy Pixel session already did.
Fresh-session integration tests require newly captured private PNGs for each
launched app; they do not seed previews with the helper's `once` command.

## App identity

Card headings use the same GIO desktop-entry display name as the drawer.
For example, `dev.ctlst.Dialer` is shown as **Phone**, not Dialer. Lookup
accepts an exact desktop ID with or without `.desktop`, follows XDG data
directory precedence and locale-specific names, and never launches the entry.
Unregistered or mismatched IDs retain a readable ID-suffix fallback; this is
not fuzzy matching of window titles or executable names.

To customize a name, copy the application's complete desktop file into
`$XDG_DATA_HOME/applications/` (normally `~/.local/share/applications/`)
under its original filename. Edit its `Name=` or localized name; if it has
`X-GNOME-FullName=`, that display-name field takes precedence. Keep the other
fields intact. Reopen Overview to rebuild cards; if GIO has not noticed the
file change yet, restart Overview. No shell rebuild or custom name registry
is needed. Long names remain valid UTF-8 inside the bounded card heading.
Apps with identical display names are still counted by their distinct IDs.
Lookups happen during card rebuild, not during rendering or gesture frames.

## Interaction

Existing shell bindings open the overview (Super+O in the packaged session).
Mouse drag and touch use the same GTK gesture handlers: horizontal navigation,
downward dismissal, and upward card dismissal. Clicking/tapping the centered
preview focuses the workspace; its top-right close control closes that entire
workspace. A short upward drag cancels; passing 110 logical pixels commits a
180 ms exit animation before any app is closed. IPC P/S/H/R is unchanged.

The modern-UI branch adds a Recent apps heading, larger fitted cards and
reserved footer space. Once committed, Left/Right or wheel steps select a
task; Enter on the canvas opens it. Native Previous/Open/Close/Next and Done
buttons support Tab/Enter/Space, with visible focus and accessible labels.
Escape or Done dismisses without closing an app. Keyboard ownership is
exclusive only while committed and is released when hidden. Selection/open
controls pause during a close animation to avoid acting on a moving target.

On landscape outputs at least 640 logical pixels wide, the navigation controls
share the header band. This preserves preview height on short displays without
covering the separate task dock. Portrait keeps its bottom controls. A
compact landscape heading band starts the cards higher, and landscape widths
are bounded to 220–420px rather than stretching with the display. At 640×320
this gives a single-window preview about 106px of height instead of 72px,
while preserving the full image aspect ratio. These are responsive layout
defaults, not a new device profile or snapshot cropping mode. A
single-window card omits the redundant window-count line; grouped cards still
show their count. The card's top-right close target now uses the same 180ms
dismissal as the native Close button and upward swipe. Preview-only and
already-dismissing cards ignore clicks.

New presses reset movement suppression from the previous gesture. GTK can
deliver a click release before that press's zero-distance drag end; the latter
must not cancel an already-started close. The native test exercises that event
ordering. Control relocation happens during allocation, never mid-snapshot.
When no tasks remain, the navigation toolbar is hidden and a centered empty
state replaces the cards; Done/Escape and the separate Home dock remain usable.
The canvas exposes the selected task or empty/loading state to accessibility.

## Reduced motion

Overview now follows GTK's effective `gtk-enable-animations` preference and,
where available, its GTK 4.22 `gtk-interface-reduced-motion` preference. The
normal defaults are animations enabled and no motion-reduction preference.
With either requesting reduced motion, opening/hiding, keyboard
or wheel selection and card-release settling reach their final state on the
next frame. Live touch/mouse pulls still track directly; a short upward pull
still cancels and a committed close still runs through the same deferred
backend path. Changing the effective preference during a settle finishes that
settle on its next frame. This does not disable GPU rendering or add polling.

For a standalone GTK setup, edit the existing
`$XDG_CONFIG_HOME/gtk-4.0/settings.ini` (normally
`~/.config/gtk-4.0/settings.ini`), retaining its other entries:

```ini
[Settings]
gtk-enable-animations=false
```

Restart the resident Overview process/session after editing the file; do not
assume a theme reload makes GTK reread it. A desktop settings service can
override the file: Overview follows the effective GtkSettings property.
For example, the Arch VM's desktop source must also be changed with
`gsettings set org.gnome.desktop.interface enable-animations false`;
the GTK dotfile alone does not change the effective preference there.
That command requires the corresponding installed schema/settings backend,
not GNOME Shell. See [GTK settings](https://docs.gtk.org/gtk4/class.Settings.html)
and [system reduced motion](https://docs.gtk.org/gtk4/property.Settings.gtk-interface-reduced-motion.html).
This is GTK's existing preference, not a new CTLST config key. Other custom
shell animations still require their own adoption/verification.

The legacy Pixel theme installer currently replaces GTK settings files during
theme application. On that path, apply the theme first, then edit this setting
and restart the session. Preserving the preference across that legacy install
path remains an integration gap; portable theme reload does not install that
generated GTK settings file.

## Validation

Build with make (-Wall -Wextra -Werror). Run the workspace-card overview Python
contracts and, inside a disposable Wayland session, compile/run the native test:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  $(pkg-config --cflags gtk4 gtk4-layer-shell-0 json-c gio-unix-2.0) \
  ../tests/overview-render-test.c \
  $(pkg-config --libs gtk4 gtk4-layer-shell-0 json-c gio-unix-2.0) -lm \
  -o /tmp/overview-render-test
GSK_RENDERER=cairo /tmp/overview-render-test
```

Build `tests/overview-identity-test.c` with the same libraries to verify
real desktop-entry resolution, user overrides, localized/full names,
unregistered fallbacks, UTF-8 boundaries and same-name/different-ID grouping.
That fixture needs no display and creates/removes only its private XDG tree.
`gio-unix-2.0` is part of the existing GLib dependency, not a new service.

Build `tests/overview-groups-test.c` with the same libraries for a display-free
grouping regression. It parses tiled, floating, nested and Xwayland leaves,
checks workspace ownership, Home/shell exclusions, focused ordering and the
complete card ID lists used by Close. The old collector fails this test.
Python tests also exercise automatic capture with a focused floating leaf and
retain the Home/shell/visible-overlay guards. These tests never close real apps.

Build `tests/overview-refresh-test.c` with the same libraries and run inside
the disposable Wayland session. Its private fake swaymsg verifies a responsive
main loop during slow discovery, actual PNG ownership transfer, request
coalescing, cancellation/new-generation adoption, gesture deferral/cancellation,
query failure versus empty/retry, and shutdown with a pending task/timer.
It never contacts the real compositor or closes applications. The lifetime
fixture also passes AddressSanitizer/UndefinedBehaviorSanitizer; GTK process
leak reporting is disabled for that run.

`tests/overview-refresh-latency-test.c` is a shared before/after probe. With an
artificial 600ms query, the previous implementation processed zero 10ms
heartbeats and blocked one dispatch for about 615ms; the new Pixel and portable
builds processed 58 heartbeats with longest dispatches below 1ms. These are VM
event-loop measurements, not phone frame-rate claims. To compare an older
source, compile with `-DOVERVIEW_SOURCE='"/absolute/old/ctlstoverview.c"'`.
The async source follow-up is not yet deployed to the Pixel.

The native test checks actual texture retention/eviction, cache invalidation,
portrait/landscape bounds, direct drag tracking, and canceled dismissal.
It also checks keyboard/wheel selection, clamping and dismissal guards at
320–960 logical pixels. The Arch modern-UI probe uses separate fixture
workspaces and verifies keyboard and pointer navigation before dismissal.
Check real touch and GPU motion on the phone as well; a headless software VM
is not a hardware performance benchmark. ctlstoverview.fps in the runtime
directory reports snapshot submission rate/timing, not optical display FPS.

The portable `vm/clean-room/overview-ui.py` probe captures real Phone/Messages
demo windows through `workspace-card-cache`, checks private thumbnail files,
and exercises light/dark 320–960px layouts. It injects pointer, wheel and
persistent-device touch input as well as keys, including canceled swipes and
closing only its own demo windows. It never calls or sends texts. Its renderer
is deliberately software-only, so it cannot establish physical GPU smoothness.

Use `--expected-overview /absolute/build/ctlstoverview` with that driver to
launch a development build via the session's normal component PATH and check
the running executable's hash. No installed binary is replaced. Add
`--reduced-motion` to write/restore the guest's GTK preference and retain
separate artifacts. The probe also saves/sets/restores the desktop animation
preference when its schema exists, including restoring an absent user override.
The native fixture verifies GTK's effective value,
then tests both animated and immediate release behavior, including an
in-flight preference change, without sending a close command.

The fresh staged-session journey checks two-window tiled, mixed and all-floating
groups: merge Phone/Messages demos through the workspace manager, require a new
automatic private preview and both app names, open by portrait touch/landscape
keyboard, cancel a short upward drag, then explicitly close the group by pointer
while retaining the same unrelated Settings window. Both palettes pass; actual
Sway tree checks retain the original IDs, workspace and floating state. Fully
occluded members remain in the group. Bar activation now passes for each
arrangement with the starter global task list; the focused bar probe also checks
separate workspaces and fully overlapping windows. User-selected output
filtering retains upstream visibility behavior; crowded/multi-monitor bar QA
remains separate.
`vm/clean-room/preview-rotation-ui.py` additionally checks actual mode/transform
events, private fresh images matching the new workspace geometry, exclusion
while Overview owns the screen and refresh after dismissal. Field/Paper pass.
Snapshots may still retain their previous orientation if Overview opens before
the debounced capture finishes. Physical GPU/input remains separate.
