# Source provenance

CTLST Shell was originally developed for an Arch Linux ARM installation on the
Pixel 3a XL. This repository contains the standalone shell source; device
integration, firmware and keyboard forks are maintained separately.

The source history begins at commit `1c8419e`.
`release/source-files.json` records SHA-256 hashes of the files imported in
that commit. It is an import record, not a checksum manifest for later versions.
Use Git commits and release references to identify subsequent changes.

The responsive Home/tray, calendar refresh, widget runtime and Shade theme
updates were integrated from Pixel source commit
`87604ae78184302d64aaa404a31ca3c4ad82c1a5`, preserving this repository's portable
paths, application roles and keyboard navigation. Device-only window policy,
telephony, account configuration and generated binaries are not part of that
core integration.

CTLST code is Apache-2.0, copyright 2026 Vladimir Kovalchuk.
[THIRD_PARTY.md](THIRD_PARTY.md) documents external dependencies and artwork.
