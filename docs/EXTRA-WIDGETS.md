# Optional calendar, Photos and Now Playing providers

These providers are outside the core install and are installed explicitly:

```sh
make install-extra-widgets DESTDIR=/path/to/staging PREFIX=/usr
```

This installs the Python helpers, Photos desktop entry and Photos/Now Playing
descriptors. It does not install external applications, configure accounts or
modify the current Home layout. Add and resize widgets through Home editing.

Calendar event-day markers use Evolution Data Server through Python GI
(`EDataServer` and `ECal` introspection). Install a compatible calendar and
account provider separately. The helper reads selected calendars; complete
Google sign-in and consent in the provider's account UI. The month grid works
without the helper, EDS or an account. Account/network synchronization remains
the responsibility of the calendar provider.

Photos uses Python GI, GTK4/Adwaita and GdkPixbuf for the gallery and thumbnails.
It reads `~/Pictures`, or `CTLST_PHOTOS_DIR` when set before starting Home.
Scanning is bounded to the root and one subdirectory level, excludes symlinks
and oversized files, and retains up to 256 cached thumbnails under
`$XDG_CACHE_HOME/ctlst/widget-images`. Full-size photos open through `xdg-open`;
install and select an image viewer separately. Photos and Now Playing adapt
information density to their size; originals are never modified.

Now Playing uses `playerctl` and a running MPRIS-compatible media player. Both
helpers stop polling when hidden. Media controls activate on release and
cancel when a gesture becomes a swipe. Protocol v1 has no keyboard-event
channel; direct keyboard media-widget controls remain unsupported.

Runtime image nodes require absolute local thumbnail paths, with bounded file
size/dimensions. Helpers run with the user's permissions and are not sandboxed.
See [the widget protocol](../agent/pi/skills/ctlst-app-builder/references/widget-protocol-v1.md).
