# ctlstdock

The modern UI branch keeps the rotary model and click-through policy. The
dash-to-pill morph uses retained GTK snapshot/rounded-clip/border nodes instead
of a per-frame Cairo canvas. Static fallback icons still use Cairo and are
cached by GTK; this is not a wholesale removal of the Cairo dependency.
The default renderer is OpenGL unless GSK_RENDERER is explicitly set.

The active slot uses a quieter semantic accent-soft fill. Two-window tasks
retain their split arrangement; larger groups show one app identity and a
count rather than three overlapping tiny icons. Native buttons have readable
tooltips and accessible Home/app-count labels. The root and controls now fit
the intended 184x56 logical surface, including padding.

Build `tests/dock-render-test.c` against GTK4/layer-shell,
JSON-C, gio-unix and Cairo in a disposable Wayland session. It checks every
morph frame's bounds and native control minimums for one-to-six-window groups,
with 0/1/2/3/12/64 app workspaces and every selected position. It checks unique
destinations, exactly one selected control and measured pill/input bounds.
The portable chrome probe additionally exercises task routing and captures
both light/dark orientations; this does not prove physical GPU smoothness.

The portable repo's `vm/clean-room/dock-layout-ui.py` tests this follow-up
in a private staged Arch VM session. Supply `--disposable-vm --stage STAGE
--build BUILD --touch-inject HELPER`, where BUILD contains
`ctlstdock/ctlstdock` and `dock-render-test`. Add `--pixel` only for the
Pixel dock build: it maps its XDG theme path inside the private fixture and
does not claim portable collapsed-wheel behavior for that binary.
The driver checks the running hash, actual light/dark panel pixels, unique
0/1/2-task controls, pointer/touch/keyboard Home/app routing and unused-side
click-through. Portable additionally tests wheel routing; closing only its
own fixture apps tests contraction back to Home-only. This is staged UI
evidence, not Pixel raw-digitizer or whole-session hardware acceptance.

`ctlstdock` is a bounded, native GTK4 task dock for the ctlst Sway shell. It
uses `gtk4-layer-shell` without an exclusive zone and only creates a compact
bottom-center surface. Waybar remains responsible for the rest of the edge.

The daemon polls `$XDG_RUNTIME_DIR/ctlst-workspaces.sock` with:

```json
{"command":"list"}
```

and sends focus actions as:

```json
{"command":"home"}
{"command":"focus","workspace":"2"}
```

The dock shows each destination once when only Home or Home plus one app
workspace exists. Home stays on the left of the app; changing focus changes
the highlight, not the button positions. The capsule fits GTK's measured
controls, with a 96px minimum. This avoids the former Home–app–Home duplicate.
With two or more app workspaces, the selected destination occupies the middle
of the existing three-slot rotary carousel, with its previous and next
destinations on either side. Home participates in the same wrapping order.
The full task model supports up to 64 app workspaces within a 184px-wide pill.

The actual Wayland surface stays bounded at 184×56 with no exclusive zone.
The smaller painted pill is centered within it; its unused sides are
click-through, including when expanded. This is automatic small-set layout,
not a new theme/provider/setting. Scrub/wheel/keyboard sequencing and the
workspace manager's Home/focus commands are unchanged.

At rest the carousel is transparent and only a centered gesture dash is drawn.
`ctlst-gestured` sends scrub state over
`$XDG_RUNTIME_DIR/ctlst-dock.sock`: `S` starts a soft morph, `P 0` through
`P 1000` drives reveal from dash to full pill, `E` completes it, and `H`
schedules collapse. Hold inflate stays under the interactive threshold until
`E`, so short taps remain click-through.
Keyboard/workspace-manager Home and gesture Home both send `H`, even when
no active workspace-wheel gesture owns the dock. Returning Home therefore
ends transient task selection and restores the small charm using the existing
420ms collapse delay and retained animation. It does not resize Home or reserve
the expanded pill's height permanently; deliberate hover/scrubbing still reveals it.
The visible surface morphs from the dash into the task pill while its icons
fade in. The gesture daemon still owns task stepping and the dock still uses
the workspace manager for clicks, preserving the existing rotary semantics.

For pointer use, the collapsed dash owns only a centered 96x24 input target.
Wheel or trackpad scrolling over that charm steps through the same wrapping
workspace list, briefly reveals the selected task pill, and then collapses.
The remainder of the 184x56 layer surface stays click-through while collapsed.

Theme colors are read from the session's generated `theme.env` under
`CTLST_GENERATED_DIR` (normally `$XDG_RUNTIME_DIR/ctlst-shell/generated`),
using the same semantic `SHELL_*` tokens as the rest of the native shell.
Without that session variable, the fallback is `$XDG_CONFIG_HOME/ctlst/theme.env`
(normally `~/.config/ctlst/theme.env`). Theme application sends the existing
reload command; no theme files are rewritten by the dock itself.
The dock intentionally does not modify Sway, Waybar, systemd, or VM files.

Build on the target device with `make`. The host macOS checkout is not expected
to have the GTK4 and layer-shell development packages required by the Makefile.
