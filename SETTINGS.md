# Settings

Settings is the original Python/GTK4 shell configuration UI. It retains
GL rendering by default, native kinetic scrolling, ordinary helper interfaces,
and the separate Pixel/portable integration paths.

## Navigation and layout

The index groups Appearance and Wallpaper under Personalize, Connections and
Display & sound under Device, and Shell memory under Advanced. Detail pages
are created when opened. The index keeps its large title alongside Close in
one row, without a separate empty toolbar. Detail pages use a compact toolbar
title with a named Back button and Close. This saves vertical space on short
landscape screens without reducing the touch targets or adding resize timers.

Back, Escape, the platform Back key and Alt+Left return a detail page to the
index. Escape at the index closes Settings. Native selector menus consume
Escape first, without leaving the page. After a page transition, Back restores
focus to the originating category and scrolls it into view. Touch/pointer
activation and Tab/Enter use the same controls.

Selectors bound their closed labels with an ellipsis, but retain the full
string for accessibility and wrap full names in the popup. Long wallpaper or
theme names cannot force the main window wider than a phone. Supporting row
labels and memory diagnostics wrap rather than forcing horizontal overflow.
This does not truncate the wallpaper filename, theme name or stored value.

## Styling and behavior

The default toolbar title is 18px, index title 30px, and Back/Close have at
least 44px CSS minimum height. These are style defaults, not fixed device
coordinates. Customize .settings-toolbar-title, .settings-title,
.settings-back, .settings-close, .settings-card and .settings-row in the
normal theme CSS. Changing a palette does not require compiling Settings.

Index categories use named symbolic icons from the required system icon theme,
not bundled artwork. The five icons share a centered 34px tile (22px glyph plus
6px padding), with accent/soft-accent colors and a quiet secondary chevron.
The category text remains the accessible button name; icon colors are not the
only identification. Customize .settings-category-icon (padding, radius,
foreground/background), .settings-category-chevron and .settings-section-title
through the same CSS path. These are defaults, not fixed renderer geometry.
The heading shares its row with Close and has no extra top offset.

The portable app reads generated shared CSS, including
~/.config/ctlst/theme-style.css; run ctlst-session reload after an edit.
The Pixel app retains ~/.config/sway-touch/theme.css and the existing
themes.d/NAME/variant.css path; apply that theme and reopen Settings to review.
No user configuration or provider choice is migrated by this layout change.

### Appearance selection and application

Opening Appearance reads the catalog and current theme on the same guarded
worker mechanism as Display & sound. Browsing the selector changes only an
in-memory draft. Apply theme is the explicit save/reload action; it can also
reapply the current theme. Refresh themes reads only and retains an unapplied
choice, including after temporary catalog failures or a page rebuild.

The current saved theme is shown separately from the pending selection. Empty,
missing or malformed catalogs do not invent a fallback theme; an unknown
current selection is labeled Unavailable. A configured name outside the catalog
is displayed as-is, with Choose a theme until the user makes a valid selection.

Apply is single-flight and reads the catalog/current selection afterward even
if the helper fails or times out. Theme selection saved means the helper
returned success and its current selection matches the request, not that every
shell process has independently acknowledged a repaint. A partially successful
reload is described explicitly. No automatic retry or rollback occurs.
Back/Close remain available; closing the window does not cancel accepted work.

Catalog/current reads are bounded to ten seconds each, Apply to thirty seconds
before readback. A timeout does not prove downstream reload work was canceled.
Both variants keep their own helper, config and stylesheet-loading paths. The
Pixel version no longer reports success immediately after spawning its helper;
it still requires reopening Settings to load new colors. The portable app
retains its generated-stylesheet monitor. No theme files are migrated.

### Wallpaper preview and application

The wallpaper collection and current selection load on a worker. Missing or
malformed results show an explicit unavailable state rather than choosing the
first image. The current wallpaper is separate from the pending choice.
Selecting a collection item or choosing a local file only stages a draft;
Apply wallpaper is the sole save/import action and stays in a fixed bottom
action area while the page scrolls. Refresh wallpapers reads only. Pending
choices and placement survive refresh and page rebuilds.

Preview decoding is single-flight outside GTK's loop. A generation/selection
serial rejects stale results and queues only the latest requested image. The
retained thumbnail fits within 640×640, with aspect ratio preserved; this is
not a bound on a codec's peak decoding memory or a sandbox for untrusted images.
Missing/invalid previews clear the previous image and explain their absence.
Placement changes reuse the thumbnail: Crop uses cover; Fit and Center show
the full image. This is an image preview, not a pixel-exact simulation of
the output's crop or original-size placement. The default draft placement is
fill only when no readable current placement or existing draft is available.

Apply snapshots the image and placement, runs import/set outside GTK and reads
state back even after errors/timeouts. Verified success requires the returned
target/mode and readback to agree. A successful file import switches the draft
to its managed entry, so subsequent previews do not depend on the source file.
The helper's own validation, transaction/restore behavior, preferences and
storage roots remain unchanged. Direct helper calls are bounded to ten seconds;
timeout is not proof downstream work was canceled. There is no UI-level blind
rollback or automatic retry. Theme and wallpaper operations cannot be applied
over one another from Settings.

Portable Settings retains the native GTK file chooser; Pixel retains its
separate Files companion. Cancel stages nothing. Replies from closed/rebuilt
views cannot stage into the new view. Back/Escape now attaches once to the
window, not a focusable child subtree, so it still works after a pending
operation disables the focused button. Native popups consume Escape first.

