# CTLST action fan

C/GTK4 passive visual feedback for the gesture daemon's bottom-right action
fan. The daemon owns target selection, gesture release and the actual Gamepad,
Keyboard and Close window actions; this surface never executes them.

The modern UI branch uses rounded GTK snapshot nodes and cached Pango captions,
not a per-frame Cairo canvas. Each selected action has a readable name and
accessible description. Icons are CTLST-drawn primitives, not Apple assets.
The original three target centers are unchanged; the selected ring and caption
are feedback, not additional hit regions. The entire surface remains input
transparent and does not request keyboard focus.

Theme ACTION_* roles remain editable through the shared theme pipeline.
Missing roles inherit semantic shell colors; explicitly authored roles are
preserved. Reload sends R or opens the fan again. OpenGL is the default unless
GSK_RENDERER is explicitly set for diagnosis.

Build with make (GTK4/layer-shell; warnings as errors). The native
tests/action-render-test.c covers all targets and morph frames at 320–960px,
bounds and retained captions. The portable overlay VM probe checks light/dark
renders and keyboard/pointer pass-through without executing any action.
Physical touch/GPU and optional keyboard/gamepad providers remain separate QA.
