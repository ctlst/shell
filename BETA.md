# Beta status — 2026-09-11

**Developer beta candidate, not a complete phone OS or a daily-driver promise.**
The physical reference is the existing Arch Linux ARM / Pixel 3a XL setup.
Generic Arch and postmarketOS VM results are useful portability evidence, not
support for every phone. Plain Alpine/OpenRC, Ubuntu Touch and Hyprland are not
validated targets. The session uses upstream Sway, not a compositor fork.

## What works, what depends on something else

| Component | Language | Current state |
| --- | --- | --- |
| Home/launcher | C / GTK4 | Real app grid, pages, edit/move/resize, rotation-aware layout and widgets. Recent later-page picking/grid repair is live on the Pixel. Direct keyboard editing/removal is incomplete. |
| App drawer | Python / GTK4 | Real installed-app discovery, search, launch, Hide/Show and Home placement. |
| Dock/charm | C / GTK4 | Real task switching and pointer wheel route; small task sets no longer duplicate entries. |
| Overview | C / GTK4 | Real workspace-group switch/close and asynchronous previews. Previews are cached screenshots, not live per-window textures. |
| Quick Settings | C / GTK4 | Real controls and dismissal. Brightness/device actions require system capabilities and permissions; unavailable providers must remain visibly unavailable. |
| Settings | Python / GTK4 | Real themes, accents, wallpaper preview/apply and preferences; OpenGL default with Cairo recovery override. |
| Action surface / gestures | C | Real touch routing and action rendering. Most shell routes have pointer/keyboard alternatives; full accessibility/input parity is not complete. |
| Session / workspace helpers | Python + shell | Separate login entry, upstream Sway startup, app/workspace coordination and scoped session cleanup. Never replaces ~/.config/sway. |
| Bar / notifications | Config + Python/shell integration | Upstream Waybar and Mako are dependencies, not CTLST-authored programs. |
| Themes / dotfiles | CSS, INI, YAML, JSON | Working layered configuration, theme/accent and user CSS overrides. Some edits require session reentry; not every compiled value is configurable. |
| Notes | Python widget helper | Real local plain text, visible refresh, responsive preview and external-editor launch. Current Pixel fallback is Foot + Neovim. No editor is bundled. |
| Widget authoring | Python tools + C host | No-recompile NDJSON widgets and working Pocket Note example. Helpers execute as the user; this is not a sandbox. |
| Clock / month widgets | C | Real time/month display. Opening an app requires an installed external clock/calendar handler. |
| At a Glance calendar | C display | **Placeholder: no event provider.** Displays “Calendar not connected”; it does not check your schedule. |
| Memory integration | Python/shell | Explicit per-app cgroup limits where supported. Generic defaults impose no CTLST caps. Portable freezing, pressure eviction and compositor memory restarts are **not implemented**. |
| Privacy/swipe surface | C / GTK4 | Works as a dismissible privacy cover. **Not authenticated, not a secure lock.** |
| Home agent panel | C frontend / optional adapter | Default-off prototype, not a supported assistant or security boundary. |

## Separate companion work — not a promise of this core beta

Phone/Messages (C), the SMS daemon (C), Files (Python), Weather (Python), and
Gamepad (C plus scripts) have real implementations and targeted tests in the
development repositories. Their standalone packaging/provider/device acceptance
is unfinished. Phone/SMS needs a working telephony stack; a UI demo is not proof
of calls or delivery. The Pixel keyboard is a separately attributed upstream
fork; neither its code nor a working generic keyboard adapter is bundled here.

Explicit test/demo modes are fixtures, not normal user data. Optional AI/game/
device experiments are not part of the first public core source snapshot.

## Known limits that must stay visible

- No authenticated lock. Do not rely on the privacy cover to protect an
  unattended device, notification history or cached workspace screenshots.
- Stock on-screen-keyboard integration and full touch-only fresh-phone setup
  are not accepted. Arrange a working keyboard provider and recovery access.
- Automatic rotation and some sound/power controls need separately installed
  device/provider integration. Rendering at another size is not sensor support.
- Calendar feed and clock/calendar application defaults remain user choices.
- AUR/distro recipes, package upgrade/removal, graphical display-manager
  selection and broader hardware acceptance are not certified by source tests.
- Changes verified on the Pixel and changes only verified in a VM are not
  interchangeable. No ROM, kernel, firmware or device image is included.

## Evidence

The development checkout's full source suite on 2026-09-11 passed **823 tests,
23 explicit skips and 65 subtests**. This includes source contracts and fixtures;
it is not 823 physical UI tests. Notes' actual editor launch was verified on the
Pixel; its native widget allocation/reflow passes in Arch ARM. Existing native
UI regression probes cover the recent Home, drawer, settings, shade, overview
and companion work, with their individual provider/input limits recorded.

The public snapshot is tested separately: see `release/VALIDATION.md` for its
exact results. Current preparation passes 100 source checks (two explicit skips),
a warning-clean native Arch ARM rebuild and all 20 installed-session checkpoints,
including wallpaper import, theming and reentry under a fresh VM user. This is
not a fresh OS-image or package-manager acceptance test.
Do not reuse historical green results as acceptance for a changed
release candidate. The older Alpine VM gate currently requires sudo credentials;
no successful current Alpine rerun is claimed.
