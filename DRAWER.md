# App drawer

The resident Python/GTK4 drawer discovers desktop entries and launches
applications through the shared `app-run` helper. It provides searchable
browsing and Home/drawer visibility controls.

## Browsing

Open with `Super+D`, Apps on Home or the Home content pull gesture.
Search filters names and desktop metadata; hidden entries are excluded.
Enter in search launches the first result of the current text, including
changes not yet processed by GTK's search debounce. No results means no launch.
Focused grid items and native buttons retain their own activation.

Escape or Close dismisses. Long press provides Home placement.
The grid supports 2–8 columns; packaged styling fits three columns at 320 logical
pixels with 44px icons and two-line names.

## Editing

Edit displays a single-column list with Add/Remove from Home and Hide/Show
actions. Touch, pointer and Tab/Enter use native controls. Row taps and Enter
in search do not launch applications while editing.

Hidden apps remain searchable in Edit and can be restored with Show.
Hide affects drawer visibility, not installation or an existing Home icon.
Add/Remove changes Home membership, not application files. Done restores browsing.

Preferences are stored in `~/.config/ctlst/preferences.json`.
Internal helper commands are:

```sh
/usr/libexec/ctlst-shell/ctlst-preferences favorites list
/usr/libexec/ctlst-shell/ctlst-preferences drawer-hidden list
```

The same subcommands accept `add` and `remove` with an application ID.

## Styling

Edit `~/.config/ctlst/theme-style.css` and run `ctlst-session reload`.
Packaged variant CSS follows shared component CSS; user layers follow both.
See [configuration precedence](docs/ARCHITECTURE.md#custom-gtk-styling).

Grid rules use `.app-tile:not(.drawer-edit-row)`. Edit rows retain at least
44px touch targets and separate icon/margin rules. The Home badge area aligns
names across pinned and unpinned entries. The moving drawer panel is opaque;
the unrevealed region of its fullscreen layer is not.

## Development

Run `make check` and the [installed-session test](docs/CLEAN-ROOM-VM.md).
Validate immediate search/Enter, empty results, reversible Hide/Show and Home
placement, edit-mode launch suppression, scrolling and narrow layouts.

Large-catalog performance, helper failure/stall feedback, physical keyboard
providers and broader assistive navigation need additional validation.
Third-party app icons may have limited contrast in some themes.
