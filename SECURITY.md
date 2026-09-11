# Security boundaries

CTLST Shell is an experimental desktop session running as your Linux user.

- **The swipe/privacy screen is not a secure lock.** It does not authenticate
  you or provide a session-lock security boundary. Keep a separate trusted
  lock/recovery strategy; do not leave sensitive sessions unattended.
- Widgets, application commands and installed providers run with your user
  permissions. They are not sandboxed. Review third-party widgets and scripts
  before running them, including the widget validator (which executes helpers).
- Notes, notification history and task preview caches can contain private data.
  Do not post your dotfiles, caches, screenshots or logs without reviewing them.
- Memory limits are resource policy, not a security sandbox.
- Do not install keyboard/input privileges, polkit rules or device services
  copied from another phone without reviewing them for your system.

For a suspected vulnerability, use GitHub's private vulnerability reporting if
enabled. Otherwise contact the maintainer privately to establish a reporting
channel before sending sensitive details. Do not post tokens, personal messages
or an exploitable private-device configuration in a public issue.

There is no promised security-support window for this beta. Read BETA.md before
using it on a device containing sensitive accounts or data.
