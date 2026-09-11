# Contributing

Simple defaults. Your phone, your way.

Keep the core small, preserve user dotfiles and upstream Sway, and put device
integration and companion applications behind explicit boundaries. Read
AGENTS.md, docs/PHILOSOPHY.md, BETA.md and the relevant component documentation.

For bug reports include the source commit, distro/architecture, hardware,
compositor/GTK versions, input method, theme and a minimal reproduction. State
whether it also occurs with default configuration. Redact personal information
from logs and screenshots. Report vulnerabilities privately; see SECURITY.md.

Changes need focused tests and a warning-clean native build when C changes.
Run `make check`. Test in a disposable VM before a phone; include the exact
commands and distinguish real input/provider checks from fixtures. Do not
claim a passing source assertion as physical UI or hardware acceptance.

User-facing options need a documented default, ordinary editable file and
reload/restart behavior. Touch actions must have reasonable mouse/keyboard
routes and cancellation behavior. Use shared theme roles; do not add hidden
personal paths, copied binaries, credentials or upstream forks to core.

Contributions to original CTLST code use Apache-2.0 with existing notices
preserved. Identify any third-party origin instead of silently relicensing it.
