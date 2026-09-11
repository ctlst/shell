import os
from pathlib import Path
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]


def invoke(tmp_path, *args, editor=None, data_dir=ROOT):
    env = os.environ | {
        "XDG_CONFIG_HOME": str(tmp_path / "config"),
        "CTLST_PROFILE_DIR": str(tmp_path / "profiles"),
        "CTLST_SYSTEM_CONFIG_DIR": str(tmp_path / "etc"),
        "CTLST_DATA_DIR": str(data_dir),
        "CTLST_LIBEXEC_DIR": str(ROOT / "scripts"),
    }
    env.pop("VISUAL", None)
    env.pop("EDITOR", None)
    if editor is not None:
        env["EDITOR"] = editor
    return subprocess.run([str(ROOT / "session/ctlst-session"), "config", *args],
                          env=env, text=True, capture_output=True)


@pytest.mark.parametrize("location", ["config/ctlst", "profiles/10-test", "etc"])
@pytest.mark.parametrize("kind", ["directory", "dangling-link"])
def test_invalid_ini_path_is_not_silently_ignored(tmp_path, location, kind):
    path = tmp_path / location / "memory.conf"
    path.parent.mkdir(parents=True)
    if kind == "directory":
        path.mkdir()
    else:
        path.symlink_to(tmp_path / "missing")
    result = invoke(tmp_path, "check")
    assert result.returncode == 1
    assert "expected a readable configuration file" in result.stderr
    assert str(path) in result.stderr


@pytest.mark.parametrize("editor", [None, " ", "ctlst-nonexistent-editor", "'bad quote"])
def test_bad_editor_does_not_create_dotfiles(tmp_path, editor):
    result = invoke(tmp_path, "edit", "memory", editor=editor)
    assert result.returncode != 0
    assert not (tmp_path / "config").exists()
    assert "Traceback" not in result.stderr


def test_edit_creates_defaults_once_and_preserves_user_contents(tmp_path):
    result = invoke(tmp_path, "edit", "memory", editor="true")
    assert result.returncode == 0, result.stderr
    path = tmp_path / "config/ctlst/memory.conf"
    assert path.read_text() == (ROOT / "config/defaults/memory.conf").read_text()
    path.write_text("# My existing custom file\n")
    assert invoke(tmp_path, "edit", "memory", editor="true").returncode == 0
    assert path.read_text() == "# My existing custom file\n"


def test_unreadable_template_does_not_leave_empty_override(tmp_path):
    defaults = tmp_path / "data/defaults"
    defaults.mkdir(parents=True)
    (defaults / "memory.conf").write_bytes(b"\xff")
    result = invoke(tmp_path, "edit", "memory", editor="true", data_dir=defaults.parent)
    assert result.returncode != 0
    assert not (tmp_path / "config/ctlst/memory.conf").exists()
