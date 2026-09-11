# Public developer beta checklist

Destination: https://github.com/ctlst/shell (public).
Publish only this clean-root core repository; never push the private extraction
or Pixel repository history here. No force-push is needed for the initial upload.

## Prepared

- [x] Core-only source and build/install payload; no companion app sources,
  upstream forks, prebuilt programs, device images or private history.
- [x] Apache-2.0 license, Vladimir Kovalchuk attribution, dependency/artwork
  notices, contribution instructions and security boundaries.
- [x] Component/language inventory, install instructions, dotfile/widget docs,
  feature/placeholder table and current validation record.
- [x] Only core runtime/build dependency profiles; no phone/emulator installer.
- [x] Original Sway configuration remains outside the install payload.
- [x] Source checks, staged-payload fixture checks and secret scanning performed.
  Exact results and limits are in VALIDATION.md; these are not hardware tests.

## Before presenting the candidate as install-validated

- [x] Resolve the Settings accessibility/chooser test failures; complete one
  bounded installed-session run through wallpaper, theme changes and reentry.
- [x] Record source revision, VM environment and passing/failed results.
- [x] Review the final staged file list and dependency closure for that revision.

These checks pass for the documented Arch ARM VM source install. They do not
establish a new phone installation or stable release. At publication, verify
the remote has no conflicting work, push only reviewed core commits without
force, then verify the GitHub tree and README. Do not attach generated phone
images or private development history.

## Explicitly outside this beta's support promise

Stock keyboard integration, secure authentication, calendar event providers,
complete accessibility, other phones, GDM GUI acceptance, distro package
upgrade/removal and AUR publication remain unfinished. The Alpine VM lane is
blocked on credentials; no current Alpine acceptance is claimed. No ROM or
companion applications are being released from this repository.
