# Quick Settings and notifications

Swipe up to close when content already fits or the viewport is at the bottom.
When more content remains below, the swipe scrolls instead; a fresh swipe at
the bottom closes. Brightness retains its own drag, and horizontal notification
swipes remain row actions. Close and Escape still dismiss without scrolling.
The focused native regression is `shade-primary-test.c --gestures`, driven by
the portable `vm/clean-room/shade-dismiss-ui.py` in an isolated ARM VM.

Secondary actions (screen timeout, Tailscale, Capture and Settings) now use
the same balanced native layout as the primary controls. Their complete labels
wrap instead of competing for four fixed columns; defaults use two columns on
narrow screens. `window#ctlstshade .shade-utilities > *` exposes the same
120px content minimum and 4px margins to ordinary CSS. Headings, rotation text
and network/provider names wrap without forcing the shade wider than the
output. Provider operations and gesture/brightness handling are unchanged.

The native fixture seeds long network names and real unavailable/timeout
labels. Its 320px root-width regression fails the earlier source with a 622px
minimum. The extended VM driver also checks keyboard scrolling to Settings
at every size, then pointer/touch/Return activation through inert callbacks.
This secondary-control follow-up is source/VM work, not a phone deployment.

Native resident GTK4 layer-shell surface. The four primary controls use
balanced one-, two- or four-column rows from their native natural requests,
including CSS and fonts. Defaults fit two columns at 320px and four at 640px;
larger text or custom controls can reduce the count. Brightness follows this
group, before rotation and secondary controls. Short screens scroll, and
reallocation retains the same widgets and focus.

Wi-Fi/Bluetooth/Sound main buttons toggle; separate 44px detail buttons open
their pages. Existing long-press details remain. Pointer/touch and Tab/Return
reach the same native buttons; brightness retains native arrow-key adjustment.
Content scrolling and the established close/Back routes are unchanged.

Ordinary theme CSS controls `window#ctlstshade .shade-primary > *` (120px
content minimum, 4px margins), `.shade-control-tile`, `.shade-control-detail`
and `.shade-slider`. Use `$XDG_CONFIG_HOME/ctlst/theme-style.css` (normally
`~/.config/ctlst/theme-style.css`) and `ctlst-session reload`. No new daemon, configuration format, dependency
or Pixel orientation assumption was introduced. Styling needs no rebuild.

`tests/shade-primary-test.c` hosts the actual UI with external callbacks
disconnected. Compile with `-O2 -Wall -Wextra -Werror`, GTK4,
gtk4-layer-shell and libm. Run `vm/clean-room/shade-primary-ui.py` with
`--disposable-vm --binary PATH --components themes/components.css
--touch-inject PATH`. It checks both palettes, normal/24px labels, 320–960px
bounds, default brightness placement and separate pointer/touch/keyboard
toggle/detail activation. It never toggles radios or writes a backlight.
Production session and physical GPU/hardware acceptance are separate checks.
