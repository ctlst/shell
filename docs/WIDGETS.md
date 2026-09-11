# Make a Home widget without recompiling

This guide is installed with the portable shell. Read it with `ctlst-widget
guide`, or open `/usr/share/ctlst-shell/docs/WIDGETS.md`. Give that path to any
coding agent; no particular agent, cloud account or development checkout is
required. It is documentation, not an automatically loaded agent skill.

Before coding, also read the complete protocol at
`/usr/share/ctlst-shell/docs/widget-protocol-v1.md` (in the source tree:
`agent/pi/skills/ctlst-app-builder/references/widget-protocol-v1.md`).

## Try the supplied example

For everyday editable notes, use the packaged [Notes widget](NOTES.md): it
previews a real text file and opens your selected external editor. Pocket Note
below remains a separate, deliberately display-only authoring example.

```sh
ctlst-widget register /usr/share/ctlst-shell/examples/pocket-note/pocket-note.yaml
ctlst-widget list
```

Pocket Note is a small Python helper and a YAML descriptor. It displays two
lines from `~/.config/ctlst/pocket-note.txt`, with a friendly default when that
file is absent. Edit that plain text file: visible content updates within a
second. It makes no network requests and stops polling while hidden. Theme
colors come from Home. There is no compiler or Python package installation.

The current Home host resolves palette tokens independently of their order in
the generated theme file, including light-theme text colors. Older builds had
an order-sensitive parser. Pocket Note retains its validated literal-color
fallback for those older hosts; new widgets can use the documented theme tokens.

The default card is five columns by one row. Free space through Home's widget
editor, or merge `glance` into `widgets.hidden` in `~/.config/ctlst/home.yaml`
to replace the placeholder At a Glance card. Preserve existing settings:

```yaml
widgets:
  hidden:
    - glance
```

Then run `ctlst-widget reload` from a terminal **inside the CTLST session**.
It restarts only that session's Home; apps and Sway stay alive. Widget-local
unsaved state may reset. New descriptors are discovered at Home startup,
not by `ctlst-session reload`. If there is no room, use Home's Add widget
picker after freeing a slot. The saved arrangement is in
`~/.config/ctlst/home-layout.json`; back it up before deliberate layout edits.

To undo, run `ctlst-widget remove pocket-note`, unhide `glance` if you hid it,
then `ctlst-widget reload`. Removal deletes the installed helper/descriptor;
it leaves your note text intact.

## Build your own

```sh
ctlst-widget scaffold tiny-counter --name "Tiny Counter" --output ./my-widgets
# Edit the generated Python helper and YAML descriptor.
ctlst-widget validate ./my-widgets/tiny-counter/tiny-counter.yaml
ctlst-widget register ./my-widgets/tiny-counter/tiny-counter.yaml
ctlst-widget reload
```

Scaffolding currently produces a pointer/touch counter, not a keyboard-complete
control. Prefer `interaction: none` for a display-only card. For interactive
widgets, test mouse and touch; keyboard messages are not part of protocol v1.
Provide a companion app for keyboard-heavy interaction rather than pretending
that keyboard support exists in the widget host.

Registration installs a helper in `~/.local/bin/ctlst-widget-ID` and a descriptor
in `~/.config/ctlst/widgets/ID.yaml`. The installed descriptor uses the helper's
absolute path, so a display manager's PATH does not matter. Existing files are
not replaced unless you explicitly pass `--force`. Back up before replacing.
If you already use a `widgets.enabled` allow-list, retain all desired IDs.
The current registrar creates an allow-list when absent: include your other
external widget IDs as well, or remove that list to allow all discovered ones.
Home's descriptor discovery still assumes the conventional ~/.config location;
non-default XDG widget directories are not yet a verified portable path.

## Instructions to hand to a coding agent

> Read /usr/share/ctlst-shell/docs/WIDGETS.md and the full widget-protocol-v1.md
> beside it. Create a small CTLST Home widget as a Python helper plus YAML,
> using ctlst-widget scaffold/validate/register. Preserve existing files and
> Home layout; do not use --force without checking what would be replaced.
> Do not rebuild Home, restart Sway, install a model provider, or modify system
> files. Ask before connecting accounts or using credentials. Verify it in
> Home and report input/lifecycle checks you could not perform.

## Essential contract and safety

- A helper exchanges newline-delimited JSON with Home over stdin/stdout.
  Home renders its text, rectangles, lines and circles; no GTK window needed.
- Handle hello/configure, visibility and shutdown/EOF. Send an initial frame
  promptly, flush stdout, and keep diagnostics on stderr.
- Bound input, text and frame sizes. At most 512 nodes and 256 KiB per frame.
  Never emit unchanged frames continuously; pause work when hidden.
- Use @ctlst_* theme tokens. Never read /dev/input, control Sway, inject input,
  or steal shell gestures from a widget.
- For pointer actions, commit only on release within the same target; cancel
  when a drag leaves it or Home cancels the sequence.
- Validation **executes the helper as you**. It is a protocol smoke check,
  not a security audit or sandbox. Review third-party code before validating
  or registering it. Helpers have your user's filesystem/network privileges.
- Test initial render, live changes, theme change, hide/show, resize/rotation,
  clean shutdown and idle CPU. Test applicable mouse/touch/keyboard routes.

The older Pi app-builder skill in the extraction tree is Pixel-development
guidance, not the portable installation workflow. This installed guide is the
entry point for widgets on the standalone shell.
