# CTLST Shell engineering rules

Read README.md, BETA.md, SECURITY.md and docs/PHILOSOPHY.md first. This is the
core source snapshot, not the device ROM or the private extraction checkout.

- Keep upstream Sway and the user's existing session/configuration intact.
- Preserve user dotfiles. Use packaged defaults, profiles, /etc/ctlst, then
  XDG_CONFIG_HOME/ctlst overrides, with documented reload/reentry behavior.
- Keep upstream applications, keyboard forks, device code and optional
  companions out of the core install and source export.
- Build changed C with -Wall -Wextra -Werror, run make check, and verify in a
  disposable VM. Do not claim source contracts as hardware/input acceptance.
- Touch work needs pointer/keyboard alternatives and cancellation behavior.
- Use shared theme roles. Widget helpers use the documented NDJSON protocol;
  see docs/WIDGETS.md and agent/pi/skills/ctlst-app-builder/references/.
- Never ship binaries, private dotfiles, images of devices, keys or credentials.
- Never deploy directly to a phone from this snapshot. The Pixel development
  repository owns its separate guarded device promotion process.
- Keep incomplete features and the unauthenticated privacy screen explicit.
