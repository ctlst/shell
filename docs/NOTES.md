# Notes

Notes is a plain-text Home widget. Its helper
is `ctlst-home-notes`, and tapping it launches `dev.ctlst.Notes.desktop` through
the shell app launcher. Editing is handled by a separately installed text editor.

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
then a supported installed GUI editor, then Foot with nano/nvim/vi. Terminal-only MIME handlers
use the explicit Foot fallback, because GIO does not discover Foot itself.
Install your preferred editor separately. Missing editors produce an error;
preview configuration errors are displayed on the card.

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
ID overrides the packaged one. A custom Notes descriptor should use
`desktop: dev.ctlst.Notes.desktop` and `interaction: launch` for editor activation.
Installation preserves custom descriptors and Home layouts.

Run `make check` for the included Notes tests: file handling, editor arguments,
visibility, error states and bounded reads. Test input and save/exit behavior
with the chosen editor and keyboard provider on the target device.
