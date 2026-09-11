# Beta candidate validation — 2026-09-11

This is a developer source candidate, **not a release-gate pass**. A clean
source archive and successful native compilation do not establish that a new
phone is ready for daily use.

## Passing evidence

- The final core code selection passed **94 source/fixture tests, two explicit
  skips** (historical font and omitted Pixel transform helper) in 14.07 seconds.
  Session config check passed. The final documentation update does not change
  the tested source selection.
- All seven core C components compiled with warnings as errors in Arch ARM.
  Staged files installed into a disposable existing VM with no developer CTLST
  dotfiles. Runtime library closure was present. `/etc/sway/config` was unchanged.
- A fresh VM user reached a visible installed session, status bar and wallpaper,
  with nine live endpoints. Virtual input passed two drawer launches, workspace
  keyboard navigation, charm wheel navigation, hotplug touch Home and bottom
  Home gesture exiting edit mode without changing the saved layout.
- The first functional run additionally passed provider-unavailable feedback,
  overview/shade keyboard routes, privacy keyboard dismissal, Settings launch,
  Settings index without optional helpers and Settings workspace retention.
- The new privacy-screen binary was separately built and exercised with real
  virtual touch in portrait and landscape: full-sheet movement, button-origin
  drag, short-swipe return, horizontal rejection, pointer and keyboard dismissal.
  Pixel and portable variants passed. This uses a software-rendered VM, not a
  phone GPU benchmark; live Pixel installation is separate from this snapshot.
- Working-directory secret scans found no detected secrets in the reviewed
  private extraction and both core exports (including the final code selection).
  The exporter excludes Git history,
  generated executables, fonts, firmware, device images and upstream forks.
  Scanning is not a guarantee of provenance or absence of all sensitive data.

## Unresolved acceptance

- The first complete functional run timed out at the wallpaper file chooser's
  Open button. The harness could invoke Choose files while Settings was busy.
  A readiness guard was added; a second fresh user run then timed out finding
  Sound details. The full gate has **not passed**. Neither failure is silently
  classified as proof of a product bug or proof that wallpaper/theming works.
- The broader Settings/wallpaper/theme/reentry portion needs a reliable current
  acceptance run. Earlier development evidence does not close that gate.
- The required legacy Alpine lane stops at remote sudo authentication. No
  current successful Alpine run is claimed.
- This was a fresh Unix user in an existing Arch ARM guest, not a new OS image,
  distro package transaction, upgrade/uninstall check or graphical GDM test.
- Stock keyboard integration, physical touch/GPU behavior across devices,
  sensor rotation, suspend and full accessibility are not certified here.

The companion applications and Pixel system integration are not included in
this core source snapshot. See BETA.md for functional and placeholder boundaries.
