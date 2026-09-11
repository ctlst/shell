# Release procedure

Use this checklist when preparing a source release. Record the tested commit,
environment, commands, results and limitations in [VALIDATION.md](VALIDATION.md).

## Source and documentation

- Verify the component map, dependency manifests and install target agree.
- Review source and artifacts for credentials, private data and generated files.
- Preserve license and attribution notices; document third-party additions.
- Update installation, configuration, input and platform-support documentation.
- Check repository links and runnable examples.
- List unsupported features and security limitations in [BETA.md](../BETA.md).

## Build and tests

1. Run `make check` and syntax checks for modified scripts.
2. Build native components with `-Wall -Wextra -Werror` on the target architecture.
3. Stage `make install` in an empty directory and review the file list.
4. Check runtime library resolution and compare installed binaries with the build.
5. Run the [installed-session tests](../docs/CLEAN-ROOM-VM.md) under a fresh user.
6. Exercise affected touch, pointer, keyboard, theme, resize and restart behavior.
7. Report simulated input, physical hardware and package lifecycle results separately.

## Publication

Review the outgoing commits and remote state, push without overwriting unrelated
work, then verify the published revision, README and artifact contents.
Source releases must exclude credentials, private configuration, firmware and
installation-specific device images.

A passing source-install test does not validate distro package upgrade/removal
or all devices. Keep release labels and support claims within the recorded
test coverage.
