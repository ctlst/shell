# Simple defaults. Your phone, your way.

A usable Linux phone foundation, built to be made your own.

CTLST's product is the foundation for customization, not one mandatory rice.
It should be pleasant and useful before someone edits a file, and remain
understandable after they change everything. A small core is a scope decision,
not a reason to leave basic phone interaction broken.

## What belongs in the base

- Reliable touch-sized navigation: Home, app launching, switching, status,
  notifications, Quick Settings, and clear recovery/dismissal routes.
- A restrained, readable default layout and a coherent starter theme.
- Documented, ordinary configuration files with useful comments and concrete
  values. A settings UI is a convenience, not the only way to configure things.
- Stable interfaces for themes, widgets, application roles, input, and profiles.
- Useful touch, pointer, and keyboard routes, plus accessibility and feedback
  when a requested action fails or a hardware capability is unavailable.

Core may depend on upstream programs and libraries. Small does not mean
rewriting them or pretending Linux needs no dependencies.

## What stays optional

Phone/SMS/file applications, third-party setup integrations, elaborate widgets,
AI agents, games, and device-specific tuning are independent choices. A usable
touch-only installation needs a working keyboard provider; that does not make
a particular keyboard fork CTLST's own code or a compulsory core bundle.

Omarchy or other theme importers belong in optional adapters. They should
translate supported theme data into CTLST's format, not execute theme-supplied
scripts or adopt another project's whole application/compositor configuration.
Importing a palette is not a claim of Hyprland or Omarchy session compatibility.

The Pixel profile can express Vlad's preferences. Generic defaults must not
silently inherit Pixel memory limits, hardware paths, installed apps, personal
accounts, or a particular external service.

## The customization promise

- Keep user-owned dotfiles authoritative; updates must not reset someone's rice.
- Preserve a user's existing Sway session and configuration.
- Let people choose apps by role and disable optional integrations.
- Aim for no rebuild for themes, layouts, bindings, gestures, and external
  widgets. New native features can still require compilation.
- State which edits reload live and which require a new session.
- Provide a small copy-and-edit example before building a plugin marketplace
  or a large compatibility framework.
- Defaults should be easy to understand, change, and recover.

This is the product standard, not a claim that all of it is already implemented.

## Where the current implementation stands

The generic defaults already leave phone/messages unassigned, select external
file handling, and impose no CTLST memory caps or pressure eviction. The bar
has basic clock/task/status controls. Existing layered dotfiles, packaged theme
selection/accent, wallpaper import, and external widget tools provide a start.

Remaining work includes first-class user theme directories and complete theme
authoring guidance, clearer user-authored layout/widget composition, remaining
Home/widget keyboard and pointer parity, stock keyboard integration, and an
authenticated lock. Some input/layout edits currently require logging in again.
The current privacy surface must not be advertised as a secure lock.

Do not remove the known-good Pixel setup to make a generic demo look minimal.
Keep its tuning in the device lane while improving the portable defaults.

## Feature review rule

Before adding a dependency, service, configuration option, or subsystem, ask:

1. Does it make basic Linux phone operation reliable or customization clearer?
2. Is it genuinely core, or better as a provider, example, profile, or companion?
3. Can users understand, override, and recover its configuration?
4. Are ownership, reload behavior, supported inputs, and failure states documented?
5. Does it preserve other sessions and avoid expanding device-specific assumptions?

A particular rice is an example of the shell, not the definition of the shell.

## Public positioning

**Motto:** Simple defaults. Your phone, your way.

**Short description:** A small, customizable GTK mobile shell for Linux, with
sensible defaults, editable dotfiles, and room to make it yours.

**Engineering principle:** Make customization the product; keep particular
customizations optional.

Keep the experimental status, tested hardware, and missing security/input
capabilities visible alongside this description. The motto does not settle
the product's final name or expand its supported-platform claims.
