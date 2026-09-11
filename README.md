# CTLST Shell

**Simple defaults. Your phone, your way.**

CTLST Shell is a customizable GTK mobile shell for Linux. It provides a Home
launcher, app drawer, task switching, Quick Settings and an editable
configuration under `~/.config/ctlst/`. It runs as a separate session on
upstream Sway and preserves existing Sway configurations.

## Status

Developer beta. The reference device is the Pixel 3a XL running Arch Linux ARM.
Build and installed-session tests also run in an Arch ARM VM. Other devices
are experimental.

**The privacy screen does not authenticate users and is not a secure lock.**
Touch-only use requires a separately configured on-screen keyboard.
See [platform support and limitations](BETA.md) and [security](SECURITY.md).

## Features

- Home pages with movable apps and resizable widgets.
- Searchable app drawer, task dock and workspace overview.
- Quick Settings and Settings with OpenGL rendering.
- Themes, accent colors, wallpapers and user CSS.
- A separate display-manager entry that starts Sway.
- Python widgets that can be added without rebuilding Home.

Native surfaces and gesture routing are written in C. The drawer, Settings,
widget tools and session helpers use Python and shell scripts.
See the [component inventory](packages/CORE-CONTENTS.md).

## Build and install

```sh
git clone https://github.com/ctlst/shell.git
cd shell
```

Follow [INSTALL.md](INSTALL.md) to install dependencies, build and stage the
package. Source installation is currently intended for disposable VMs and
development systems; a supported distro package and upgrade/uninstall workflow
are not available yet.

Sway, GTK, Waybar, Mako, Foot and other [dependencies](docs/DEPENDENCIES.md)
are installed separately. Phone, messaging and file applications, keyboard
implementations, kernels and device services are outside this package.

## Documentation

- [Configuration and architecture](docs/ARCHITECTURE.md)
- [Home layout and widgets](ctlsthome/HOME-V2.md)
- [Touch, pointer and keyboard controls](docs/INPUT-PARITY.md)
- [Widget development](docs/WIDGETS.md)
- [Notes configuration](docs/NOTES.md)
- [Design principles](docs/PHILOSOPHY.md)
- [Contributing](CONTRIBUTING.md)
- [Validation results](release/VALIDATION.md)

## License

Copyright 2026 **Vladimir Kovalchuk** ([@radmadvlad](https://twitter.com/radmadvlad)).
CTLST code is licensed under [Apache-2.0](LICENSE).
See [NOTICE](NOTICE), [asset and dependency notices](THIRD_PARTY.md) and
[source provenance](PROVENANCE.md).
