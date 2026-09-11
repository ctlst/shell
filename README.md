# CTLST Shell

**Simple defaults. Your phone, your way.**

A small, customizable GTK mobile shell for Linux: a native Home launcher,
app drawer, task switching, Quick Settings and ordinary editable dotfiles.
Built on upstream Sway—not a Sway fork, phone ROM or replacement for your
existing Sway configuration.

**Developer beta candidate.** The physical reference is our existing
**Arch Linux ARM / Pixel 3a XL** setup. Other phones are experimental.
The privacy screen is **not a secure lock**, and a working on-screen keyboard
must be supplied separately. Read [beta status](BETA.md) and
[security boundaries](SECURITY.md) before installing.

## What you get

- Home pages, movable/resizable widgets and installed-app launching.
- Searchable app drawer, dock/charm and workspace overview.
- GTK Quick Settings and Settings, with accelerated rendering and a recovery
  renderer override.
- Basic themes, accents, wallpaper selection and user CSS.
- A separate CTLST display-manager entry that starts upstream Sway.
- Dotfiles under `~/.config/ctlst/` and no-compile Python widgets.

Home, dock, overview, shade and gesture routing are C. Drawer, Settings,
widgets and session helpers use Python/shell. GTK, Sway, Waybar, Mako, Foot,
fonts and other upstream software remain separately installed dependencies.

The first public source snapshot is core-only. Phone/SMS, Files, Weather,
Gamepad, keyboard forks and device integration are separate development lanes,
not silently bundled applications. See [what core contains](packages/CORE-CONTENTS.md).

## Try it and make it yours

Start with a disposable VM or a backed-up, already Sway-capable device:

- [Build, stage and start the shell](INSTALL.md)
- [What works, what is a placeholder, and what remains](BETA.md)
- [Configuration and architecture](docs/ARCHITECTURE.md)
- [Touch, pointer and keyboard routes](docs/INPUT-PARITY.md)
- [Create a Home widget without recompiling](docs/WIDGETS.md)
- [Notes file/editor configuration](docs/NOTES.md)
- [Design philosophy](docs/PHILOSOPHY.md)
- [Contributing](CONTRIBUTING.md)

There is no certified AUR release, distro upgrade/uninstall path, universal
phone support or complete calendar integration yet. Passing VM/source tests
does not make those features complete. Exact candidate checks belong in
`release/VALIDATION.md`; historical development notes are not release acceptance.

## Attribution

Created by **Vladimir “Vlad” Kovalchuk**
([@radmadvlad](https://twitter.com/radmadvlad)). Original CTLST work is
[Apache-2.0](LICENSE), with attribution in [NOTICE](NOTICE) and dependency/
artwork boundaries in [THIRD_PARTY.md](THIRD_PARTY.md).

CTLST Shell is still a provisional name. The project is a customizable Linux
phone foundation, not a mandatory rice or an attempt to bundle every app.
