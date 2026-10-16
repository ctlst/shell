"""Default start-zone contract and native direction/rotation regression."""
import configparser
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


def test_home_shades_overlap_at_center_by_default():
    config = configparser.ConfigParser()
    config.read(ROOT / "config/defaults/gestures.conf")
    assert config.getfloat("shade", "home_pull_start_fraction") == .60
    source = (ROOT / "ctlst-gestured/ctlst-gestured.c").read_text()
    assert "#define SHADE_HOME_PULL_START_DEPTH 0.60" in source
    assert "#define DRAWER_PULL_START_Y_MIN 0.50" in source


def test_native_home_shade_overlap(tmp_path):
    if not shutil.which("cc") or not shutil.which("pkg-config"):
        pytest.skip("native gesture development tools unavailable")
    flags = subprocess.run(["pkg-config", "--cflags", "--libs", "libinput", "json-c"],
                           capture_output=True, text=True)
    if flags.returncode:
        pytest.skip("native libinput/json-c development libraries unavailable")
    executable = tmp_path / "home-shade-overlap"
    subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    str(ROOT / "tests/home-shade-overlap-test.c"), "-o", str(executable),
                    *flags.stdout.split(), "-lm"], check=True, timeout=60)
    subprocess.run([str(executable)], check=True, timeout=10)
