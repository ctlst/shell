import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMMAND = ROOT / "session" / "ctlst-session"
DEFAULTS = ROOT / "config" / "defaults"


def run_config(tmp_path: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    environment = os.environ | {
        "HOME": str(tmp_path / "home"),
        "XDG_CONFIG_HOME": str(tmp_path / "config"),
        "CTLST_DATA_DIR": str(ROOT / "config"),
        "CTLST_PROFILE_DIR": str(tmp_path / "profiles"),
        "CTLST_SYSTEM_CONFIG_DIR": str(tmp_path / "etc"),
    }
    return subprocess.run(
        [str(COMMAND), "config", *arguments],
        env=environment,
        text=True,
        capture_output=True,
        check=False,
    )


def test_generic_defaults_do_not_assume_pixel_hardware_or_memory_policy():
    input_config = (DEFAULTS / "input.conf").read_text(encoding="utf-8")
    memory = (DEFAULTS / "memory.conf").read_text(encoding="utf-8")

    assert "output = auto" in input_config
    assert "touch_device = auto" in input_config
    assert "DSI-1" not in input_config
    assert "enabled = false" in memory
    assert "memory_high_bytes = 0" in memory


def test_show_is_a_labeled_cat_of_the_user_dotfile(tmp_path):
    missing = run_config(tmp_path, "show", "memory")
    assert missing.returncode == 0
    assert "# user:" in missing.stdout
    assert "# not created" in missing.stdout

    user = tmp_path / "config" / "ctlst" / "memory.conf"
    user.parent.mkdir(parents=True)
    user.write_text("[lifecycle]\nenabled = true\n", encoding="utf-8")
    shown = run_config(tmp_path, "show", "memory")
    assert shown.stdout.endswith("[lifecycle]\nenabled = true\n")


def test_notes_scope_validates_editor_arguments(tmp_path):
    assert run_config(tmp_path, "effective", "notes").returncode == 0
    user = tmp_path / "config/ctlst"
    user.mkdir(parents=True)
    (user / "notes.conf").write_text('[notes]\neditor = "shell command"\n')
    result = run_config(tmp_path, "check")
    assert result.returncode != 0 and "notes:" in result.stderr


def test_effective_values_include_their_origin(tmp_path):
    profiles = tmp_path / "profiles" / "10-tablet"
    profiles.mkdir(parents=True)
    (profiles / "memory.conf").write_text(
        "[app]\nmemory_high_bytes = 2147483648\n", encoding="utf-8"
    )
    user = tmp_path / "config" / "ctlst"
    user.mkdir(parents=True)
    (user / "memory.conf").write_text(
        "[lifecycle]\nenabled = false\n", encoding="utf-8"
    )

    result = run_config(tmp_path, "effective", "memory")

    assert result.returncode == 0
    assert "memory_high_bytes = 2147483648  # profile 10-tablet" in result.stdout
    assert "enabled = false  # user" in result.stdout
    assert "manage_flatpak = false  # packaged default" in result.stdout


def test_check_reports_invalid_user_ini(tmp_path):
    user = tmp_path / "config" / "ctlst"
    user.mkdir(parents=True)
    (user / "session.conf").write_text("not ini\n", encoding="utf-8")

    result = run_config(tmp_path, "check")

    assert result.returncode == 1
    assert "session:" in result.stderr


def test_start_renders_layered_sway_config_without_populating_home(tmp_path):
    home = tmp_path / "home"
    runtime = tmp_path / "runtime"
    binaries = tmp_path / "bin"
    profiles = tmp_path / "profiles"
    system = tmp_path / "etc"
    user = home / ".config" / "ctlst"
    for directory in (home, runtime, binaries, profiles / "10-test", system, user):
        directory.mkdir(parents=True, exist_ok=True)
    (profiles / "10-test" / "sway.conf").write_text(
        "set $profile_loaded yes\n", encoding="utf-8"
    )
    (system / "sway.conf").write_text(
        "set $system_loaded yes\n", encoding="utf-8"
    )
    (user / "sway.conf").write_text("set $user_loaded yes\n", encoding="utf-8")
    fake_sway = binaries / "sway"
    fake_sway.write_text(
        "#!/bin/sh\nprintf '%s\\n' \"$@\"\nprintf 'data=%s\\n' \"$CTLST_DATA_DIR\"\n",
        encoding="utf-8",
    )
    fake_sway.chmod(0o755)
    environment = os.environ | {
        "HOME": str(home),
        "XDG_CONFIG_HOME": str(home / ".config"),
        "XDG_RUNTIME_DIR": str(runtime),
        "CTLST_DATA_DIR": str(ROOT),
        "CTLST_PROFILE_DIR": str(profiles),
        "CTLST_SYSTEM_CONFIG_DIR": str(system),
        "PATH": f"{binaries}:{os.environ['PATH']}",
    }

    result = subprocess.run(
        [str(COMMAND), "start"],
        env=environment,
        text=True,
        capture_output=True,
        check=False,
    )

    assert result.returncode == 0
    assert result.stdout.splitlines()[0] == "--config"
    rendered_path = Path(result.stdout.splitlines()[1])
    assert rendered_path == runtime / "ctlst-shell" / "sway.conf"
    rendered = rendered_path.read_text(encoding="utf-8")
    expected = [
        ROOT / "sway" / "ctlst.conf",
        DEFAULTS / "sway.conf",
        profiles / "10-test" / "sway.conf",
        system / "sway.conf",
        user / "sway.conf",
    ]
    positions = [rendered.index(str(path)) for path in expected]
    assert positions == sorted(positions)
    assert f"data={ROOT}" in result.stdout
    assert sorted(user.iterdir()) == [user / "sway.conf"]
