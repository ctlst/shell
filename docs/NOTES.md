# Notes

Notes is a plain-text Home widget, not a bundled text editor. Its stock helper
is `ctlst-home-notes`, and tapping it launches `dev.ctlst.Notes.desktop` through
the normal shell app launcher. Existing Notes placement and dimensions survive
this update. No sample text is written over your notes.

By default the preview reads `~/.config/ctlst/notes.txt`. The file is created
privately only when you first open it for editing. Changes saved by any editor
appear within about one second while the widget is visible. Hidden widgets
stop polling; returning Home refreshes them. Resizing/rotation reflows the
preview. Only the first 8 KiB is read for display; the editor receives the whole
file. Notes are local, unencrypted, and visible to anyone viewing your Home.

## Your dotfile

Create `~/.config/ctlst/notes.conf` (respecting `XDG_CONFIG_HOME` if set):

```ini
[notes]
file = notes.txt
editor = ["gnome-text-editor"]
```

Relative files resolve under your `ctlst` config directory. Absolute paths and
`~/` paths also work. `editor` is a JSON argument array, **not shell syntax**;
the absolute note filename is appended as one argument. A terminal alternative
is `["foot", "--app-id=dev.ctlst.Notes", "-e", "nvim"]`.

Use `editor = []` for automatic selection: installed graphical text/plain handler,
then a supported installed GUI editor, then Foot with nano/nvim/vi. On the
current Pixel, the available fallback is Foot + nvim. Terminal-only MIME handlers
use the explicit Foot fallback, because GIO does not discover Foot itself.
Install your preferred
editor separately; none is bundled. Missing editors produce an error, not a
fake editor window. Preview configuration errors are displayed on the card.

Config precedence is packaged defaults, sorted profile snippets, `/etc/ctlst`,
then your dotfile. Editing the file, config, or Python helper needs no Home
rebuild. Config changes are picked up on the next visible refresh or editor
launch. The portable session config tool also accepts the `notes` scope.

## Input and installation

Touch/pointer taps activate on release through Home's normal launch-mode path.
Dragging, resizing and edit mode do not launch the editor. Typing happens in
the chosen editor, not inside the preview. Keyboard-only users can launch Notes
from the searchable app drawer or run `ctlst-home-notes --edit`; editor save
and exit shortcuts are those of their selected editor.

The package installs `/usr/bin/ctlst-home-notes`, the Notes desktop entry and
`/usr/share/ctlst/widgets/notes.yaml` together. A user descriptor with the same
ID overrides the stock one: if you copied an old demo descriptor, update its
`desktop` to `dev.ctlst.Notes.desktop` and `interaction` to `launch` yourself.
Deployment never replaces a custom descriptor or Home layout.

Regression coverage includes save-to-exact-path, unchanged-frame suppression,
hidden/show behavior, missing/error states, bounded nonblocking reads, and
native GTK allocation-to-helper-to-frame reflow in the ARM Arch VM. This does
not substitute for physical finger/keyboard acceptance with your chosen editor.
