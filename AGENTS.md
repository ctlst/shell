# Contributor and coding-agent instructions

Read [README.md](README.md), [BETA.md](BETA.md), [SECURITY.md](SECURITY.md)
and [design principles](docs/PHILOSOPHY.md) before making changes.

## Package boundaries

- This repository contains the CTLST core shell, not a device distribution.
- Keep upstream applications, keyboard forks, device services and companion
  applications outside the core install target.
- Preserve upstream Sway and the user's existing configuration.
- Do not commit credentials, private dotfiles, device images or build outputs.
- Phone deployment requires the target device's documented deployment procedure;
  this repository does not supply one.

## Implementation and validation

- Read the relevant component documentation before editing it.
- Document configuration defaults, units, precedence and reload behavior.
- Preserve user configuration through installation and updates.
- Use shared theme roles and provide cancellation for touch interactions.
- Maintain pointer and keyboard alternatives; document incomplete routes.
- Build changed C with `-Wall -Wextra -Werror` and run `make check`.
- Validate affected interactions in a disposable VM. Report source, simulated
  input and hardware results separately.
- Keep unsupported features and the unauthenticated privacy screen explicit.

Widget development uses [WIDGETS.md](docs/WIDGETS.md) and the
[protocol reference](agent/pi/skills/ctlst-app-builder/references/widget-protocol-v1.md).

## Documentation

Write for developers installing, configuring, debugging or contributing to
this repository. Describe current behavior and reproducible steps. Keep design
requirements separate from implemented features. Avoid conversation summaries,
personal development plans, deployment diaries and references to unavailable
files or unpublished test tools.
