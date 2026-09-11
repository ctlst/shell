"""Exercise the real install recipe; fixtures replace only native compilation."""
from pathlib import Path
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
        "ctlst-session", "ctlst-settings", "ctlst-widget", "ctlst-home-notes"}
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
    assert {"settings", "notes-widget"} <= core
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
