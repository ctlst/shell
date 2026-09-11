"""Behavioral checks for configuration-to-launch wiring, independent of a compositor."""
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def environment(tmp_path):
    return os.environ | {
        "HOME": str(tmp_path / "home"),
        "XDG_CONFIG_HOME": str(tmp_path / "config"),
        "CTLST_DATA_DIR": str(ROOT),
        "CTLST_LIBEXEC_DIR": str(ROOT / "scripts"),
        "CTLST_PROFILE_DIR": str(tmp_path / "profiles"),
        "CTLST_SYSTEM_CONFIG_DIR": str(tmp_path / "etc"),
    }


def test_unlimited_generic_launch_does_not_request_pixel_limits(tmp_path):
    result = subprocess.run(
        [str(ROOT / "scripts/app-run"), "--explain", "command-new", "test", "true"],
        env=environment(tmp_path), capture_output=True, text=True, check=True,
    )
    plan = json.loads(result.stdout)
    assert plan["command"] == ["true"]
    assert set(plan["limits"].values()) == {0}


def test_user_memory_dotfile_controls_real_launch_plan(tmp_path):
    config = tmp_path / "config/ctlst"
    config.mkdir(parents=True)
    (config / "memory.conf").write_text("[app]\nmemory_high_bytes = 67108864\nmemory_max_bytes = 134217728\n")
    result = subprocess.run(
        [str(ROOT / "scripts/app-run"), "--explain", "command", "test", "true"],
        env=environment(tmp_path), capture_output=True, text=True, check=True,
    )
    assert json.loads(result.stdout)["limits"] == {
        "MemoryHigh": 67108864, "MemoryMax": 134217728, "MemorySwapMax": 0,
    }


def test_unknown_and_unsupported_settings_are_rejected(tmp_path):
    config = tmp_path / "config/ctlst"
    config.mkdir(parents=True)
    for text in ("[app]\nmemory_max_bytes = -1\n",
                 "[lifecycle]\nenabled = true\n",
                 "[app]\nmemory_hogh_bytes = 1\n"):
        (config / "memory.conf").write_text(text)
        result = subprocess.run(
            [str(ROOT / "session/ctlst-session"), "config", "check"],
            env=environment(tmp_path), capture_output=True, text=True,
        )
        assert result.returncode != 0, text


def test_terminal_role_obeys_user_handler(tmp_path):
    config = tmp_path / "config/ctlst"
    config.mkdir(parents=True)
    (config / "applications.conf").write_text("[applications]\nterminal = custom-terminal\n")
    result = subprocess.run(
        [str(ROOT / "scripts/app-run"), "--explain", "role", "terminal"],
        env=environment(tmp_path), capture_output=True, text=True, check=True,
    )
    assert json.loads(result.stdout)["command"] == ["custom-terminal"]


def test_invalid_float_and_multiline_values_are_rejected(tmp_path):
    config = tmp_path / "config/ctlst"
    config.mkdir(parents=True)
    for text in ("[dock]\nscroll_detent = nan\n", "[dock]\nscroll_detent = -1\n"):
        (config / "gestures.conf").write_text(text)
        result = subprocess.run([str(ROOT / "session/ctlst-session"), "config", "check"],
                                env=environment(tmp_path), capture_output=True)
        assert result.returncode != 0
    (config / "gestures.conf").unlink()
    (config / "applications.conf").write_text("[applications]\nterminal = foot\n  injected\n")
    result = subprocess.run([str(ROOT / "session/ctlst-session"), "config", "check"],
                            env=environment(tmp_path), capture_output=True)
    assert result.returncode != 0


def test_session_menu_resolves_installed_helpers(tmp_path):
    result = subprocess.run(
        [str(ROOT / "scripts/session-menu"), "--resolve", "Close focused app"],
        env=environment(tmp_path), capture_output=True, text=True, check=True,
    )
    assert str(ROOT / "scripts/close-active") in result.stdout
    assert "sway-touch" not in result.stdout


def test_drawer_keys_and_dock_pointer_events_cover_child_widgets():
    drawer = (ROOT / "scripts/ctlstdrawer-ui").read_text()
    dock = (ROOT / "ctlstdock/ctlstdock.c").read_text()
    assert "keys.set_propagation_phase(Gtk.PropagationPhase.CAPTURE)" in drawer
    assert "not search_focused and codepoint" in drawer
    assert "focus.is_ancestor(self.search)" in drawer
    assert "Gtk4LayerShell.KeyboardMode.EXCLUSIVE if focus" in drawer
    assert "gtk_widget_add_controller(GTK_WIDGET(dock->window), scroll)" in dock
    assert 'G_CALLBACK(dock_pointer_enter)' in dock
    assert "gdk_surface_get_height(surface)" in dock


def test_touch_discovery_does_not_assume_bonito_event_path():
    gesture = (ROOT / "ctlst-gestured/ctlst-gestured.c").read_text()
    assert 'getenv("CTLST_TOUCH_DEVICE")' in gesture
    assert 'glob("/dev/input/event*"' in gesture
    assert "INPUT_PROP_DIRECT" in gesture
    assert "platform-a84000" not in gesture
    assert "input_retry_deadline" in gesture


def test_logout_cleanup_is_scoped_to_this_login_not_binary_names():
    session = (ROOT / "session/ctlst-session").read_text()
    assert 'environment["CTLST_SESSION_ID"] = token' in session
    assert "process.stat().st_uid == os.getuid()" in session
    assert 'marker in (process / "environ").read_bytes().split(b"\\0")' in session
    assert "cleanup_session(token)" in session
    assert "pkill" not in session


def test_drawer_waits_for_a_replaced_stale_endpoint(tmp_path, request):
    import socket
    import sys
    import tempfile
    import threading
    import time
    # macOS pytest roots can exceed the Unix socket pathname limit.
    temporary = tempfile.TemporaryDirectory(prefix="ctlst-", dir="/tmp")
    request.addfinalizer(temporary.cleanup)
    runtime = Path(temporary.name)
    path = runtime / "ctlst-drawer.sock"
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as stale:
        stale.bind(str(path))
    received = []
    def resident():
        time.sleep(0.25)
        path.unlink()
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as endpoint:
            endpoint.bind(str(path))
            endpoint.settimeout(4)
            received.append(endpoint.recv(1))
    worker = threading.Thread(target=resident)
    worker.start()
    try:
        result = subprocess.run([sys.executable, str(ROOT / "scripts/ctlstdrawer"), "show"],
                                env=environment(tmp_path) | {"XDG_RUNTIME_DIR": str(runtime)},
                                capture_output=True, timeout=5)
        assert result.returncode == 0, result.stderr
    finally:
        worker.join(timeout=5)
    assert received == [b"S"]


def test_launch_reservation_timeout_is_dotfile_controlled(tmp_path):
    import sys
    config = tmp_path / "config/ctlst"
    config.mkdir(parents=True)
    (config / "session.conf").write_text("[session]\nlaunch_timeout_seconds = 60\n")
    result = subprocess.run([str(ROOT / "scripts/ctlst-component"), sys.executable, "-c",
                             "import os; print(os.environ['CTLST_LAUNCH_TIMEOUT_SECONDS'])"],
                            env=environment(tmp_path), capture_output=True, text=True, check=True)
    assert result.stdout.strip() == "60"
