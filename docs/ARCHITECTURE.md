# Architecture and configuration contract

## Session composition

The display manager entry executes `ctlst-session start`. That launcher sets up
the Wayland session environment and starts upstream `sway` with CTLST's
package-owned base configuration. Sway then starts the core shell processes.
No nested compositor and no custom Sway binary are required.

```text
display manager
  -> ctlst-session start
     -> upstream sway --config <generated entrypoint>
        -> Home + drawer + dock + overview + shade + lock
        -> ctlst-gestured + ctlst-workspaced
        -> optional keyboard, phone, files, and lifecycle capabilities
```

The generated entrypoint establishes deterministic configuration includes.
Edit source dotfiles rather than generated files.

## Configuration locations

Users edit ordinary files under `${XDG_CONFIG_HOME:-$HOME/.config}/ctlst/`:

| File | Purpose |
| --- | --- |
| `session.conf` | compositor, renderer, app launch reservation timeout |
| `sway.conf` | user Sway directives, included last |
| `input.conf` | output-independent keyboard, pointer, and touch preferences |
| `gestures.conf` | thresholds and gesture behavior |
| `applications.conf` | terminal, launcher, phone/SMS/file defaults |
| `memory.conf` | explicit per-app and terminal cgroup byte limits |
| `theme.conf` | packaged theme name and accent color |
| `theme-style.css` | GTK style overrides layered after the selected theme; all present layers participate |
| `bar.jsonc` | native Waybar configuration; highest present file wins |
| `bar-style.css` | CSS overrides layered after the generated bar theme |
| `home.yaml` | native Home grid, paging and rotation layout settings |
| `preferences.json` | favorites, hidden drawer apps, wallpaper selection |

System administrators use `/etc/ctlst/`. Packaged defaults live below
`/usr/share/ctlst-shell/defaults/`; optional profiles install fragments below
`/usr/lib/ctlst-shell/profile.d/`. All components use the same resolver and
precedence:

1. compiled safety fallback;
2. packaged defaults;
3. installed profile fragments;
4. `/etc/ctlst` administrator values;
5. the user's XDG files;
6. documented one-shot environment overrides, where necessary for debugging.

## Inspectability

Use the session command to inspect and edit configuration:

- `ctlst-session config show memory` prints the user's `memory.conf` and reports
  when the file does not exist.
- `ctlst-session config paths` prints the complete load order.
- `ctlst-session config effective memory` prints resolved concrete values and
  the origin of each value.
- `ctlst-session config check` validates known INI keys/types and rejects
  unsupported enabled policies. Sway/Home files are checked for file type;
  their native parsers remain authoritative for contents.
- `ctlst-session config edit memory` opens the user's file in `$VISUAL` or
  `$EDITOR`.
- `ctlst-session reload` validates, regenerates includes, and asks the running
  session to reload.

`config edit` checks that the editor is executable before creating a dotfile;
existing contents are preserved. INI paths that are directories or dangling
symlinks are reported as errors, not silently treated as missing overrides.

For example, this user file applies a 64 MiB soft pressure threshold and a
128 MiB hard cap to new non-terminal launches (terminals have their own section):

```ini
# ~/.config/ctlst/memory.conf
[app]
# 64 MiB; crossing this asks the kernel to reclaim/throttle this scope.
memory_high_bytes = 67108864
# 128 MiB; a hard scope limit, not a whole-system memory target.
memory_max_bytes = 134217728
```

These values illustrate configuration syntax; choose limits appropriate to the
application workload. Zero is the default and adds no CTLST limit. Effective
output reports each resolved value and its origin.

## Profiles and capabilities

Profiles are declarative inputs. A generic profile discovers outputs and input
devices through Sway/libinput and treats phone-specific services as absent.
Device profiles may identify a preferred output, touch mapping, rotation
sensor, haptic device, and tested memory envelope.

