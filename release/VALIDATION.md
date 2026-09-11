# Beta candidate validation — 2026-09-11

The core source suite, native Arch ARM build and installed-session functional
gate pass. This is a **developer beta**, not a complete phone OS, certified
distro package or promise that a new phone is ready for daily use.

## Passing evidence

- Final public-repository preparation: **100 passed, two explicit skips** in
  13.43 seconds; session config check passed. This includes staged core-payload
  fixtures, core-only dependency/profile boundaries, rejection of companion
  installer profiles, accessibility readiness regressions and the VM fixture's
  explicit opt-in guard. No C source changed in this preparation pass.
- The final core code selection passed **94 source/fixture tests, two explicit
  skips** (historical font and omitted Pixel transform helper) in 14.07 seconds.
  Session config check passed. The final documentation update does not change
  the tested source selection.
- All seven core C components compiled with warnings as errors in Arch ARM.
  Staged files installed into a disposable existing VM with no developer CTLST
  dotfiles. Runtime library closure was present. `/etc/sway/config` was unchanged.
- A final native rebuild of this prepared source staged 118 files, all beneath
  `/usr`, with no `/etc`, home tree or bundled fonts. All seven rebuilt C binaries
  were byte-identical to the installed binaries used by the passing functional
  gate, with no unresolved runtime libraries. Native/runtime source is unchanged
  from root commit `1c8419e`; subsequent preparation changes affect the dependency
  installer, tests, component map and documentation, not installed UI code.
- A fresh VM user reached a visible installed session, status bar and wallpaper,
  with nine live endpoints. Virtual input passed two drawer launches, workspace
  keyboard navigation, charm wheel navigation, hotplug touch Home and bottom
  Home gesture exiting edit mode without changing the saved layout.
- The first functional run additionally passed provider-unavailable feedback,
  overview/shade keyboard routes, privacy keyboard dismissal, Settings launch,
  Settings index without optional helpers and Settings workspace retention.
- The final complete functional run passed **20 checkpoints** under a fresh
  `ctlstbeta4` VM user. Alongside navigation/provider checks it visibly imported
  wallpaper through GTK's file chooser, changed Settings' theme without resetting
  wallpaper, reloaded Sway/input/theme dotfiles, verified actual kernel cgroup
  limits, and retained wallpaper/theme/bar/user settings across logout/reentry.
  Ordinary Sway config was preserved. Temporary fixture privileges were disabled
  after testing; evidence remains in the disposable guest's test-user home.
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

## Test harness corrections

Earlier runs failed at Sound details or the wallpaper chooser. A focused native
probe showed GTK4 emits SENSITIVE/SHOWING but omits ENABLED for usable controls.
The test now checks the supported states. Separately, the default wallpaper's
already-selected ID let the test race ahead of queued Apply work; the harness
now awaits the visible saved message and actual chooser open/close. The final
full run passes with these fixes. No application behavior was changed to make
these tests pass.

## Environment and remaining acceptance

Arch Linux ARM / AArch64; Sway 1:1.12-4, GTK4 1:4.22.4-1,
gtk4-layer-shell 1.3.0-1, Python 3.14.7-1 and Waybar 0.15.0-3.
Headless Sway uses Pixman/Cairo and real virtual input; no phone GPU performance
claim follows. The unchanged `/etc/sway/config` SHA-256 is
`ef7cd1591d8cf8db7ced418abdc1d156d72a3b2ea3f15a148183c818e751d3c9`.

- The required legacy Alpine lane stops at remote sudo authentication. No
  current successful Alpine run is claimed.
- This was a fresh Unix user in an existing Arch ARM guest, not a new OS image,
  distro package transaction, upgrade/uninstall check or graphical GDM test.
- Stock keyboard integration, physical touch/GPU behavior across devices,
  sensor rotation, suspend and full accessibility are not certified here.

The companion applications and Pixel system integration are not included in
this core source snapshot. See BETA.md for functional and placeholder boundaries.