Settings explicitly depends on the system GdkPixbuf GI runtime for scaled
previews (gdk-pixbuf2 on Arch, gdk-pixbuf on Alpine); nothing is bundled.
The [scaled image loader](https://docs.gtk.org/gdk-pixbuf/ctor.Pixbuf.new_from_file_at_scale.html)
and [texture conversion](https://docs.gtk.org/gdk4/ctor.Texture.new_for_pixbuf.html)
use the existing GTK stack. The latter is deprecated in newer GTK but retained
for the project's supported older GTK4 stack; a decoder migration is separate.

## Verification and remaining work

Display & sound now reads screen timeout, sound profile and rotation lock on a
worker, and uses that same single-flight path for changes. Only plain command
results cross back to GTK's main loop. Controls are disabled while work is
pending; the fixed header and detail page both show progress. Back and Close
remain available. A window/generation guard rejects late UI updates after a
close or rebuild; a rebuilt Display page resamples once the old job finishes.

After every attempted change, Settings reads all three states again. It does
not blindly undo a selection after failure: the command may already have
changed persistent state. Success is labeled verified only when readback
matches the requested value and the helper reports success. Errors and
ambiguous timeouts keep explicit recovery text without automatic retry.
Unreadable/missing/malformed states display Unavailable instead of a guessed
dropdown value or off switch. Refresh device settings retries reads only.

Each direct helper subprocess uses a ten-second timeout; a refresh queries
three helpers. Closing the window does not cancel accepted work: the app holds
until its worker finishes. A subprocess timeout is not proof that a child
service or partially completed write was canceled. Helper paths, state storage
and hardware effects are unchanged. In-call sound status currently reports
a call-specific class rather than the ringing profile, so that profile is
shown unavailable until it can be read reliably.

tests/settings-state-test.py adds delayed native regressions for main-loop
progress, duplicate attempts, errors with and without applied state, timeout
readback, malformed/missing helpers, Back, rebuild and window destruction.
Its helpers and writes are all inert. settings-state-ui.py captures the
recovery state in light/dark portrait and landscape, with --source available
for the Pixel variant.

The portable tests/settings-layout-test.py loads the actual application
definitions with inert helper responses and inert command-capable actions.
The settings-ui.py disposable Arch ARM driver checks all five detail pages in
field/paper at 320, 480, 640 and 960px widths, native popup bounds, and actual
pointer/touch/keyboard navigation. Long names are deliberate fixtures. The
Pixel source can be passed with --source; its test display gets a generated
palette without changing its production theme path or claiming reload coverage.

The native probe records the exact source hash and resolves every category icon
through GTK's installed theme; a symbolic-looking name alone is insufficient.
Index captures cover the same four widths, checking centered square icon tiles,
contained row bounds, touch-sized controls and the single-row heading. The old
Appearance and memory names fail this check as missing/non-symbolic icons.

These are layout/input and inert state checks, not live provider tests.
Appearance also has a separate portable real-helper VM probe,
tests/settings-theme-provider-test.py with settings-theme-provider-ui.py. It
applies paper/field through the installed helper and checks saved selection,
generated mode, live Settings mode update and GTK-loop progress. It restores
the guest configuration afterward; this is not Pixel helper or phone GPU QA.
Wallpaper has an inert native state/preview fixture (settings-wallpaper-test.py)
and a separate portable real chooser/import fixture
(settings-wallpaper-provider-test.py). The real fixture uses an owned temporary
image/store, verifies that selection does not import, then checks actual Apply,
readback and preview from the managed copy after deleting the owned source.
Its driver restores guest preferences afterward. The Pixel Files provider still
needs its own real integration check; native stale-reply guards are covered.

### Memory readings and session tools

Memory shows available/total RAM, swap use, Sway proportional memory (PSS) and
resident memory (RSS) in MiB. Metric tooltips retain exact byte counts. Available
RAM includes reclaimable pages; the usage meter is an estimate of total minus
available, not a sum of application allocations. PSS divides shared pages among
processes; RSS includes shared pages in full. Missing readings say Unavailable,
not zero. Partial/malformed results and failures remain visible.

Opening the page and Refresh memory status read on a worker with a ten-second
helper timeout. Navigation remains available. Repeated requests coalesce into
one follow-up read; generation guards reject replies to closed/rebuilt views.
Refresh changes no memory or session settings. Warnings display thresholds from
the helper's report, without duplicating or changing its policy. They are
advisory: there is no automatic restart or memory tuning.

Session tools are separate from diagnostics. Portable Reload shell configuration
reloads dotfiles/styling, not the compositor or application memory. Pixel Restart
Sway opens a compact warning with Cancel focused first: Return cancels, Escape
dismisses, and only explicit Restart proceeds. A supervised restart can close
apps; older sessions may instead soft-reload. Both actions report requested
after spawning their existing helper, not verified completion.

The portable settings-memory-test.py fixture exercises delayed/failing/partial
reads, coalescing, invalid metrics, advisory thresholds, navigation, rebuild and
close, then reads the installed VM memory helper without changing policy.
Session actions remain inert. The layout/input fixture covers the distinct
portable reload and Pixel confirmation using pointer, touch and keyboard.

Small stylesheet reads, image-codec resource limits, broader file/provider
integration, accessibility scaling and physical GL performance remain in the
broader acceptance ledger.
