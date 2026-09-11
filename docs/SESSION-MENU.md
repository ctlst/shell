# Session menu

The session menu is a Fuzzel client. The first page offers Exit menu, Lock
screen, More options and available power actions. More options contains Close
focused app, Reload configuration, Log out and Suspend when supported.

The Lock screen action opens CTLST's unauthenticated privacy cover; it does
not provide a secure session lock.

## Navigation and confirmation

Use pointer/touch or Up/Down/Return. Back returns to the first page.
Closing an app, logging out, suspending, restarting or powering off requires
a second selection. Cancel is first; Escape cancels confirmation.

Commands come from a fixed allowlist and are not evaluated as shell text.
Available actions depend on installed providers and system permissions.

## Appearance

Defaults use the generated palette, Noto Sans 14, a 28-character width,
five visible rows, 44px row height and 12px selection radius.
Long lists scroll.

For a complete Fuzzel override, copy the generated configuration to
`~/.config/ctlst/session-menu.ini`, respecting `XDG_CONFIG_HOME`:

```sh
cp "$XDG_RUNTIME_DIR/ctlst-shell/generated/fuzzel.ini" \
   "$HOME/.config/ctlst/session-menu.ini"
```

Then edit, for example:

```ini
[main]
font=Noto Sans:size=14
width=28
lines=5
line-height=44
```

Retain or customize the colors/key-bindings sections. The file is read whenever
the menu opens and is not overwritten by theme updates. Rename/remove it to
resume generated styling. This is a whole-file override, not a layered CTLST
INI scope.

## Diagnostics

`/usr/libexec/ctlst-shell/session-menu --list-actions` and
`--resolve "Action"` inspect action availability without executing it.
Test confirmation paths with inert providers and cancel before invoking
real power actions.