Generic memory amounts are zero: CTLST imposes no per-app limit and does not
freeze, evict, or reparent unrelated applications. Nonzero app/terminal limits
apply on the next launch through `app-run` and require a working systemd user
manager. Pressure eviction, freeze policy, Flatpak management and compositor
restarts are not implemented in the portable runtime; enabling them is rejected.
The tested limits cover processes launched directly inside CTLST's scope, not
every application on the system. D-Bus activation, existing application
instances and sandbox-managed processes require separate lifecycle validation.
Zero adds no CTLST limit; it does not remove ancestor or system limits.

`ctlst-session reload` regenerates Sway/input/theme files under
`$XDG_RUNTIME_DIR/ctlst-shell/generated` and signals the live surfaces. Input
device selection, gesture thresholds and native Home layout require logging
out and back in. Application-role and memory settings are read per launch.
Imported wallpapers live in `$XDG_DATA_HOME/ctlst/wallpapers`; packaged assets
stay read-only under `/usr/share/ctlst-shell/wallpapers`.
The saved wallpaper is restored at login and after Sway reload; changing a
theme does not replace it. Settings uses GTK's built-in image chooser without
requiring CTLST Files. Its native controls follow the theme's light/dark mode.
The core bar is session-supervised and reloads its JSONC/CSS on
`ctlst-session reload`. Notifications consume generated Mako configuration;
new launches of the default Foot terminal role consume generated Foot colors.
Existing terminal windows retain their launch-time configuration.

## Custom GTK styling

### Palette accent

The default `theme.conf` selects Canopy and uses `accent = theme`: the chosen
palette supplies its own accent, including Paper's darker light-mode ink.
To choose a palette or pin a custom accent, edit an ordinary dotfile:

```ini
# ~/.config/ctlst/theme.conf
[theme]
name = paper
# Follow the palette. Replace with six RGB hex digits to pin your own color.
accent = theme
```

Run `ctlst-session reload` to apply. `ctlst-theme apply NAME` changes only the
name and preserves the accent policy and comments. Existing profile, system
and user hex overrides remain authoritative; no dotfiles are migrated or
overwritten. Setting `accent = theme` in your file also overrides an inherited
pinned accent. Arbitrary custom colors can reduce contrast; the shell does not
silently recolor a user's chosen accent.

`ctlst-session config effective theme` reports the resolved name/accent policy
and its source. The resulting RGBA color is `SHELL_ACCENT` in
`$XDG_RUNTIME_DIR/ctlst-shell/generated/theme.env`; inspect that generated
file, but edit the source dotfile. Switching back to `theme` clears any
previously generated accent override on the next reload.

### GTK overrides

Create `~/.config/ctlst/theme-style.css` (or use your `XDG_CONFIG_HOME`):

```css
/* A smaller drawer heading; no rebuild or generated-file edits. */
window#ctlstdrawer .drawer-title { font-size: 26px; }
```

Run `ctlst-session reload` to apply it. This file survives theme selection,
updates and session restarts. The packaged default contains comments only.
Its load order is the selected palette/component/variant CSS, followed by
packaged `theme-style.css`, sorted profile fragments, `/etc/ctlst/theme-style.css`,
then your file. Later rules win when specificity is equal; normal GTK CSS
specificity still applies. Relative `url(...)` assets resolve against their
source stylesheet, not the runtime output directory.

`ctlst-session config show|paths|edit theme-style` uses the ordinary config
commands. `effective theme-style` prints ordered CSS imports with origins;
these are cascade inputs, not computed widget values. `config check` checks
file type/readability/encoding, not GTK CSS grammar. GTK reports syntax problems
when loading the stylesheet. To recover, fix or rename your override and run
`ctlst-session reload` from a terminal; no reinstall is needed.

This styles GTK nodes in CTLST consumers of the generated theme. It does not
restyle unrelated apps, change Waybar (`bar-style.css`), or change geometry and
colors painted directly by custom GSK code. Those retain their own documented
configuration and theme tokens. User theme catalogs are not currently supported.
