# Window movement feedback

The gesture daemon remains the authority for dragging/moving a window and
choosing a drop destination. The Python/GTK4 window-drop-overlay is on-demand,
input transparent, and never performs a move or close by itself. Existing
keyboard/compositor window-movement commands remain available independently.

The top row shows nearby workspaces and + for a new workspace; side targets
move to previous/next workspace. Close window is explicitly labeled at the
bottom. Selection feedback uses native retained GTK shapes and ellipsized,
cached Pango text. The full-screen surface never requests keyboard focus.
Missing or unusual workspace names cannot force its labels beyond the targets.

At least 48px is retained for each workspace target. The daemon sends up to five
nearby workspaces, fewer when width requires it (four at 320px), centered around
the focused workspace. The same range and geometry drive both hit testing and
the visual protocol. The panel stays within the viewport; the overlay must not
independently shift or reorder targets. Changing these constants requires
coordinated daemon/overlay changes and tests.

Themes use shared semantic colors; selected controls use selected-text and
danger text is contrast checked. User-supplied danger text is retained when
readable. T reloads the palette; closing and reopening also reloads. OpenGL is
the default with an explicit GSK_RENDERER override for diagnostics.
The portable copy resolves the generated theme directory; Pixel retains its
separate deployed theme path.

Verification: tests/drop-geometry-test.c exercises actual C range selection and
hit testing for 0–63 tasks, every focus and widths 320–960. Source policy tests
and the native Python snapshot probe cover captions/cache. The disposable VM
renders both themes and orientations and verifies pointer/keyboard pass-through.
This does not certify real drag-to-move/close on a physical touchscreen.
