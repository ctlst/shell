import subprocess
import os
import shutil
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = ROOT / "dependencies"


def packages(name: str) -> list[str]:
    return [
        line.strip()
        for line in (DEPENDENCIES / name).read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]

def test_dependency_manifests_are_sorted_unique_and_profiles_do_not_overlap():
    names = (
        "alpine-runtime.txt",
        "alpine-build.txt",
    )
    profiles = {name: packages(name) for name in names}

    for name, declared in profiles.items():
        assert declared == sorted(set(declared)), name

    seen: set[str] = set()
    for name in names:
        overlap = seen.intersection(profiles[name])
        assert not overlap, f"{name} duplicates {sorted(overlap)}"
        seen.update(profiles[name])


def test_runtime_and_build_manifests_cover_native_shell_contract():
    runtime = set(packages("alpine-runtime.txt"))
    build = set(packages("alpine-build.txt"))

    assert {
        "gtk4-layer-shell",
        "gtk4.0",
        "json-c",
        "libepoxy",
        "libinput",
        "sqlite-libs",
        "sway",
        "waybar",
        "python3",
    } <= runtime
    assert {
        "build-base",
        "gtk4-layer-shell-dev",
        "gtk4.0-dev",
        "json-c-dev",
        "libepoxy-dev",
        "libinput-dev",
        "sqlite-dev",
    } <= build


def test_arch_core_dependencies_are_separate_and_complete():
    runtime = packages("arch-runtime.txt")
    build = packages("arch-build.txt")
    for declared in (runtime, build):
        assert declared == sorted(set(declared))
    assert not set(runtime) & set(build)
    assert {"sway", "waybar", "gtk4-layer-shell", "python-gobject", "python-cairo",
            "at-spi2-core", "ttf-droid", "json-c", "libinput", "sqlite"} <= set(runtime)
    assert {"base-devel", "pkgconf", "wayland-protocols"} <= set(build)
    source = (ROOT / "scripts/install-dependencies").read_text()
    assert "pacman -Syu --needed" in source
    assert "arch-runtime.txt arch-build.txt" in source
    assert "unsupported Arch dependency profile" in source


@pytest.mark.parametrize("manager,profile,prefix,manifests", [
    ("pacman", "runtime", ["-Syu", "--needed"], ["arch-runtime.txt"]),
    ("pacman", "build", ["-Syu", "--needed"], ["arch-runtime.txt", "arch-build.txt"]),
    ("apk", "build", ["add"], ["alpine-runtime.txt", "alpine-build.txt"]),
])
def test_installer_invokes_matching_manager(tmp_path, manager, profile, prefix, manifests):
    for utility in ("dirname", "sed"):
        (tmp_path / utility).symlink_to(shutil.which(utility))
    for name, body in (("id", "printf '0\\n'"),
                       (manager, "printf '%s\\n' \"$@\"")):
        executable = tmp_path / name
        executable.write_text("#!/bin/sh\n" + body + "\n")
        executable.chmod(0o755)
    env = os.environ | {"PATH": str(tmp_path)}
    result = subprocess.run([str(ROOT / "scripts/install-dependencies"), profile],
                            env=env, capture_output=True, text=True, check=True)
    assert result.stdout.splitlines() == prefix + [p for m in manifests for p in packages(m)]
    for excluded in ("phone", "all", "motion-lab"):
        rejected = subprocess.run([str(ROOT / "scripts/install-dependencies"), excluded],
                                  env=env, capture_output=True, text=True)
        assert rejected.returncode == 2
        assert not rejected.stdout


def test_dependency_installer_consumes_the_manifests():
    installer = ROOT / "scripts" / "install-dependencies"
    source = installer.read_text(encoding="utf-8")

    subprocess.run(["sh", "-n", str(installer)], check=True)
    for name in (
        "alpine-runtime.txt",
        "alpine-build.txt",
    ):
        assert name in source


def test_vm_guide_documents_isolation_and_coverage():
    contract = (ROOT / "docs" / "CLEAN-ROOM-VM.md").read_text(encoding="utf-8")

    assert "disposable Arch ARM VM" in contract
    assert "fresh Unix user" in contract
    assert "baseline" in contract
    assert "--disposable-vm" in contract
    assert "does not validate a fresh OS image or package upgrade/removal" in contract

def test_vendored_status_font_records_provenance_and_license():
    if not (ROOT / "fonts").exists():
        import pytest
        pytest.skip("Historical font excluded from the original-only core source snapshot")
    font_readme = (ROOT / "fonts" / "README.md").read_text(encoding="utf-8")

    assert (ROOT / "fonts" / "ctlst-status-icons.ttf").is_file()
    assert (ROOT / "fonts" / "LICENSE-MDI").is_file()
    assert "MaterialDesign-Webfont v7.4.47" in font_readme
    assert "SHA-256" in font_readme
