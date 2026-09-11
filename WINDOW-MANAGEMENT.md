# Window movement feedback

The gesture daemon owns window movement and destination selection.
`scripts/window-drop-overlay` is an on-demand Python/GTK4 visual surface:
it is input-transparent, requests no keyboard focus and executes no move or
close action itself. Keyboard/compositor movement commands remain independent.

## Targets

The top row shows nearby app workspaces and **+** for a new workspace.
Side targets move to the previous/next workspace. **Close window** is labeled
separately at the bottom.

Targets retain at least 48 logical pixels. The daemon sends up to five nearby
workspaces, reduced when width requires it; a 320px output fits four plus New.
The focused workspace determines the range. Hit testing and rendering must use
the same order and geometry.

Changes to target geometry require coordinated updates to the gesture daemon
and overlay. Do not shift or reorder targets only in the renderer.

## Theme and rendering

The overlay uses retained GTK shapes, ellipsized cached Pango text and shared
semantic colors. Selected controls use selected-text; danger text is checked
for contrast. `T` or reopening reloads the palette. OpenGL is the default;
`GSK_RENDERER` provides a diagnostic override.

## Testing

Run `make check` and test destination ranges, long captions, target bounds,
pointer/keyboard pass-through and cancelled drags. Use an isolated session
with expendable windows before testing move/close actions on hardware.
See [gestures](GESTUREMAP.md) and [VM testing](docs/CLEAN-ROOM-VM.md).
