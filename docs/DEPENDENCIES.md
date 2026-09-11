# Dependencies and external applications

The dependency installer reads the package manifests in [dependencies/](../dependencies/).
These describe the default runtime environment, not a strict minimal-dependency
set. Some packages provide individual features rather than session startup.

| Role | Default software |
| --- | --- |
| Compositor and background | Sway, swaybg, swayidle |
| Status bar and notifications | Waybar, Mako |
| Terminal and session menu | Foot, Fuzzel |
| UI and rendering | GTK4, gtk4-layer-shell, Pango, Cairo, Mesa, libepoxy |
| Input and application discovery | libinput, GLib/GIO, desktop-file-utils, system icon themes |
| Python components | Python, GObject introspection and Cairo bindings |
| Network and audio controls | NetworkManager, PipeWire, WirePlumber, playerctl |
| Brightness, capture and clipboard | brightnessctl, grim, slurp, wl-clipboard |
| Desktop integration | D-Bus, portal backends and supporting command-line utilities |

Exact package names differ by distribution. Use the manifests for the complete
list. On Arch, the build profile combines `arch-runtime.txt` and `arch-build.txt`.
The corresponding Alpine manifests are provided, but current Alpine validation
is incomplete.

```sh
./scripts/install-dependencies runtime
# Or include compilation dependencies:
./scripts/install-dependencies build
```

Run one profile as appropriate. On Arch, this performs a full `pacman -Syu`
upgrade and asks for confirmation. Review the transaction first.
Test dependencies such as pytest and VM input tools are installed separately;
see [installation](../INSTALL.md) and [VM testing](CLEAN-ROOM-VM.md).

## Applications supplied by the user

Configure application roles in `~/.config/ctlst/applications.conf`:

```ini
[applications]
terminal = foot
launcher = ctlstdrawer
files = xdg
phone = none
messages = none
```

Phone and messaging apps are not required to start the shell. File handling uses
the system's configured external handler. Notes requires an installed text editor;
see [Notes configuration](NOTES.md).

A touch-only system needs a compatible on-screen keyboard. CTLST does not install
one, and generic keyboard integration has not completed validation. Device-specific
rotation, modem, camera, sound and power services must also be configured separately.
See [platform support](../BETA.md) before installing on a phone.
