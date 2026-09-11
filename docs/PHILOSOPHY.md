# Design principles

**Simple defaults. Your phone, your way.**

CTLST provides mobile navigation and configuration on top of upstream Sway.
The default session should be usable without customization, with documented
interfaces for users who want to change its appearance and behavior.

## Scope

The core package provides Home, app launching, task switching, notifications,
Quick Settings, input routing and session configuration. Applications and
hardware services integrate through explicit providers.

Phone/SMS applications, keyboards, games, AI services and device-specific
configuration are independent components. Core defaults must not assume
personal accounts, device nodes, installed companion apps or memory limits
from a particular device.

## Configuration

- Preserve user dotfiles during installation and updates.
- Keep the existing Sway session and configuration available.
- Document every option's type, units, default, precedence and reload behavior.
- Use explicit application roles and provider settings.
- Support theme, layout, binding and external-widget changes without a rebuild
  where the relevant interface supports it.
- Keep compiled constants as fallback values; expose supported customization
  through ordinary configuration files.
- Provide small, runnable examples and actionable error messages.

Theme adapters should translate supported data without executing theme-supplied
scripts or replacing unrelated application configuration.

## Interface design

Use shared semantic theme colors, readable labels and touch-sized controls.
Provide pointer and keyboard alternatives to touch actions, with documented
gaps until those routes are implemented. Drag interactions must support
cancellation; scrolling and resize must preserve focus and user data.

Unavailable providers must produce an unavailable state, not fabricated data.
Destructive actions require deliberate activation and an appropriate
confirmation or recovery path.

## Reviewing changes

A contribution should identify:

1. The user or developer workflow it improves.
2. Whether the implementation belongs in core or an external provider.
3. Its configuration, ownership and failure behavior.
4. Its touch, pointer and keyboard routes.
5. Its tests, platform assumptions and compatibility impact.

Current feature availability and outstanding work are listed in
[beta status](../BETA.md).
