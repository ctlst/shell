# App drawer

The resident Python/GTK4 drawer is a core shell surface, not a launcher bundled
with third-party apps. It discovers desktop entries, uses retained icon textures
and the existing touch scroller, and opens apps through the shared app-run
helper. The redesign does not replace Home's layout or alter workspace routing.

## Browse and edit

Normal mode keeps the 2–8-column icon grid. Search filters names and desktop
metadata; hidden entries are excluded. Enter in search launches its first
result, while focused grid items and native buttons keep their own activation.
Escape or Close dismisses. Long press retains the existing place-on-Home route.

Enter refreshes the filter from the current search text before choosing its
first result. GTK delays search-changed while typing; without this explicit
activation-time refresh, fast typing followed by Enter could launch an item
from the previous query. Empty current results consume Enter without launching.
Focused native controls and grid items retain their own activation.

Packaged browse styling fits three columns at 320 logical pixels, using 44px
icons and 12px two-line names. It clears inherited tile/icon margins so rows
remain compact without shrinking icons or touch targets. The scoped
`.app-tile:not(.drawer-edit-row)` rules live in theme CSS, not a new density
mode or compiled geometry policy. The reserved
Home badge lane keeps labels aligned across pinned and unpinned apps.

The portable session renders packaged `themes/NAME/variant.css` after shared
component CSS, then imports the layered `theme-style.css` overrides. Put user
rules in `~/.config/ctlst/theme-style.css` and run `ctlst-session reload`.
Theme changes preserve this file. See [the configuration contract](docs/ARCHITECTURE.md#custom-gtk-styling)
for precedence, relative assets and recovery; do not edit generated `theme.css`.

Edit switches to a single-column list: icon, name, Add/Remove from Home, and
Hide/Show in the drawer. Native actions work with touch, pointer and Tab/Enter.
Tapping the row or pressing Enter in search cannot launch an app during Edit.
Names ellipsize on narrow screens; action descriptions/tooltips retain the full
app name. The redundant miniature Home grid is gone; the heading reports Home
occupancy/capacity and the hidden count.

Hidden entries remain searchable in Edit and expose Show, so hiding is
reversible from the same surface. Leaving Edit filters them out again.
Hide does not uninstall the app or remove its Home icon. Add/Remove affects
the existing Home favorites list, not the installed application's files.

## Customization and boundaries

Existing preferences remain in `~/.config/ctlst/preferences.json`; the shell's
`ctlst-preferences favorites list|add|remove` and
`ctlst-preferences drawer-hidden list|add|remove` commands remain authoritative.
No new settings format or migration is introduced. Theme CSS provides ordinary
editable colors/styles; generated defaults make the moving drawer child opaque,
not the unrevealed area of its full-screen layer.

Edit rows explicitly clear inherited icon/row margins and retain at least
44px touch targets. Grid and edit-row styling remain separate, so exiting Edit
restores the ordinary icon presentation. The live theme loader and resident
control protocol are unchanged.

## Verification and remaining work

`tests/test_drawer_navigation.py` covers key policy, hidden filtering, edit
launch suppression and state contracts. The portable
`tests/drawer-layout-test.py` instantiates the real widgets without starting a
second resident socket service, checks narrow name/action bounds and catches
theme margins inflating row height. It needs the same gtk4-layer-shell preload
as the normal launcher.

`tests/drawer-search-test.py` checks a genuinely pending native search and
immediate Enter with an inert launch adapter. The whole staged-session journey
also types and immediately launches Phone through the real drawer/helper path.

The portable `vm/clean-room/drawer-ui.py` uses one owned desktop entry whose
only launch action creates a marker in its temporary directory. It tests
light/dark portrait/landscape layouts, reversible Add/Remove/Hide/Show with
pointer/touch, keyboard editing and normal launch. It backs up and restores
Home/preference state and removes its fixture. Nothing is installed on a phone.
The narrow portrait and short landscape cases also touch-scroll the edit list
and immediately return to the first row with vertical pointer scrolling,
checking that scrolling did not change either preference list.

The software-rendered VM is not a GPU benchmark. Physical keyboard-provider
behavior, large-catalog performance, helper failure/stall feedback, and broader
assistive navigation still need acceptance testing. Some upstream monochrome
icons have weak dark-theme contrast; do not recolor arbitrary app artwork to
claim that issue solved.
