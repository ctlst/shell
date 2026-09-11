# Source snapshot provenance

CTLST Shell originated in Vladimir Kovalchuk's Pixel 3a XL development project.
The private standalone extraction retained experimental history; the first
public core snapshot deliberately starts a new Git root without that history.
This preserves the private development record without publishing historical
binaries, device utilities, keyboard forks or personal screenshots.

release/source-files.json records the SHA-256 of each exported source input.
The exporter is an allowlist, not a copy of the whole developer directory.
Only the source snapshot's own root and later reviewed commits may be pushed.

Original CTLST work is Apache-2.0, copyright 2026 Vladimir Kovalchuk. Upstream
dependencies remain separately installed and attributed; see THIRD_PARTY.md.
The Pixel production deployment and this portable snapshot are separate lanes.
