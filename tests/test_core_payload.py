"""Exercise the real install recipe; fixtures replace only native compilation."""
from pathlib import Path
import json
import os
import subprocess
import tomllib

import pytest


ROOT = Path(__file__).resolve().parents[1]
NATIVE = {"ctlst-gestured", "ctlstaction", "ctlstdock", "ctlsthome",
          "ctlstlock", "ctlstoverview", "ctlstshade"}


@pytest.fixture
def staged(tmp_path):
    binaries = tmp_path / "native"
    binaries.mkdir()
    for name in NATIVE:
        (binaries / name).write_text("#!/bin/sh\nexit 0\n")
    stage = tmp_path / "stage"
    subprocess.run(
        ["make", "install", f"DESTDIR={stage}",
         "CORE_BINARIES=" + " ".join(str(binaries / name) for name in sorted(NATIVE))],
        cwd=ROOT, check=True, capture_output=True,
    )
    return stage


def test_installed_executables_are_only_owned_core(staged):
    assert {p.name for p in (staged / "usr/bin").iterdir()} == NATIVE | {
        "ctlst-session", "ctlst-settings", "ctlst-widget", "ctlst-home-notes", "ctlst-battery"}
    assert not (staged / "usr/share/fonts").exists()
    forbidden = {"system", "patches", "firmware", "modules", "wvkbd", "lisgd",
                 "ctlstdialer", "ctlstmessages", "ctlst-smsd", "ctlstfiles", "ctlstpad"}
    for path in staged.rglob("*"):
        assert not forbidden.intersection(path.relative_to(staged).parts), path
        assert path.suffix not in {".ttf", ".otf", ".woff", ".woff2", ".so", ".patch"}, path
        assert ".so." not in path.name, path


def test_core_preserves_its_license_and_explains_retained_history(staged):
    notices = staged / "usr/share/licenses/ctlst-shell"
    assert {p.name for p in notices.iterdir()} == {
        "LICENSE", "NOTICE", "THIRD_PARTY.md", "CORE-CONTENTS.md"}
    assert "Vladimir Kovalchuk" in (notices / "NOTICE").read_text()


def test_battery_is_a_complete_portable_shell_component(staged, tmp_path):
    for relative in (
        "usr/bin/ctlst-battery",
        "usr/libexec/ctlst-shell/ctlst-battery",
        "usr/libexec/ctlst-shell/ctlst_battery.py",
        "usr/libexec/ctlst-shell/ctlst_battery_ui.py",
        "usr/share/applications/dev.ctlst.Battery.desktop",
        "usr/share/ctlst-shell/defaults/battery.conf",
        "usr/share/ctlst-shell/docs/BATTERY.md",
        "usr/lib/systemd/user/ctlst-battery.service",
    ):
        assert (staged / relative).is_file(), relative
    sway = (staged / "usr/share/ctlst-shell/sway/ctlst.conf").read_text()
    assert "exec $ctlst/ctlst-component $ctlst/ctlst-battery daemon" in sway
    config = tmp_path / "user-config/ctlst/battery.conf"
    config.parent.mkdir(parents=True)
    original = "[battery]\ninterval_seconds=90\nprocess_activity=false\n"
    config.write_text(original)
    state = tmp_path / "user-state"
    result = subprocess.run(
        [str(staged / "usr/bin/ctlst-battery"), "config-check"],
        env=os.environ | {
            "CTLST_LIBEXEC_DIR": str(staged / "usr/libexec/ctlst-shell"),
            "XDG_CONFIG_HOME": str(config.parents[1]),
            "XDG_STATE_HOME": str(state),
        },
        capture_output=True, text=True, check=True,
    )
    assert json.loads(result.stdout) == {
        "interval_seconds": 90, "retention_days": 30,
        "process_activity": False, "device": "auto",
    }
    assert config.read_text() == original
    assert not state.exists()  # Checking settings does not start recording.


def test_installed_theme_uses_external_font(staged):
    for css in (staged / "usr/share/ctlst-shell/themes").rglob("waybar.css*"):
        text = css.read_text()
        assert "Symbols Nerd Font" in text
        assert "CTLST Status Icons" not in text
    assert "Symbols Nerd Font" in (ROOT / "ctlstshade/ctlstshade.c").read_text()
    for distro, package in (("arch", "ttf-nerd-fonts-symbols"),
                            ("alpine", "font-nerd-fonts-symbols")):
        assert package in (ROOT / f"dependencies/{distro}-runtime.txt").read_text().splitlines()


def test_package_map_names_integration_not_upstream_ownership():
    manifest = tomllib.loads((ROOT / "packages/components.toml").read_text())
    assert set(manifest["package"]) == {"ctlst-shell"}
    core = set(manifest["package"]["ctlst-shell"]["components"])
    assert {"settings", "notes-widget", "battery-history"} <= core
    assert {"sway-session-config", "waybar-integration", "notification-integration"} <= core
    assert not {"sway", "waybar", "mako", "status-bar"} & core


def test_source_snapshot_keeps_only_core_dependency_profiles():
    assert {p.name for p in (ROOT / "dependencies").iterdir()} == {
        "arch-runtime.txt", "arch-build.txt", "alpine-runtime.txt", "alpine-build.txt"}
    for excluded in ("ctlstdialer", "ctlstmessages", "ctlst-smsd", "ctlstfiles",
                     "ctlstpad", "patches", "fonts", "system", "profiles"):
        assert not (ROOT / excluded).exists(), excluded


def test_functional_fixture_requires_explicit_disposable_vm_flag(tmp_path):
    result = subprocess.run(
        ["python3", str(ROOT / "vm/clean-room/functional.py")],
        cwd=tmp_path, capture_output=True, text=True)
    assert result.returncode == 2
    assert "--disposable-vm" in result.stderr
    assert "ModuleNotFoundError" not in result.stderr
